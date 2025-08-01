// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2008	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright 2015-2017	Intel Deutschland GmbH
 * Copyright 2018-2020, 2022-2025  Intel Corporation
 */

#include <crypto/utils.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include <linux/unaligned.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "debugfs_key.h"
#include "aes_ccm.h"
#include "aes_cmac.h"
#include "aes_gmac.h"
#include "aes_gcm.h"


/**
 * DOC: Key handling basics
 *
 * Key handling in mac80211 is done based on per-interface (sub_if_data)
 * keys and per-station keys. Since each station belongs to an interface,
 * each station key also belongs to that interface.
 *
 * Hardware acceleration is done on a best-effort basis for algorithms
 * that are implemented in software,  for each key the hardware is asked
 * to enable that key for offloading but if it cannot do that the key is
 * simply kept for software encryption (unless it is for an algorithm
 * that isn't implemented in software).
 * There is currently no way of knowing whether a key is handled in SW
 * or HW except by looking into debugfs.
 *
 * All key management is internally protected by a mutex. Within all
 * other parts of mac80211, key references are, just as STA structure
 * references, protected by RCU. Note, however, that some things are
 * unprotected, namely the key->sta dereferences within the hardware
 * acceleration functions. This means that sta_info_destroy() must
 * remove the key which waits for an RCU grace period.
 */

static const u8 bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void
update_vlan_tailroom_need_count(struct ieee80211_sub_if_data *sdata, int delta)
{
	struct ieee80211_sub_if_data *vlan;

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return;

	/* crypto_tx_tailroom_needed_cnt is protected by this */
	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	rcu_read_lock();

	list_for_each_entry_rcu(vlan, &sdata->u.ap.vlans, u.vlan.list)
		vlan->crypto_tx_tailroom_needed_cnt += delta;

	rcu_read_unlock();
}

static void increment_tailroom_need_count(struct ieee80211_sub_if_data *sdata)
{
	/*
	 * When this count is zero, SKB resizing for allocating tailroom
	 * for IV or MMIC is skipped. But, this check has created two race
	 * cases in xmit path while transiting from zero count to one:
	 *
	 * 1. SKB resize was skipped because no key was added but just before
	 * the xmit key is added and SW encryption kicks off.
	 *
	 * 2. SKB resize was skipped because all the keys were hw planted but
	 * just before xmit one of the key is deleted and SW encryption kicks
	 * off.
	 *
	 * In both the above case SW encryption will find not enough space for
	 * tailroom and exits with WARN_ON. (See WARN_ONs at wpa.c)
	 *
	 * Solution has been explained at
	 * http://mid.gmane.org/1308590980.4322.19.camel@jlt3.sipsolutions.net
	 */

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	update_vlan_tailroom_need_count(sdata, 1);

	if (!sdata->crypto_tx_tailroom_needed_cnt++) {
		/*
		 * Flush all XMIT packets currently using HW encryption or no
		 * encryption at all if the count transition is from 0 -> 1.
		 */
		synchronize_net();
	}
}

static void decrease_tailroom_need_count(struct ieee80211_sub_if_data *sdata,
					 int delta)
{
	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	WARN_ON_ONCE(sdata->crypto_tx_tailroom_needed_cnt < delta);

	update_vlan_tailroom_need_count(sdata, -delta);
	sdata->crypto_tx_tailroom_needed_cnt -= delta;
}

static int ieee80211_key_enable_hw_accel(struct ieee80211_key *key)
{
	struct ieee80211_sub_if_data *sdata = key->sdata;
	struct sta_info *sta;
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(key->local->hw.wiphy);

	if (key->flags & KEY_FLAG_TAINTED) {
		/* If we get here, it's during resume and the key is
		 * tainted so shouldn't be used/programmed any more.
		 * However, its flags may still indicate that it was
		 * programmed into the device (since we're in resume)
		 * so clear that flag now to avoid trying to remove
		 * it again later.
		 */
		if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE &&
		    !(key->conf.flags & (IEEE80211_KEY_FLAG_GENERATE_MMIC |
					 IEEE80211_KEY_FLAG_PUT_MIC_SPACE |
					 IEEE80211_KEY_FLAG_RESERVE_TAILROOM)))
			increment_tailroom_need_count(sdata);

		key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
		return -EINVAL;
	}

	if (!key->local->ops->set_key)
		goto out_unsupported;

	sta = key->sta;

	/*
	 * If this is a per-STA GTK, check if it
	 * is supported; if not, return.
	 */
	if (sta && !(key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE) &&
	    !ieee80211_hw_check(&key->local->hw, SUPPORTS_PER_STA_GTK))
		goto out_unsupported;

	if (sta && !sta->uploaded)
		goto out_unsupported;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		/*
		 * The driver doesn't know anything about VLAN interfaces.
		 * Hence, don't send GTKs for VLAN interfaces to the driver.
		 */
		if (!(key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
			ret = 1;
			goto out_unsupported;
		}
	}

	if (key->conf.link_id >= 0 && sdata->vif.active_links &&
	    !(sdata->vif.active_links & BIT(key->conf.link_id)))
		return 0;

	ret = drv_set_key(key->local, SET_KEY, sdata,
			  sta ? &sta->sta : NULL, &key->conf);

	if (!ret) {
		key->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;

		if (!(key->conf.flags & (IEEE80211_KEY_FLAG_GENERATE_MMIC |
					 IEEE80211_KEY_FLAG_PUT_MIC_SPACE |
					 IEEE80211_KEY_FLAG_RESERVE_TAILROOM)))
			decrease_tailroom_need_count(sdata, 1);

		WARN_ON((key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE) &&
			(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV));

		WARN_ON((key->conf.flags & IEEE80211_KEY_FLAG_PUT_MIC_SPACE) &&
			(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_MMIC));

		return 0;
	}

	if (ret != -ENOSPC && ret != -EOPNOTSUPP && ret != 1)
		sdata_err(sdata,
			  "failed to set key (%d, %pM) to hardware (%d)\n",
			  key->conf.keyidx,
			  sta ? sta->sta.addr : bcast_addr, ret);

 out_unsupported:
	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		/* all of these we can do in software - if driver can */
		if (ret == 1)
			return 0;
		if (ieee80211_hw_check(&key->local->hw, SW_CRYPTO_CONTROL))
			return -EINVAL;
		return 0;
	default:
		return -EINVAL;
	}
}

static void ieee80211_key_disable_hw_accel(struct ieee80211_key *key)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	int ret;

	might_sleep();

	if (!key || !key->local->ops->set_key)
		return;

	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		return;

	sta = key->sta;
	sdata = key->sdata;

	lockdep_assert_wiphy(key->local->hw.wiphy);

	if (key->conf.link_id >= 0 && sdata->vif.active_links &&
	    !(sdata->vif.active_links & BIT(key->conf.link_id)))
		return;

	if (!(key->conf.flags & (IEEE80211_KEY_FLAG_GENERATE_MMIC |
				 IEEE80211_KEY_FLAG_PUT_MIC_SPACE |
				 IEEE80211_KEY_FLAG_RESERVE_TAILROOM)))
		increment_tailroom_need_count(sdata);

	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
	ret = drv_set_key(key->local, DISABLE_KEY, sdata,
			  sta ? &sta->sta : NULL, &key->conf);

	if (ret)
		sdata_err(sdata,
			  "failed to remove key (%d, %pM) from hardware (%d)\n",
			  key->conf.keyidx,
			  sta ? sta->sta.addr : bcast_addr, ret);
}

static int _ieee80211_set_tx_key(struct ieee80211_key *key, bool force)
{
	struct sta_info *sta = key->sta;
	struct ieee80211_local *local = key->local;

	lockdep_assert_wiphy(local->hw.wiphy);

	set_sta_flag(sta, WLAN_STA_USES_ENCRYPTION);

	sta->ptk_idx = key->conf.keyidx;

	if (force || !ieee80211_hw_check(&local->hw, AMPDU_KEYBORDER_SUPPORT))
		clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
	ieee80211_check_fast_xmit(sta);

	return 0;
}

int ieee80211_set_tx_key(struct ieee80211_key *key)
{
	return _ieee80211_set_tx_key(key, false);
}

static void ieee80211_pairwise_rekey(struct ieee80211_key *old,
				     struct ieee80211_key *new)
{
	struct ieee80211_local *local = new->local;
	struct sta_info *sta = new->sta;
	int i;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (new->conf.flags & IEEE80211_KEY_FLAG_NO_AUTO_TX) {
		/* Extended Key ID key install, initial one or rekey */

		if (sta->ptk_idx != INVALID_PTK_KEYIDX &&
		    !ieee80211_hw_check(&local->hw, AMPDU_KEYBORDER_SUPPORT)) {
			/* Aggregation Sessions with Extended Key ID must not
			 * mix MPDUs with different keyIDs within one A-MPDU.
			 * Tear down running Tx aggregation sessions and block
			 * new Rx/Tx aggregation requests during rekey to
			 * ensure there are no A-MPDUs when the driver is not
			 * supporting A-MPDU key borders. (Blocking Tx only
			 * would be sufficient but WLAN_STA_BLOCK_BA gets the
			 * job done for the few ms we need it.)
			 */
			set_sta_flag(sta, WLAN_STA_BLOCK_BA);
			for (i = 0; i <  IEEE80211_NUM_TIDS; i++)
				__ieee80211_stop_tx_ba_session(sta, i,
							       AGG_STOP_LOCAL_REQUEST);
		}
	} else if (old) {
		/* Rekey without Extended Key ID.
		 * Aggregation sessions are OK when running on SW crypto.
		 * A broken remote STA may cause issues not observed with HW
		 * crypto, though.
		 */
		if (!(old->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
			return;

		/* Stop Tx till we are on the new key */
		old->flags |= KEY_FLAG_TAINTED;
		ieee80211_clear_fast_xmit(sta);
		if (ieee80211_hw_check(&local->hw, AMPDU_AGGREGATION)) {
			set_sta_flag(sta, WLAN_STA_BLOCK_BA);
			ieee80211_sta_tear_down_BA_sessions(sta,
							    AGG_STOP_LOCAL_REQUEST);
		}
		if (!wiphy_ext_feature_isset(local->hw.wiphy,
					     NL80211_EXT_FEATURE_CAN_REPLACE_PTK0)) {
			pr_warn_ratelimited("Rekeying PTK for STA %pM but driver can't safely do that.",
					    sta->sta.addr);
			/* Flushing the driver queues *may* help prevent
			 * the clear text leaks and freezes.
			 */
			ieee80211_flush_queues(local, old->sdata, false);
		}
	}
}

static void __ieee80211_set_default_key(struct ieee80211_link_data *link,
					int idx, bool uni, bool multi)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_key *key = NULL;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (idx >= 0 && idx < NUM_DEFAULT_KEYS) {
		key = wiphy_dereference(sdata->local->hw.wiphy,
					sdata->keys[idx]);
		if (!key)
			key = wiphy_dereference(sdata->local->hw.wiphy,
						link->gtk[idx]);
	}

	if (uni) {
		rcu_assign_pointer(sdata->default_unicast_key, key);
		ieee80211_check_fast_xmit_iface(sdata);
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN)
			drv_set_default_unicast_key(sdata->local, sdata, idx);
	}

	if (multi)
		rcu_assign_pointer(link->default_multicast_key, key);

	ieee80211_debugfs_key_update_default(sdata);
}

void ieee80211_set_default_key(struct ieee80211_link_data *link, int idx,
			       bool uni, bool multi)
{
	lockdep_assert_wiphy(link->sdata->local->hw.wiphy);

	__ieee80211_set_default_key(link, idx, uni, multi);
}

static void
__ieee80211_set_default_mgmt_key(struct ieee80211_link_data *link, int idx)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_key *key = NULL;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (idx >= NUM_DEFAULT_KEYS &&
	    idx < NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS)
		key = wiphy_dereference(sdata->local->hw.wiphy,
					link->gtk[idx]);

	rcu_assign_pointer(link->default_mgmt_key, key);

	ieee80211_debugfs_key_update_default(sdata);
}

void ieee80211_set_default_mgmt_key(struct ieee80211_link_data *link,
				    int idx)
{
	lockdep_assert_wiphy(link->sdata->local->hw.wiphy);

	__ieee80211_set_default_mgmt_key(link, idx);
}

static void
__ieee80211_set_default_beacon_key(struct ieee80211_link_data *link, int idx)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_key *key = NULL;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (idx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS &&
	    idx < NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS +
	    NUM_DEFAULT_BEACON_KEYS)
		key = wiphy_dereference(sdata->local->hw.wiphy,
					link->gtk[idx]);

	rcu_assign_pointer(link->default_beacon_key, key);

	ieee80211_debugfs_key_update_default(sdata);
}

void ieee80211_set_default_beacon_key(struct ieee80211_link_data *link,
				      int idx)
{
	lockdep_assert_wiphy(link->sdata->local->hw.wiphy);

	__ieee80211_set_default_beacon_key(link, idx);
}

static int ieee80211_key_replace(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_link_data *link,
				 struct sta_info *sta,
				 bool pairwise,
				 struct ieee80211_key *old,
				 struct ieee80211_key *new)
{
	struct link_sta_info *link_sta = sta ? &sta->deflink : NULL;
	int link_id;
	int idx;
	int ret = 0;
	bool defunikey, defmultikey, defmgmtkey, defbeaconkey;
	bool is_wep;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	/* caller must provide at least one old/new */
	if (WARN_ON(!new && !old))
		return 0;

	if (new) {
		idx = new->conf.keyidx;
		is_wep = new->conf.cipher == WLAN_CIPHER_SUITE_WEP40 ||
			 new->conf.cipher == WLAN_CIPHER_SUITE_WEP104;
		link_id = new->conf.link_id;
	} else {
		idx = old->conf.keyidx;
		is_wep = old->conf.cipher == WLAN_CIPHER_SUITE_WEP40 ||
			 old->conf.cipher == WLAN_CIPHER_SUITE_WEP104;
		link_id = old->conf.link_id;
	}

	if (WARN(old && old->conf.link_id != link_id,
		 "old link ID %d doesn't match new link ID %d\n",
		 old->conf.link_id, link_id))
		return -EINVAL;

	if (link_id >= 0) {
		if (!link) {
			link = sdata_dereference(sdata->link[link_id], sdata);
			if (!link)
				return -ENOLINK;
		}

		if (sta) {
			link_sta = rcu_dereference_protected(sta->link[link_id],
							     lockdep_is_held(&sta->local->hw.wiphy->mtx));
			if (!link_sta)
				return -ENOLINK;
		}
	} else {
		link = &sdata->deflink;
	}

	if ((is_wep || pairwise) && idx >= NUM_DEFAULT_KEYS)
		return -EINVAL;

	WARN_ON(new && old && new->conf.keyidx != old->conf.keyidx);

	if (new && sta && pairwise) {
		/* Unicast rekey needs special handling. With Extended Key ID
		 * old is still NULL for the first rekey.
		 */
		ieee80211_pairwise_rekey(old, new);
	}

	if (old) {
		if (old->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) {
			ieee80211_key_disable_hw_accel(old);

			if (new)
				ret = ieee80211_key_enable_hw_accel(new);
		}
	} else {
		if (!new->local->wowlan)
			ret = ieee80211_key_enable_hw_accel(new);
		else if (link_id < 0 || !sdata->vif.active_links ||
			 BIT(link_id) & sdata->vif.active_links)
			new->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;
	}

	if (ret)
		return ret;

	if (new)
		list_add_tail_rcu(&new->list, &sdata->key_list);

	if (sta) {
		if (pairwise) {
			rcu_assign_pointer(sta->ptk[idx], new);
			if (new &&
			    !(new->conf.flags & IEEE80211_KEY_FLAG_NO_AUTO_TX))
				_ieee80211_set_tx_key(new, true);
		} else {
			rcu_assign_pointer(link_sta->gtk[idx], new);
		}
		/* Only needed for transition from no key -> key.
		 * Still triggers unnecessary when using Extended Key ID
		 * and installing the second key ID the first time.
		 */
		if (new && !old)
			ieee80211_check_fast_rx(sta);
	} else {
		defunikey = old &&
			old == wiphy_dereference(sdata->local->hw.wiphy,
						 sdata->default_unicast_key);
		defmultikey = old &&
			old == wiphy_dereference(sdata->local->hw.wiphy,
						 link->default_multicast_key);
		defmgmtkey = old &&
			old == wiphy_dereference(sdata->local->hw.wiphy,
						 link->default_mgmt_key);
		defbeaconkey = old &&
			old == wiphy_dereference(sdata->local->hw.wiphy,
						 link->default_beacon_key);

		if (defunikey && !new)
			__ieee80211_set_default_key(link, -1, true, false);
		if (defmultikey && !new)
			__ieee80211_set_default_key(link, -1, false, true);
		if (defmgmtkey && !new)
			__ieee80211_set_default_mgmt_key(link, -1);
		if (defbeaconkey && !new)
			__ieee80211_set_default_beacon_key(link, -1);

		if (is_wep || pairwise)
			rcu_assign_pointer(sdata->keys[idx], new);
		else
			rcu_assign_pointer(link->gtk[idx], new);

		if (defunikey && new)
			__ieee80211_set_default_key(link, new->conf.keyidx,
						    true, false);
		if (defmultikey && new)
			__ieee80211_set_default_key(link, new->conf.keyidx,
						    false, true);
		if (defmgmtkey && new)
			__ieee80211_set_default_mgmt_key(link,
							 new->conf.keyidx);
		if (defbeaconkey && new)
			__ieee80211_set_default_beacon_key(link,
							   new->conf.keyidx);
	}

	if (old)
		list_del_rcu(&old->list);

	return 0;
}

struct ieee80211_key *
ieee80211_key_alloc(u32 cipher, int idx, size_t key_len,
		    const u8 *key_data,
		    size_t seq_len, const u8 *seq)
{
	struct ieee80211_key *key;
	int i, j, err;

	if (WARN_ON(idx < 0 ||
		    idx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS +
		    NUM_DEFAULT_BEACON_KEYS))
		return ERR_PTR(-EINVAL);

	key = kzalloc(sizeof(struct ieee80211_key) + key_len, GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);

	/*
	 * Default to software encryption; we'll later upload the
	 * key to the hardware if possible.
	 */
	key->conf.flags = 0;
	key->flags = 0;

	key->conf.link_id = -1;
	key->conf.cipher = cipher;
	key->conf.keyidx = idx;
	key->conf.keylen = key_len;
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		key->conf.iv_len = IEEE80211_WEP_IV_LEN;
		key->conf.icv_len = IEEE80211_WEP_ICV_LEN;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key->conf.iv_len = IEEE80211_TKIP_IV_LEN;
		key->conf.icv_len = IEEE80211_TKIP_ICV_LEN;
		if (seq) {
			for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
				key->u.tkip.rx[i].iv32 =
					get_unaligned_le32(&seq[2]);
				key->u.tkip.rx[i].iv16 =
					get_unaligned_le16(seq);
			}
		}
		spin_lock_init(&key->u.tkip.txlock);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key->conf.iv_len = IEEE80211_CCMP_HDR_LEN;
		key->conf.icv_len = IEEE80211_CCMP_MIC_LEN;
		if (seq) {
			for (i = 0; i < IEEE80211_NUM_TIDS + 1; i++)
				for (j = 0; j < IEEE80211_CCMP_PN_LEN; j++)
					key->u.ccmp.rx_pn[i][j] =
						seq[IEEE80211_CCMP_PN_LEN - j - 1];
		}
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.ccmp.tfm = ieee80211_aes_key_setup_encrypt(
			key_data, key_len, IEEE80211_CCMP_MIC_LEN);
		if (IS_ERR(key->u.ccmp.tfm)) {
			err = PTR_ERR(key->u.ccmp.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		key->conf.iv_len = IEEE80211_CCMP_256_HDR_LEN;
		key->conf.icv_len = IEEE80211_CCMP_256_MIC_LEN;
		for (i = 0; seq && i < IEEE80211_NUM_TIDS + 1; i++)
			for (j = 0; j < IEEE80211_CCMP_256_PN_LEN; j++)
				key->u.ccmp.rx_pn[i][j] =
					seq[IEEE80211_CCMP_256_PN_LEN - j - 1];
		/* Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.ccmp.tfm = ieee80211_aes_key_setup_encrypt(
			key_data, key_len, IEEE80211_CCMP_256_MIC_LEN);
		if (IS_ERR(key->u.ccmp.tfm)) {
			err = PTR_ERR(key->u.ccmp.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		key->conf.iv_len = 0;
		if (cipher == WLAN_CIPHER_SUITE_AES_CMAC)
			key->conf.icv_len = sizeof(struct ieee80211_mmie);
		else
			key->conf.icv_len = sizeof(struct ieee80211_mmie_16);
		if (seq)
			for (j = 0; j < IEEE80211_CMAC_PN_LEN; j++)
				key->u.aes_cmac.rx_pn[j] =
					seq[IEEE80211_CMAC_PN_LEN - j - 1];
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.aes_cmac.tfm =
			ieee80211_aes_cmac_key_setup(key_data, key_len);
		if (IS_ERR(key->u.aes_cmac.tfm)) {
			err = PTR_ERR(key->u.aes_cmac.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		key->conf.iv_len = 0;
		key->conf.icv_len = sizeof(struct ieee80211_mmie_16);
		if (seq)
			for (j = 0; j < IEEE80211_GMAC_PN_LEN; j++)
				key->u.aes_gmac.rx_pn[j] =
					seq[IEEE80211_GMAC_PN_LEN - j - 1];
		/* Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.aes_gmac.tfm =
			ieee80211_aes_gmac_key_setup(key_data, key_len);
		if (IS_ERR(key->u.aes_gmac.tfm)) {
			err = PTR_ERR(key->u.aes_gmac.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		key->conf.iv_len = IEEE80211_GCMP_HDR_LEN;
		key->conf.icv_len = IEEE80211_GCMP_MIC_LEN;
		for (i = 0; seq && i < IEEE80211_NUM_TIDS + 1; i++)
			for (j = 0; j < IEEE80211_GCMP_PN_LEN; j++)
				key->u.gcmp.rx_pn[i][j] =
					seq[IEEE80211_GCMP_PN_LEN - j - 1];
		/* Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.gcmp.tfm = ieee80211_aes_gcm_key_setup_encrypt(key_data,
								      key_len);
		if (IS_ERR(key->u.gcmp.tfm)) {
			err = PTR_ERR(key->u.gcmp.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	}
	memcpy(key->conf.key, key_data, key_len);
	INIT_LIST_HEAD(&key->list);

	return key;
}

static void ieee80211_key_free_common(struct ieee80211_key *key)
{
	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		ieee80211_aes_key_free(key->u.ccmp.tfm);
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		ieee80211_aes_cmac_key_free(key->u.aes_cmac.tfm);
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		ieee80211_aes_gmac_key_free(key->u.aes_gmac.tfm);
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		ieee80211_aes_gcm_key_free(key->u.gcmp.tfm);
		break;
	}
	kfree_sensitive(key);
}

static void __ieee80211_key_destroy(struct ieee80211_key *key,
				    bool delay_tailroom)
{
	if (key->local) {
		struct ieee80211_sub_if_data *sdata = key->sdata;

		ieee80211_debugfs_key_remove(key);

		if (delay_tailroom) {
			/* see ieee80211_delayed_tailroom_dec */
			sdata->crypto_tx_tailroom_pending_dec++;
			wiphy_delayed_work_queue(sdata->local->hw.wiphy,
						 &sdata->dec_tailroom_needed_wk,
						 HZ / 2);
		} else {
			decrease_tailroom_need_count(sdata, 1);
		}
	}

	ieee80211_key_free_common(key);
}

static void ieee80211_key_destroy(struct ieee80211_key *key,
				  bool delay_tailroom)
{
	if (!key)
		return;

	/*
	 * Synchronize so the TX path and rcu key iterators
	 * can no longer be using this key before we free/remove it.
	 */
	synchronize_net();

	__ieee80211_key_destroy(key, delay_tailroom);
}

void ieee80211_key_free_unused(struct ieee80211_key *key)
{
	if (!key)
		return;

	WARN_ON(key->sdata || key->local);
	ieee80211_key_free_common(key);
}

static bool ieee80211_key_identical(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_key *old,
				    struct ieee80211_key *new)
{
	u8 tkip_old[WLAN_KEY_LEN_TKIP], tkip_new[WLAN_KEY_LEN_TKIP];
	u8 *tk_old, *tk_new;

	if (!old || new->conf.keylen != old->conf.keylen)
		return false;

	tk_old = old->conf.key;
	tk_new = new->conf.key;

	/*
	 * In station mode, don't compare the TX MIC key, as it's never used
	 * and offloaded rekeying may not care to send it to the host. This
	 * is the case in iwlwifi, for example.
	 */
	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    new->conf.cipher == WLAN_CIPHER_SUITE_TKIP &&
	    new->conf.keylen == WLAN_KEY_LEN_TKIP &&
	    !(new->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		memcpy(tkip_old, tk_old, WLAN_KEY_LEN_TKIP);
		memcpy(tkip_new, tk_new, WLAN_KEY_LEN_TKIP);
		memset(tkip_old + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY, 0, 8);
		memset(tkip_new + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY, 0, 8);
		tk_old = tkip_old;
		tk_new = tkip_new;
	}

	return !crypto_memneq(tk_old, tk_new, new->conf.keylen);
}

int ieee80211_key_link(struct ieee80211_key *key,
		       struct ieee80211_link_data *link,
		       struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	static atomic_t key_color = ATOMIC_INIT(0);
	struct ieee80211_key *old_key = NULL;
	int idx = key->conf.keyidx;
	bool pairwise = key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE;
	/*
	 * We want to delay tailroom updates only for station - in that
	 * case it helps roaming speed, but in other cases it hurts and
	 * can cause warnings to appear.
	 */
	bool delay_tailroom = sdata->vif.type == NL80211_IFTYPE_STATION;
	int ret;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (sta && pairwise) {
		struct ieee80211_key *alt_key;

		old_key = wiphy_dereference(sdata->local->hw.wiphy,
					    sta->ptk[idx]);
		alt_key = wiphy_dereference(sdata->local->hw.wiphy,
					    sta->ptk[idx ^ 1]);

		/* The rekey code assumes that the old and new key are using
		 * the same cipher. Enforce the assumption for pairwise keys.
		 */
		if ((alt_key && alt_key->conf.cipher != key->conf.cipher) ||
		    (old_key && old_key->conf.cipher != key->conf.cipher)) {
			ret = -EOPNOTSUPP;
			goto out;
		}
	} else if (sta) {
		struct link_sta_info *link_sta = &sta->deflink;
		int link_id = key->conf.link_id;

		if (link_id >= 0) {
			link_sta = rcu_dereference_protected(sta->link[link_id],
							     lockdep_is_held(&sta->local->hw.wiphy->mtx));
			if (!link_sta) {
				ret = -ENOLINK;
				goto out;
			}
		}

		old_key = wiphy_dereference(sdata->local->hw.wiphy,
					    link_sta->gtk[idx]);
	} else {
		if (idx < NUM_DEFAULT_KEYS)
			old_key = wiphy_dereference(sdata->local->hw.wiphy,
						    sdata->keys[idx]);
		if (!old_key)
			old_key = wiphy_dereference(sdata->local->hw.wiphy,
						    link->gtk[idx]);
	}

	/* Non-pairwise keys must also not switch the cipher on rekey */
	if (!pairwise) {
		if (old_key && old_key->conf.cipher != key->conf.cipher) {
			ret = -EOPNOTSUPP;
			goto out;
		}
	}

	/*
	 * Silently accept key re-installation without really installing the
	 * new version of the key to avoid nonce reuse or replay issues.
	 */
	if (ieee80211_key_identical(sdata, old_key, key)) {
		ret = -EALREADY;
		goto out;
	}

	key->local = sdata->local;
	key->sdata = sdata;
	key->sta = sta;

	/*
	 * Assign a unique ID to every key so we can easily prevent mixed
	 * key and fragment cache attacks.
	 */
	key->color = atomic_inc_return(&key_color);

	/* keep this flag for easier access later */
	if (sta && sta->sta.spp_amsdu)
		key->conf.flags |= IEEE80211_KEY_FLAG_SPP_AMSDU;

	increment_tailroom_need_count(sdata);

	ret = ieee80211_key_replace(sdata, link, sta, pairwise, old_key, key);

	if (!ret) {
		ieee80211_debugfs_key_add(key);
		ieee80211_key_destroy(old_key, delay_tailroom);
	} else {
		ieee80211_key_free(key, delay_tailroom);
	}

	key = NULL;

 out:
	ieee80211_key_free_unused(key);
	return ret;
}

void ieee80211_key_free(struct ieee80211_key *key, bool delay_tailroom)
{
	if (!key)
		return;

	/*
	 * Replace key with nothingness if it was ever used.
	 */
	if (key->sdata)
		ieee80211_key_replace(key->sdata, NULL, key->sta,
				      key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE,
				      key, NULL);
	ieee80211_key_destroy(key, delay_tailroom);
}

void ieee80211_reenable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;
	struct ieee80211_sub_if_data *vlan;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	sdata->crypto_tx_tailroom_needed_cnt = 0;
	sdata->crypto_tx_tailroom_pending_dec = 0;

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list) {
			vlan->crypto_tx_tailroom_needed_cnt = 0;
			vlan->crypto_tx_tailroom_pending_dec = 0;
		}
	}

	if (ieee80211_sdata_running(sdata)) {
		list_for_each_entry(key, &sdata->key_list, list) {
			increment_tailroom_need_count(sdata);
			ieee80211_key_enable_hw_accel(key);
		}
	}
}

static void
ieee80211_key_iter(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct ieee80211_key *key,
		   void (*iter)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct ieee80211_key_conf *key,
				void *data),
		   void *iter_data)
{
	/* skip keys of station in removal process */
	if (key->sta && key->sta->removed)
		return;
	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		return;
	iter(hw, vif, key->sta ? &key->sta->sta : NULL,
	     &key->conf, iter_data);
}

void ieee80211_iter_keys(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 void (*iter)(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_key_conf *key,
				      void *data),
			 void *iter_data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_key *key, *tmp;
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_wiphy(hw->wiphy);

	if (vif) {
		sdata = vif_to_sdata(vif);
		list_for_each_entry_safe(key, tmp, &sdata->key_list, list)
			ieee80211_key_iter(hw, vif, key, iter, iter_data);
	} else {
		list_for_each_entry(sdata, &local->interfaces, list)
			list_for_each_entry_safe(key, tmp,
						 &sdata->key_list, list)
				ieee80211_key_iter(hw, &sdata->vif, key,
						   iter, iter_data);
	}
}
EXPORT_SYMBOL(ieee80211_iter_keys);

static void
_ieee80211_iter_keys_rcu(struct ieee80211_hw *hw,
			 struct ieee80211_sub_if_data *sdata,
			 void (*iter)(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_key_conf *key,
				      void *data),
			 void *iter_data)
{
	struct ieee80211_key *key;

	list_for_each_entry_rcu(key, &sdata->key_list, list)
		ieee80211_key_iter(hw, &sdata->vif, key, iter, iter_data);
}

void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     void (*iter)(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  struct ieee80211_key_conf *key,
					  void *data),
			     void *iter_data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	if (vif) {
		sdata = vif_to_sdata(vif);
		_ieee80211_iter_keys_rcu(hw, sdata, iter, iter_data);
	} else {
		list_for_each_entry_rcu(sdata, &local->interfaces, list)
			_ieee80211_iter_keys_rcu(hw, sdata, iter, iter_data);
	}
}
EXPORT_SYMBOL(ieee80211_iter_keys_rcu);

static void ieee80211_free_keys_iface(struct ieee80211_sub_if_data *sdata,
				      struct list_head *keys)
{
	struct ieee80211_key *key, *tmp;

	decrease_tailroom_need_count(sdata,
				     sdata->crypto_tx_tailroom_pending_dec);
	sdata->crypto_tx_tailroom_pending_dec = 0;

	ieee80211_debugfs_key_remove_mgmt_default(sdata);
	ieee80211_debugfs_key_remove_beacon_default(sdata);

	list_for_each_entry_safe(key, tmp, &sdata->key_list, list) {
		ieee80211_key_replace(key->sdata, NULL, key->sta,
				      key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE,
				      key, NULL);
		list_add_tail(&key->list, keys);
	}

	ieee80211_debugfs_key_update_default(sdata);
}

void ieee80211_remove_link_keys(struct ieee80211_link_data *link,
				struct list_head *keys)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_key *key, *tmp;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry_safe(key, tmp, &sdata->key_list, list) {
		if (key->conf.link_id != link->link_id)
			continue;
		ieee80211_key_replace(key->sdata, link, key->sta,
				      key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE,
				      key, NULL);
		list_add_tail(&key->list, keys);
	}
}

void ieee80211_free_key_list(struct ieee80211_local *local,
			     struct list_head *keys)
{
	struct ieee80211_key *key, *tmp;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry_safe(key, tmp, keys, list)
		__ieee80211_key_destroy(key, false);
}

void ieee80211_free_keys(struct ieee80211_sub_if_data *sdata,
			 bool force_synchronize)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *vlan;
	struct ieee80211_sub_if_data *master;
	struct ieee80211_key *key, *tmp;
	LIST_HEAD(keys);

	wiphy_delayed_work_cancel(local->hw.wiphy,
				  &sdata->dec_tailroom_needed_wk);

	lockdep_assert_wiphy(local->hw.wiphy);

	ieee80211_free_keys_iface(sdata, &keys);

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list)
			ieee80211_free_keys_iface(vlan, &keys);
	}

	if (!list_empty(&keys) || force_synchronize)
		synchronize_net();
	list_for_each_entry_safe(key, tmp, &keys, list)
		__ieee80211_key_destroy(key, false);

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		if (sdata->bss) {
			master = container_of(sdata->bss,
					      struct ieee80211_sub_if_data,
					      u.ap);

			WARN_ON_ONCE(sdata->crypto_tx_tailroom_needed_cnt !=
				     master->crypto_tx_tailroom_needed_cnt);
		}
	} else {
		WARN_ON_ONCE(sdata->crypto_tx_tailroom_needed_cnt ||
			     sdata->crypto_tx_tailroom_pending_dec);
	}

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list)
			WARN_ON_ONCE(vlan->crypto_tx_tailroom_needed_cnt ||
				     vlan->crypto_tx_tailroom_pending_dec);
	}
}

void ieee80211_free_sta_keys(struct ieee80211_local *local,
			     struct sta_info *sta)
{
	struct ieee80211_key *key;
	int i;

	lockdep_assert_wiphy(local->hw.wiphy);

	for (i = 0; i < ARRAY_SIZE(sta->deflink.gtk); i++) {
		key = wiphy_dereference(local->hw.wiphy, sta->deflink.gtk[i]);
		if (!key)
			continue;
		ieee80211_key_replace(key->sdata, NULL, key->sta,
				      key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE,
				      key, NULL);
		__ieee80211_key_destroy(key, key->sdata->vif.type ==
					NL80211_IFTYPE_STATION);
	}

	for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
		key = wiphy_dereference(local->hw.wiphy, sta->ptk[i]);
		if (!key)
			continue;
		ieee80211_key_replace(key->sdata, NULL, key->sta,
				      key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE,
				      key, NULL);
		__ieee80211_key_destroy(key, key->sdata->vif.type ==
					NL80211_IFTYPE_STATION);
	}
}

void ieee80211_delayed_tailroom_dec(struct wiphy *wiphy,
				    struct wiphy_work *wk)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = container_of(wk, struct ieee80211_sub_if_data,
			     dec_tailroom_needed_wk.work);

	/*
	 * The reason for the delayed tailroom needed decrementing is to
	 * make roaming faster: during roaming, all keys are first deleted
	 * and then new keys are installed. The first new key causes the
	 * crypto_tx_tailroom_needed_cnt to go from 0 to 1, which invokes
	 * the cost of synchronize_net() (which can be slow). Avoid this
	 * by deferring the crypto_tx_tailroom_needed_cnt decrementing on
	 * key removal for a while, so if we roam the value is larger than
	 * zero and no 0->1 transition happens.
	 *
	 * The cost is that if the AP switching was from an AP with keys
	 * to one without, we still allocate tailroom while it would no
	 * longer be needed. However, in the typical (fast) roaming case
	 * within an ESS this usually won't happen.
	 */

	decrease_tailroom_need_count(sdata,
				     sdata->crypto_tx_tailroom_pending_dec);
	sdata->crypto_tx_tailroom_pending_dec = 0;
}

void ieee80211_gtk_rekey_notify(struct ieee80211_vif *vif, const u8 *bssid,
				const u8 *replay_ctr, gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_gtk_rekey_notify(sdata, bssid, replay_ctr);

	cfg80211_gtk_rekey_notify(sdata->dev, bssid, replay_ctr, gfp);
}
EXPORT_SYMBOL_GPL(ieee80211_gtk_rekey_notify);

void ieee80211_get_key_rx_seq(struct ieee80211_key_conf *keyconf,
			      int tid, struct ieee80211_key_seq *seq)
{
	struct ieee80211_key *key;
	const u8 *pn;

	key = container_of(keyconf, struct ieee80211_key, conf);

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (WARN_ON(tid < 0 || tid >= IEEE80211_NUM_TIDS))
			return;
		seq->tkip.iv32 = key->u.tkip.rx[tid].iv32;
		seq->tkip.iv16 = key->u.tkip.rx[tid].iv16;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		if (WARN_ON(tid < -1 || tid >= IEEE80211_NUM_TIDS))
			return;
		if (tid < 0)
			pn = key->u.ccmp.rx_pn[IEEE80211_NUM_TIDS];
		else
			pn = key->u.ccmp.rx_pn[tid];
		memcpy(seq->ccmp.pn, pn, IEEE80211_CCMP_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		if (WARN_ON(tid != 0))
			return;
		pn = key->u.aes_cmac.rx_pn;
		memcpy(seq->aes_cmac.pn, pn, IEEE80211_CMAC_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		if (WARN_ON(tid != 0))
			return;
		pn = key->u.aes_gmac.rx_pn;
		memcpy(seq->aes_gmac.pn, pn, IEEE80211_GMAC_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (WARN_ON(tid < -1 || tid >= IEEE80211_NUM_TIDS))
			return;
		if (tid < 0)
			pn = key->u.gcmp.rx_pn[IEEE80211_NUM_TIDS];
		else
			pn = key->u.gcmp.rx_pn[tid];
		memcpy(seq->gcmp.pn, pn, IEEE80211_GCMP_PN_LEN);
		break;
	}
}
EXPORT_SYMBOL(ieee80211_get_key_rx_seq);

void ieee80211_set_key_rx_seq(struct ieee80211_key_conf *keyconf,
			      int tid, struct ieee80211_key_seq *seq)
{
	struct ieee80211_key *key;
	u8 *pn;

	key = container_of(keyconf, struct ieee80211_key, conf);

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (WARN_ON(tid < 0 || tid >= IEEE80211_NUM_TIDS))
			return;
		key->u.tkip.rx[tid].iv32 = seq->tkip.iv32;
		key->u.tkip.rx[tid].iv16 = seq->tkip.iv16;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		if (WARN_ON(tid < -1 || tid >= IEEE80211_NUM_TIDS))
			return;
		if (tid < 0)
			pn = key->u.ccmp.rx_pn[IEEE80211_NUM_TIDS];
		else
			pn = key->u.ccmp.rx_pn[tid];
		memcpy(pn, seq->ccmp.pn, IEEE80211_CCMP_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		if (WARN_ON(tid != 0))
			return;
		pn = key->u.aes_cmac.rx_pn;
		memcpy(pn, seq->aes_cmac.pn, IEEE80211_CMAC_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		if (WARN_ON(tid != 0))
			return;
		pn = key->u.aes_gmac.rx_pn;
		memcpy(pn, seq->aes_gmac.pn, IEEE80211_GMAC_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (WARN_ON(tid < -1 || tid >= IEEE80211_NUM_TIDS))
			return;
		if (tid < 0)
			pn = key->u.gcmp.rx_pn[IEEE80211_NUM_TIDS];
		else
			pn = key->u.gcmp.rx_pn[tid];
		memcpy(pn, seq->gcmp.pn, IEEE80211_GCMP_PN_LEN);
		break;
	default:
		WARN_ON(1);
		break;
	}
}
EXPORT_SYMBOL_GPL(ieee80211_set_key_rx_seq);

struct ieee80211_key_conf *
ieee80211_gtk_rekey_add(struct ieee80211_vif *vif,
			u8 idx, u8 *key_data, u8 key_len,
			int link_id)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_key *prev_key;
	struct ieee80211_key *key;
	int err;
	struct ieee80211_link_data *link_data =
		link_id < 0 ? &sdata->deflink :
		sdata_dereference(sdata->link[link_id], sdata);

	if (WARN_ON(!link_data))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(!local->wowlan))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(idx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS +
		    NUM_DEFAULT_BEACON_KEYS))
		return ERR_PTR(-EINVAL);

	prev_key = wiphy_dereference(local->hw.wiphy,
				     link_data->gtk[idx]);
	if (!prev_key) {
		if (idx < NUM_DEFAULT_KEYS) {
			for (int i = 0; i < NUM_DEFAULT_KEYS; i++) {
				if (i == idx)
					continue;
				prev_key = wiphy_dereference(local->hw.wiphy,
							     link_data->gtk[i]);
				if (prev_key)
					break;
			}
		} else {
			/* For IGTK we have 4 and 5 and for BIGTK - 6 and 7 */
			prev_key = wiphy_dereference(local->hw.wiphy,
						     link_data->gtk[idx ^ 1]);
		}
	}

	if (WARN_ON(!prev_key))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(key_len < prev_key->conf.keylen))
		return ERR_PTR(-EINVAL);

	key = ieee80211_key_alloc(prev_key->conf.cipher, idx,
				  prev_key->conf.keylen, key_data,
				  0, NULL);
	if (IS_ERR(key))
		return ERR_CAST(key);

	if (sdata->u.mgd.mfp != IEEE80211_MFP_DISABLED)
		key->conf.flags |= IEEE80211_KEY_FLAG_RX_MGMT;

	key->conf.link_id = link_data->link_id;

	err = ieee80211_key_link(key, link_data, NULL);
	if (err)
		return ERR_PTR(err);

	return &key->conf;
}
EXPORT_SYMBOL_GPL(ieee80211_gtk_rekey_add);

void ieee80211_key_mic_failure(struct ieee80211_key_conf *keyconf)
{
	struct ieee80211_key *key;

	key = container_of(keyconf, struct ieee80211_key, conf);

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		key->u.aes_cmac.icverrors++;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		key->u.aes_gmac.icverrors++;
		break;
	default:
		/* ignore the others for now, we don't keep counters now */
		break;
	}
}
EXPORT_SYMBOL_GPL(ieee80211_key_mic_failure);

void ieee80211_key_replay(struct ieee80211_key_conf *keyconf)
{
	struct ieee80211_key *key;

	key = container_of(keyconf, struct ieee80211_key, conf);

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		key->u.ccmp.replays++;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		key->u.aes_cmac.replays++;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		key->u.aes_gmac.replays++;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		key->u.gcmp.replays++;
		break;
	}
}
EXPORT_SYMBOL_GPL(ieee80211_key_replay);

int ieee80211_key_switch_links(struct ieee80211_sub_if_data *sdata,
			       unsigned long del_links_mask,
			       unsigned long add_links_mask)
{
	struct ieee80211_key *key;
	int ret;

	list_for_each_entry(key, &sdata->key_list, list) {
		if (key->conf.link_id < 0 ||
		    !(del_links_mask & BIT(key->conf.link_id)))
			continue;

		/* shouldn't happen for per-link keys */
		WARN_ON(key->sta);

		ieee80211_key_disable_hw_accel(key);
	}

	list_for_each_entry(key, &sdata->key_list, list) {
		if (key->conf.link_id < 0 ||
		    !(add_links_mask & BIT(key->conf.link_id)))
			continue;

		/* shouldn't happen for per-link keys */
		WARN_ON(key->sta);

		ret = ieee80211_key_enable_hw_accel(key);
		if (ret)
			return ret;
	}

	return 0;
}
