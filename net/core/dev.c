// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      NET3    Protocol independent device support routines.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 *              Authors:	Ross Biro
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dahinds@users.sourceforge.net>
 *		Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *		Adam Sulmicki <adam@cfar.umd.edu>
 *              Pekka Riikonen <priikone@poesidon.pspt.fi>
 *
 *	Changes:
 *              D.J. Barrow     :       Fixed bug where dev->refcnt gets set
 *                                      to 2 if register_netdev gets called
 *                                      before net_dev_init & also removed a
 *                                      few lines of code in the process.
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant
 *					stunts to keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into
 *					drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *              Alan Cox        :       Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before
 *					calling netif_rx. Saves a function
 *					call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *              Alan Cox        :       Fixed nasty side effect of device close
 *					changes.
 *		Rudi Cilibrasi	:	Pass the right thing to
 *					set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to
 *					make it work out on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *		Andi Kleen	:	Fix error reporting for SIOCGIFCONF
 *	    Michael Chastain	:	Fix signed/unsigned for SIOCGIFCONF
 *		Cyrus Durgin	:	Cleaned for KMOD
 *		Adam Sulmicki   :	Bug Fix : Network Device Unload
 *					A network device unload needs to purge
 *					the backlog queue.
 *	Paul Rusty Russell	:	SIOCSIFNAME
 *              Pekka Riikonen  :	Netdev boot-time settings code
 *              Andrew Morton   :       Make unregister_netdevice wait
 *                                      indefinitely on dev->refcnt
 *              J Hadi Salim    :       - Backlog queue sampling
 *				        - netif_rx() feedback
 */

#include <linux/uaccess.h>
#include <linux/bitmap.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/isolation.h>
#include <linux/sched/mm.h>
#include <linux/smpboot.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/busy_poll.h>
#include <linux/rtnetlink.h>
#include <linux/stat.h>
#include <net/dsa.h>
#include <net/dst.h>
#include <net/dst_metadata.h>
#include <net/gro.h>
#include <net/netdev_queues.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/checksum.h>
#include <net/xfrm.h>
#include <net/tcx.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netpoll.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <net/iw_handler.h>
#include <asm/current.h>
#include <linux/audit.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/mpls.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <trace/events/napi.h>
#include <trace/events/net.h>
#include <trace/events/skb.h>
#include <trace/events/qdisc.h>
#include <trace/events/xdp.h>
#include <linux/inetdevice.h>
#include <linux/cpu_rmap.h>
#include <linux/static_key.h>
#include <linux/hashtable.h>
#include <linux/vmalloc.h>
#include <linux/if_macvlan.h>
#include <linux/errqueue.h>
#include <linux/hrtimer.h>
#include <linux/netfilter_netdev.h>
#include <linux/crash_dump.h>
#include <linux/sctp.h>
#include <net/udp_tunnel.h>
#include <linux/net_namespace.h>
#include <linux/indirect_call_wrapper.h>
#include <net/devlink.h>
#include <linux/pm_runtime.h>
#include <linux/prandom.h>
#include <linux/once_lite.h>
#include <net/netdev_lock.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/types.h>
#include <net/page_pool/helpers.h>
#include <net/page_pool/memory_provider.h>
#include <net/rps.h>
#include <linux/phy_link_topology.h>

#include "dev.h"
#include "devmem.h"
#include "net-sysfs.h"

static DEFINE_SPINLOCK(ptype_lock);
struct list_head ptype_base[PTYPE_HASH_SIZE] __read_mostly;

static int netif_rx_internal(struct sk_buff *skb);
static int call_netdevice_notifiers_extack(unsigned long val,
					   struct net_device *dev,
					   struct netlink_ext_ack *extack);

static DEFINE_MUTEX(ifalias_mutex);

/* protects napi_hash addition/deletion and napi_gen_id */
static DEFINE_SPINLOCK(napi_hash_lock);

static unsigned int napi_gen_id = NR_CPUS;
static DEFINE_READ_MOSTLY_HASHTABLE(napi_hash, 8);

static inline void dev_base_seq_inc(struct net *net)
{
	unsigned int val = net->dev_base_seq + 1;

	WRITE_ONCE(net->dev_base_seq, val ?: 1);
}

static inline struct hlist_head *dev_name_hash(struct net *net, const char *name)
{
	unsigned int hash = full_name_hash(net, name, strnlen(name, IFNAMSIZ));

	return &net->dev_name_head[hash_32(hash, NETDEV_HASHBITS)];
}

static inline struct hlist_head *dev_index_hash(struct net *net, int ifindex)
{
	return &net->dev_index_head[ifindex & (NETDEV_HASHENTRIES - 1)];
}

#ifndef CONFIG_PREEMPT_RT

static DEFINE_STATIC_KEY_FALSE(use_backlog_threads_key);

static int __init setup_backlog_napi_threads(char *arg)
{
	static_branch_enable(&use_backlog_threads_key);
	return 0;
}
early_param("thread_backlog_napi", setup_backlog_napi_threads);

static bool use_backlog_threads(void)
{
	return static_branch_unlikely(&use_backlog_threads_key);
}

#else

static bool use_backlog_threads(void)
{
	return true;
}

#endif

static inline void backlog_lock_irq_save(struct softnet_data *sd,
					 unsigned long *flags)
{
	if (IS_ENABLED(CONFIG_RPS) || use_backlog_threads())
		spin_lock_irqsave(&sd->input_pkt_queue.lock, *flags);
	else
		local_irq_save(*flags);
}

static inline void backlog_lock_irq_disable(struct softnet_data *sd)
{
	if (IS_ENABLED(CONFIG_RPS) || use_backlog_threads())
		spin_lock_irq(&sd->input_pkt_queue.lock);
	else
		local_irq_disable();
}

static inline void backlog_unlock_irq_restore(struct softnet_data *sd,
					      unsigned long *flags)
{
	if (IS_ENABLED(CONFIG_RPS) || use_backlog_threads())
		spin_unlock_irqrestore(&sd->input_pkt_queue.lock, *flags);
	else
		local_irq_restore(*flags);
}

static inline void backlog_unlock_irq_enable(struct softnet_data *sd)
{
	if (IS_ENABLED(CONFIG_RPS) || use_backlog_threads())
		spin_unlock_irq(&sd->input_pkt_queue.lock);
	else
		local_irq_enable();
}

static struct netdev_name_node *netdev_name_node_alloc(struct net_device *dev,
						       const char *name)
{
	struct netdev_name_node *name_node;

	name_node = kmalloc(sizeof(*name_node), GFP_KERNEL);
	if (!name_node)
		return NULL;
	INIT_HLIST_NODE(&name_node->hlist);
	name_node->dev = dev;
	name_node->name = name;
	return name_node;
}

static struct netdev_name_node *
netdev_name_node_head_alloc(struct net_device *dev)
{
	struct netdev_name_node *name_node;

	name_node = netdev_name_node_alloc(dev, dev->name);
	if (!name_node)
		return NULL;
	INIT_LIST_HEAD(&name_node->list);
	return name_node;
}

static void netdev_name_node_free(struct netdev_name_node *name_node)
{
	kfree(name_node);
}

static void netdev_name_node_add(struct net *net,
				 struct netdev_name_node *name_node)
{
	hlist_add_head_rcu(&name_node->hlist,
			   dev_name_hash(net, name_node->name));
}

static void netdev_name_node_del(struct netdev_name_node *name_node)
{
	hlist_del_rcu(&name_node->hlist);
}

static struct netdev_name_node *netdev_name_node_lookup(struct net *net,
							const char *name)
{
	struct hlist_head *head = dev_name_hash(net, name);
	struct netdev_name_node *name_node;

	hlist_for_each_entry(name_node, head, hlist)
		if (!strcmp(name_node->name, name))
			return name_node;
	return NULL;
}

static struct netdev_name_node *netdev_name_node_lookup_rcu(struct net *net,
							    const char *name)
{
	struct hlist_head *head = dev_name_hash(net, name);
	struct netdev_name_node *name_node;

	hlist_for_each_entry_rcu(name_node, head, hlist)
		if (!strcmp(name_node->name, name))
			return name_node;
	return NULL;
}

bool netdev_name_in_use(struct net *net, const char *name)
{
	return netdev_name_node_lookup(net, name);
}
EXPORT_SYMBOL(netdev_name_in_use);

int netdev_name_node_alt_create(struct net_device *dev, const char *name)
{
	struct netdev_name_node *name_node;
	struct net *net = dev_net(dev);

	name_node = netdev_name_node_lookup(net, name);
	if (name_node)
		return -EEXIST;
	name_node = netdev_name_node_alloc(dev, name);
	if (!name_node)
		return -ENOMEM;
	netdev_name_node_add(net, name_node);
	/* The node that holds dev->name acts as a head of per-device list. */
	list_add_tail_rcu(&name_node->list, &dev->name_node->list);

	return 0;
}

static void netdev_name_node_alt_free(struct rcu_head *head)
{
	struct netdev_name_node *name_node =
		container_of(head, struct netdev_name_node, rcu);

	kfree(name_node->name);
	netdev_name_node_free(name_node);
}

static void __netdev_name_node_alt_destroy(struct netdev_name_node *name_node)
{
	netdev_name_node_del(name_node);
	list_del(&name_node->list);
	call_rcu(&name_node->rcu, netdev_name_node_alt_free);
}

int netdev_name_node_alt_destroy(struct net_device *dev, const char *name)
{
	struct netdev_name_node *name_node;
	struct net *net = dev_net(dev);

	name_node = netdev_name_node_lookup(net, name);
	if (!name_node)
		return -ENOENT;
	/* lookup might have found our primary name or a name belonging
	 * to another device.
	 */
	if (name_node == dev->name_node || name_node->dev != dev)
		return -EINVAL;

	__netdev_name_node_alt_destroy(name_node);
	return 0;
}

static void netdev_name_node_alt_flush(struct net_device *dev)
{
	struct netdev_name_node *name_node, *tmp;

	list_for_each_entry_safe(name_node, tmp, &dev->name_node->list, list) {
		list_del(&name_node->list);
		netdev_name_node_alt_free(&name_node->rcu);
	}
}

/* Device list insertion */
static void list_netdevice(struct net_device *dev)
{
	struct netdev_name_node *name_node;
	struct net *net = dev_net(dev);

	ASSERT_RTNL();

	list_add_tail_rcu(&dev->dev_list, &net->dev_base_head);
	netdev_name_node_add(net, dev->name_node);
	hlist_add_head_rcu(&dev->index_hlist,
			   dev_index_hash(net, dev->ifindex));

	netdev_for_each_altname(dev, name_node)
		netdev_name_node_add(net, name_node);

	/* We reserved the ifindex, this can't fail */
	WARN_ON(xa_store(&net->dev_by_index, dev->ifindex, dev, GFP_KERNEL));

	dev_base_seq_inc(net);
}

/* Device list removal
 * caller must respect a RCU grace period before freeing/reusing dev
 */
static void unlist_netdevice(struct net_device *dev)
{
	struct netdev_name_node *name_node;
	struct net *net = dev_net(dev);

	ASSERT_RTNL();

	xa_erase(&net->dev_by_index, dev->ifindex);

	netdev_for_each_altname(dev, name_node)
		netdev_name_node_del(name_node);

	/* Unlink dev from the device chain */
	list_del_rcu(&dev->dev_list);
	netdev_name_node_del(dev->name_node);
	hlist_del_rcu(&dev->index_hlist);

	dev_base_seq_inc(dev_net(dev));
}

/*
 *	Our notifier list
 */

static RAW_NOTIFIER_HEAD(netdev_chain);

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the local softnet handler.
 */

DEFINE_PER_CPU_ALIGNED(struct softnet_data, softnet_data) = {
	.process_queue_bh_lock = INIT_LOCAL_LOCK(process_queue_bh_lock),
};
EXPORT_PER_CPU_SYMBOL(softnet_data);

/* Page_pool has a lockless array/stack to alloc/recycle pages.
 * PP consumers must pay attention to run APIs in the appropriate context
 * (e.g. NAPI context).
 */
DEFINE_PER_CPU(struct page_pool_bh, system_page_pool) = {
	.bh_lock = INIT_LOCAL_LOCK(bh_lock),
};

#ifdef CONFIG_LOCKDEP
/*
 * register_netdevice() inits txq->_xmit_lock and sets lockdep class
 * according to dev->type
 */
static const unsigned short netdev_lock_type[] = {
	 ARPHRD_NETROM, ARPHRD_ETHER, ARPHRD_EETHER, ARPHRD_AX25,
	 ARPHRD_PRONET, ARPHRD_CHAOS, ARPHRD_IEEE802, ARPHRD_ARCNET,
	 ARPHRD_APPLETLK, ARPHRD_DLCI, ARPHRD_ATM, ARPHRD_METRICOM,
	 ARPHRD_IEEE1394, ARPHRD_EUI64, ARPHRD_INFINIBAND, ARPHRD_SLIP,
	 ARPHRD_CSLIP, ARPHRD_SLIP6, ARPHRD_CSLIP6, ARPHRD_RSRVD,
	 ARPHRD_ADAPT, ARPHRD_ROSE, ARPHRD_X25, ARPHRD_HWX25,
	 ARPHRD_PPP, ARPHRD_CISCO, ARPHRD_LAPB, ARPHRD_DDCMP,
	 ARPHRD_RAWHDLC, ARPHRD_TUNNEL, ARPHRD_TUNNEL6, ARPHRD_FRAD,
	 ARPHRD_SKIP, ARPHRD_LOOPBACK, ARPHRD_LOCALTLK, ARPHRD_FDDI,
	 ARPHRD_BIF, ARPHRD_SIT, ARPHRD_IPDDP, ARPHRD_IPGRE,
	 ARPHRD_PIMREG, ARPHRD_HIPPI, ARPHRD_ASH, ARPHRD_ECONET,
	 ARPHRD_IRDA, ARPHRD_FCPP, ARPHRD_FCAL, ARPHRD_FCPL,
	 ARPHRD_FCFABRIC, ARPHRD_IEEE80211, ARPHRD_IEEE80211_PRISM,
	 ARPHRD_IEEE80211_RADIOTAP, ARPHRD_PHONET, ARPHRD_PHONET_PIPE,
	 ARPHRD_IEEE802154, ARPHRD_VOID, ARPHRD_NONE};

static const char *const netdev_lock_name[] = {
	"_xmit_NETROM", "_xmit_ETHER", "_xmit_EETHER", "_xmit_AX25",
	"_xmit_PRONET", "_xmit_CHAOS", "_xmit_IEEE802", "_xmit_ARCNET",
	"_xmit_APPLETLK", "_xmit_DLCI", "_xmit_ATM", "_xmit_METRICOM",
	"_xmit_IEEE1394", "_xmit_EUI64", "_xmit_INFINIBAND", "_xmit_SLIP",
	"_xmit_CSLIP", "_xmit_SLIP6", "_xmit_CSLIP6", "_xmit_RSRVD",
	"_xmit_ADAPT", "_xmit_ROSE", "_xmit_X25", "_xmit_HWX25",
	"_xmit_PPP", "_xmit_CISCO", "_xmit_LAPB", "_xmit_DDCMP",
	"_xmit_RAWHDLC", "_xmit_TUNNEL", "_xmit_TUNNEL6", "_xmit_FRAD",
	"_xmit_SKIP", "_xmit_LOOPBACK", "_xmit_LOCALTLK", "_xmit_FDDI",
	"_xmit_BIF", "_xmit_SIT", "_xmit_IPDDP", "_xmit_IPGRE",
	"_xmit_PIMREG", "_xmit_HIPPI", "_xmit_ASH", "_xmit_ECONET",
	"_xmit_IRDA", "_xmit_FCPP", "_xmit_FCAL", "_xmit_FCPL",
	"_xmit_FCFABRIC", "_xmit_IEEE80211", "_xmit_IEEE80211_PRISM",
	"_xmit_IEEE80211_RADIOTAP", "_xmit_PHONET", "_xmit_PHONET_PIPE",
	"_xmit_IEEE802154", "_xmit_VOID", "_xmit_NONE"};

static struct lock_class_key netdev_xmit_lock_key[ARRAY_SIZE(netdev_lock_type)];
static struct lock_class_key netdev_addr_lock_key[ARRAY_SIZE(netdev_lock_type)];

static inline unsigned short netdev_lock_pos(unsigned short dev_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(netdev_lock_type); i++)
		if (netdev_lock_type[i] == dev_type)
			return i;
	/* the last key is used by default */
	return ARRAY_SIZE(netdev_lock_type) - 1;
}

static inline void netdev_set_xmit_lockdep_class(spinlock_t *lock,
						 unsigned short dev_type)
{
	int i;

	i = netdev_lock_pos(dev_type);
	lockdep_set_class_and_name(lock, &netdev_xmit_lock_key[i],
				   netdev_lock_name[i]);
}

static inline void netdev_set_addr_lockdep_class(struct net_device *dev)
{
	int i;

	i = netdev_lock_pos(dev->type);
	lockdep_set_class_and_name(&dev->addr_list_lock,
				   &netdev_addr_lock_key[i],
				   netdev_lock_name[i]);
}
#else
static inline void netdev_set_xmit_lockdep_class(spinlock_t *lock,
						 unsigned short dev_type)
{
}

static inline void netdev_set_addr_lockdep_class(struct net_device *dev)
{
}
#endif

/*******************************************************************************
 *
 *		Protocol management and registration routines
 *
 *******************************************************************************/


/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 *
 *	BEWARE!!! Protocol handlers, mangling input packets,
 *	MUST BE last in hash buckets and checking protocol handlers
 *	MUST start from promiscuous ptype_all chain in net_bh.
 *	It is true now, do not change it.
 *	Explanation follows: if protocol handler, mangling packet, will
 *	be the first on list, it is not able to sense, that packet
 *	is cloned and should be copied-on-write, so that it will
 *	change it and subsequent readers will get broken packet.
 *							--ANK (980803)
 */

static inline struct list_head *ptype_head(const struct packet_type *pt)
{
	if (pt->type == htons(ETH_P_ALL)) {
		if (!pt->af_packet_net && !pt->dev)
			return NULL;

		return pt->dev ? &pt->dev->ptype_all :
				 &pt->af_packet_net->ptype_all;
	}

	if (pt->dev)
		return &pt->dev->ptype_specific;

	return pt->af_packet_net ? &pt->af_packet_net->ptype_specific :
				 &ptype_base[ntohs(pt->type) & PTYPE_HASH_MASK];
}

/**
 *	dev_add_pack - add packet handler
 *	@pt: packet type declaration
 *
 *	Add a protocol handler to the networking stack. The passed &packet_type
 *	is linked into kernel lists and may not be freed until it has been
 *	removed from the kernel lists.
 *
 *	This call does not sleep therefore it can not
 *	guarantee all CPU's that are in middle of receiving packets
 *	will see the new packet type (until the next received packet).
 */

void dev_add_pack(struct packet_type *pt)
{
	struct list_head *head = ptype_head(pt);

	if (WARN_ON_ONCE(!head))
		return;

	spin_lock(&ptype_lock);
	list_add_rcu(&pt->list, head);
	spin_unlock(&ptype_lock);
}
EXPORT_SYMBOL(dev_add_pack);

/**
 *	__dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *      The packet type might still be in use by receivers
 *	and must not be freed until after all the CPU's have gone
 *	through a quiescent state.
 */
void __dev_remove_pack(struct packet_type *pt)
{
	struct list_head *head = ptype_head(pt);
	struct packet_type *pt1;

	if (!head)
		return;

	spin_lock(&ptype_lock);

	list_for_each_entry(pt1, head, list) {
		if (pt == pt1) {
			list_del_rcu(&pt->list);
			goto out;
		}
	}

	pr_warn("dev_remove_pack: %p not found\n", pt);
out:
	spin_unlock(&ptype_lock);
}
EXPORT_SYMBOL(__dev_remove_pack);

/**
 *	dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *	This call sleeps to guarantee that no CPU is looking at the packet
 *	type after return.
 */
void dev_remove_pack(struct packet_type *pt)
{
	__dev_remove_pack(pt);

	synchronize_net();
}
EXPORT_SYMBOL(dev_remove_pack);


/*******************************************************************************
 *
 *			    Device Interface Subroutines
 *
 *******************************************************************************/

/**
 *	dev_get_iflink	- get 'iflink' value of a interface
 *	@dev: targeted interface
 *
 *	Indicates the ifindex the interface is linked to.
 *	Physical interfaces have the same 'ifindex' and 'iflink' values.
 */

int dev_get_iflink(const struct net_device *dev)
{
	if (dev->netdev_ops && dev->netdev_ops->ndo_get_iflink)
		return dev->netdev_ops->ndo_get_iflink(dev);

	return READ_ONCE(dev->ifindex);
}
EXPORT_SYMBOL(dev_get_iflink);

/**
 *	dev_fill_metadata_dst - Retrieve tunnel egress information.
 *	@dev: targeted interface
 *	@skb: The packet.
 *
 *	For better visibility of tunnel traffic OVS needs to retrieve
 *	egress tunnel information for a packet. Following API allows
 *	user to get this info.
 */
int dev_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb)
{
	struct ip_tunnel_info *info;

	if (!dev->netdev_ops  || !dev->netdev_ops->ndo_fill_metadata_dst)
		return -EINVAL;

	info = skb_tunnel_info_unclone(skb);
	if (!info)
		return -ENOMEM;
	if (unlikely(!(info->mode & IP_TUNNEL_INFO_TX)))
		return -EINVAL;

	return dev->netdev_ops->ndo_fill_metadata_dst(dev, skb);
}
EXPORT_SYMBOL_GPL(dev_fill_metadata_dst);

static struct net_device_path *dev_fwd_path(struct net_device_path_stack *stack)
{
	int k = stack->num_paths++;

	if (WARN_ON_ONCE(k >= NET_DEVICE_PATH_STACK_MAX))
		return NULL;

	return &stack->path[k];
}

int dev_fill_forward_path(const struct net_device *dev, const u8 *daddr,
			  struct net_device_path_stack *stack)
{
	const struct net_device *last_dev;
	struct net_device_path_ctx ctx = {
		.dev	= dev,
	};
	struct net_device_path *path;
	int ret = 0;

	memcpy(ctx.daddr, daddr, sizeof(ctx.daddr));
	stack->num_paths = 0;
	while (ctx.dev && ctx.dev->netdev_ops->ndo_fill_forward_path) {
		last_dev = ctx.dev;
		path = dev_fwd_path(stack);
		if (!path)
			return -1;

		memset(path, 0, sizeof(struct net_device_path));
		ret = ctx.dev->netdev_ops->ndo_fill_forward_path(&ctx, path);
		if (ret < 0)
			return -1;

		if (WARN_ON_ONCE(last_dev == ctx.dev))
			return -1;
	}

	if (!ctx.dev)
		return ret;

	path = dev_fwd_path(stack);
	if (!path)
		return -1;
	path->type = DEV_PATH_ETHERNET;
	path->dev = ctx.dev;

	return ret;
}
EXPORT_SYMBOL_GPL(dev_fill_forward_path);

/* must be called under rcu_read_lock(), as we dont take a reference */
static struct napi_struct *napi_by_id(unsigned int napi_id)
{
	unsigned int hash = napi_id % HASH_SIZE(napi_hash);
	struct napi_struct *napi;

	hlist_for_each_entry_rcu(napi, &napi_hash[hash], napi_hash_node)
		if (napi->napi_id == napi_id)
			return napi;

	return NULL;
}

/* must be called under rcu_read_lock(), as we dont take a reference */
static struct napi_struct *
netdev_napi_by_id(struct net *net, unsigned int napi_id)
{
	struct napi_struct *napi;

	napi = napi_by_id(napi_id);
	if (!napi)
		return NULL;

	if (WARN_ON_ONCE(!napi->dev))
		return NULL;
	if (!net_eq(net, dev_net(napi->dev)))
		return NULL;

	return napi;
}

/**
 *	netdev_napi_by_id_lock() - find a device by NAPI ID and lock it
 *	@net: the applicable net namespace
 *	@napi_id: ID of a NAPI of a target device
 *
 *	Find a NAPI instance with @napi_id. Lock its device.
 *	The device must be in %NETREG_REGISTERED state for lookup to succeed.
 *	netdev_unlock() must be called to release it.
 *
 *	Return: pointer to NAPI, its device with lock held, NULL if not found.
 */
struct napi_struct *
netdev_napi_by_id_lock(struct net *net, unsigned int napi_id)
{
	struct napi_struct *napi;
	struct net_device *dev;

	rcu_read_lock();
	napi = netdev_napi_by_id(net, napi_id);
	if (!napi || READ_ONCE(napi->dev->reg_state) != NETREG_REGISTERED) {
		rcu_read_unlock();
		return NULL;
	}

	dev = napi->dev;
	dev_hold(dev);
	rcu_read_unlock();

	dev = __netdev_put_lock(dev, net);
	if (!dev)
		return NULL;

	rcu_read_lock();
	napi = netdev_napi_by_id(net, napi_id);
	if (napi && napi->dev != dev)
		napi = NULL;
	rcu_read_unlock();

	if (!napi)
		netdev_unlock(dev);
	return napi;
}

/**
 *	__dev_get_by_name	- find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *
 *	Find an interface by name. Must be called under RTNL semaphore.
 *	If the name is found a pointer to the device is returned.
 *	If the name is not found then %NULL is returned. The
 *	reference counters are not incremented so the caller must be
 *	careful with locks.
 */

struct net_device *__dev_get_by_name(struct net *net, const char *name)
{
	struct netdev_name_node *node_name;

	node_name = netdev_name_node_lookup(net, name);
	return node_name ? node_name->dev : NULL;
}
EXPORT_SYMBOL(__dev_get_by_name);

/**
 * dev_get_by_name_rcu	- find a device by its name
 * @net: the applicable net namespace
 * @name: name to find
 *
 * Find an interface by name.
 * If the name is found a pointer to the device is returned.
 * If the name is not found then %NULL is returned.
 * The reference counters are not incremented so the caller must be
 * careful with locks. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_name_rcu(struct net *net, const char *name)
{
	struct netdev_name_node *node_name;

	node_name = netdev_name_node_lookup_rcu(net, name);
	return node_name ? node_name->dev : NULL;
}
EXPORT_SYMBOL(dev_get_by_name_rcu);

/* Deprecated for new users, call netdev_get_by_name() instead */
struct net_device *dev_get_by_name(struct net *net, const char *name)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(net, name);
	dev_hold(dev);
	rcu_read_unlock();
	return dev;
}
EXPORT_SYMBOL(dev_get_by_name);

/**
 *	netdev_get_by_name() - find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *	@tracker: tracking object for the acquired reference
 *	@gfp: allocation flags for the tracker
 *
 *	Find an interface by name. This can be called from any
 *	context and does its own locking. The returned handle has
 *	the usage count incremented and the caller must use netdev_put() to
 *	release it when it is no longer needed. %NULL is returned if no
 *	matching device is found.
 */
struct net_device *netdev_get_by_name(struct net *net, const char *name,
				      netdevice_tracker *tracker, gfp_t gfp)
{
	struct net_device *dev;

	dev = dev_get_by_name(net, name);
	if (dev)
		netdev_tracker_alloc(dev, tracker, gfp);
	return dev;
}
EXPORT_SYMBOL(netdev_get_by_name);

/**
 *	__dev_get_by_index - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold the RTNL semaphore.
 */

struct net_device *__dev_get_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;
	struct hlist_head *head = dev_index_hash(net, ifindex);

	hlist_for_each_entry(dev, head, index_hlist)
		if (dev->ifindex == ifindex)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(__dev_get_by_index);

/**
 *	dev_get_by_index_rcu - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_index_rcu(struct net *net, int ifindex)
{
	struct net_device *dev;
	struct hlist_head *head = dev_index_hash(net, ifindex);

	hlist_for_each_entry_rcu(dev, head, index_hlist)
		if (dev->ifindex == ifindex)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_get_by_index_rcu);

/* Deprecated for new users, call netdev_get_by_index() instead */
struct net_device *dev_get_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, ifindex);
	dev_hold(dev);
	rcu_read_unlock();
	return dev;
}
EXPORT_SYMBOL(dev_get_by_index);

/**
 *	netdev_get_by_index() - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *	@tracker: tracking object for the acquired reference
 *	@gfp: allocation flags for the tracker
 *
 *	Search for an interface by index. Returns NULL if the device
 *	is not found or a pointer to the device. The device returned has
 *	had a reference added and the pointer is safe until the user calls
 *	netdev_put() to indicate they have finished with it.
 */
struct net_device *netdev_get_by_index(struct net *net, int ifindex,
				       netdevice_tracker *tracker, gfp_t gfp)
{
	struct net_device *dev;

	dev = dev_get_by_index(net, ifindex);
	if (dev)
		netdev_tracker_alloc(dev, tracker, gfp);
	return dev;
}
EXPORT_SYMBOL(netdev_get_by_index);

/**
 *	dev_get_by_napi_id - find a device by napi_id
 *	@napi_id: ID of the NAPI struct
 *
 *	Search for an interface by NAPI ID. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not had
 *	its reference counter increased so the caller must be careful
 *	about locking. The caller must hold RCU lock.
 */
struct net_device *dev_get_by_napi_id(unsigned int napi_id)
{
	struct napi_struct *napi;

	WARN_ON_ONCE(!rcu_read_lock_held());

	if (!napi_id_valid(napi_id))
		return NULL;

	napi = napi_by_id(napi_id);

	return napi ? napi->dev : NULL;
}

/* Release the held reference on the net_device, and if the net_device
 * is still registered try to lock the instance lock. If device is being
 * unregistered NULL will be returned (but the reference has been released,
 * either way!)
 *
 * This helper is intended for locking net_device after it has been looked up
 * using a lockless lookup helper. Lock prevents the instance from going away.
 */
struct net_device *__netdev_put_lock(struct net_device *dev, struct net *net)
{
	netdev_lock(dev);
	if (dev->reg_state > NETREG_REGISTERED ||
	    dev->moving_ns || !net_eq(dev_net(dev), net)) {
		netdev_unlock(dev);
		dev_put(dev);
		return NULL;
	}
	dev_put(dev);
	return dev;
}

static struct net_device *
__netdev_put_lock_ops_compat(struct net_device *dev, struct net *net)
{
	netdev_lock_ops_compat(dev);
	if (dev->reg_state > NETREG_REGISTERED ||
	    dev->moving_ns || !net_eq(dev_net(dev), net)) {
		netdev_unlock_ops_compat(dev);
		dev_put(dev);
		return NULL;
	}
	dev_put(dev);
	return dev;
}

/**
 *	netdev_get_by_index_lock() - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. If a valid device
 *	with @ifindex is found it will be returned with netdev->lock held.
 *	netdev_unlock() must be called to release it.
 *
 *	Return: pointer to a device with lock held, NULL if not found.
 */
struct net_device *netdev_get_by_index_lock(struct net *net, int ifindex)
{
	struct net_device *dev;

	dev = dev_get_by_index(net, ifindex);
	if (!dev)
		return NULL;

	return __netdev_put_lock(dev, net);
}

struct net_device *
netdev_get_by_index_lock_ops_compat(struct net *net, int ifindex)
{
	struct net_device *dev;

	dev = dev_get_by_index(net, ifindex);
	if (!dev)
		return NULL;

	return __netdev_put_lock_ops_compat(dev, net);
}

struct net_device *
netdev_xa_find_lock(struct net *net, struct net_device *dev,
		    unsigned long *index)
{
	if (dev)
		netdev_unlock(dev);

	do {
		rcu_read_lock();
		dev = xa_find(&net->dev_by_index, index, ULONG_MAX, XA_PRESENT);
		if (!dev) {
			rcu_read_unlock();
			return NULL;
		}
		dev_hold(dev);
		rcu_read_unlock();

		dev = __netdev_put_lock(dev, net);
		if (dev)
			return dev;

		(*index)++;
	} while (true);
}

struct net_device *
netdev_xa_find_lock_ops_compat(struct net *net, struct net_device *dev,
			       unsigned long *index)
{
	if (dev)
		netdev_unlock_ops_compat(dev);

	do {
		rcu_read_lock();
		dev = xa_find(&net->dev_by_index, index, ULONG_MAX, XA_PRESENT);
		if (!dev) {
			rcu_read_unlock();
			return NULL;
		}
		dev_hold(dev);
		rcu_read_unlock();

		dev = __netdev_put_lock_ops_compat(dev, net);
		if (dev)
			return dev;

		(*index)++;
	} while (true);
}

static DEFINE_SEQLOCK(netdev_rename_lock);

void netdev_copy_name(struct net_device *dev, char *name)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&netdev_rename_lock);
		strscpy(name, dev->name, IFNAMSIZ);
	} while (read_seqretry(&netdev_rename_lock, seq));
}

/**
 *	netdev_get_name - get a netdevice name, knowing its ifindex.
 *	@net: network namespace
 *	@name: a pointer to the buffer where the name will be stored.
 *	@ifindex: the ifindex of the interface to get the name from.
 */
int netdev_get_name(struct net *net, char *name, int ifindex)
{
	struct net_device *dev;
	int ret;

	rcu_read_lock();

	dev = dev_get_by_index_rcu(net, ifindex);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	netdev_copy_name(dev, name);

	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

static bool dev_addr_cmp(struct net_device *dev, unsigned short type,
			 const char *ha)
{
	return dev->type == type && !memcmp(dev->dev_addr, ha, dev->addr_len);
}

/**
 *	dev_getbyhwaddr_rcu - find a device by its hardware address
 *	@net: the applicable net namespace
 *	@type: media type of device
 *	@ha: hardware address
 *
 *	Search for an interface by MAC address. Returns NULL if the device
 *	is not found or a pointer to the device.
 *	The caller must hold RCU.
 *	The returned device has not had its ref count increased
 *	and the caller must therefore be careful about locking
 *
 */

struct net_device *dev_getbyhwaddr_rcu(struct net *net, unsigned short type,
				       const char *ha)
{
	struct net_device *dev;

	for_each_netdev_rcu(net, dev)
		if (dev_addr_cmp(dev, type, ha))
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_getbyhwaddr_rcu);

/**
 * dev_getbyhwaddr() - find a device by its hardware address
 * @net: the applicable net namespace
 * @type: media type of device
 * @ha: hardware address
 *
 * Similar to dev_getbyhwaddr_rcu(), but the owner needs to hold
 * rtnl_lock.
 *
 * Context: rtnl_lock() must be held.
 * Return: pointer to the net_device, or NULL if not found
 */
struct net_device *dev_getbyhwaddr(struct net *net, unsigned short type,
				   const char *ha)
{
	struct net_device *dev;

	ASSERT_RTNL();
	for_each_netdev(net, dev)
		if (dev_addr_cmp(dev, type, ha))
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_getbyhwaddr);

struct net_device *dev_getfirstbyhwtype(struct net *net, unsigned short type)
{
	struct net_device *dev, *ret = NULL;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev)
		if (dev->type == type) {
			dev_hold(dev);
			ret = dev;
			break;
		}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(dev_getfirstbyhwtype);

/**
 * netdev_get_by_flags_rcu - find any device with given flags
 * @net: the applicable net namespace
 * @tracker: tracking object for the acquired reference
 * @if_flags: IFF_* values
 * @mask: bitmask of bits in if_flags to check
 *
 * Search for any interface with the given flags.
 *
 * Context: rcu_read_lock() must be held.
 * Returns: NULL if a device is not found or a pointer to the device.
 */
struct net_device *netdev_get_by_flags_rcu(struct net *net, netdevice_tracker *tracker,
					   unsigned short if_flags, unsigned short mask)
{
	struct net_device *dev;

	for_each_netdev_rcu(net, dev) {
		if (((READ_ONCE(dev->flags) ^ if_flags) & mask) == 0) {
			netdev_hold(dev, tracker, GFP_ATOMIC);
			return dev;
		}
	}

	return NULL;
}
EXPORT_IPV6_MOD(netdev_get_by_flags_rcu);

/**
 *	dev_valid_name - check if name is okay for network device
 *	@name: name string
 *
 *	Network device names need to be valid file names to
 *	allow sysfs to work.  We also disallow any kind of
 *	whitespace.
 */
bool dev_valid_name(const char *name)
{
	if (*name == '\0')
		return false;
	if (strnlen(name, IFNAMSIZ) == IFNAMSIZ)
		return false;
	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return false;

	while (*name) {
		if (*name == '/' || *name == ':' || isspace(*name))
			return false;
		name++;
	}
	return true;
}
EXPORT_SYMBOL(dev_valid_name);

/**
 *	__dev_alloc_name - allocate a name for a device
 *	@net: network namespace to allocate the device name in
 *	@name: name format string
 *	@res: result name string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. It scans list of devices to build up a free map, then chooses
 *	the first empty slot. The caller must hold the dev_base or rtnl lock
 *	while allocating the name and adding the device in order to avoid
 *	duplicates.
 *	Limited to bits_per_byte * page size devices (ie 32K on most platforms).
 *	Returns the number of the unit assigned or a negative errno code.
 */

static int __dev_alloc_name(struct net *net, const char *name, char *res)
{
	int i = 0;
	const char *p;
	const int max_netdevices = 8*PAGE_SIZE;
	unsigned long *inuse;
	struct net_device *d;
	char buf[IFNAMSIZ];

	/* Verify the string as this thing may have come from the user.
	 * There must be one "%d" and no other "%" characters.
	 */
	p = strchr(name, '%');
	if (!p || p[1] != 'd' || strchr(p + 2, '%'))
		return -EINVAL;

	/* Use one page as a bit array of possible slots */
	inuse = bitmap_zalloc(max_netdevices, GFP_ATOMIC);
	if (!inuse)
		return -ENOMEM;

	for_each_netdev(net, d) {
		struct netdev_name_node *name_node;

		netdev_for_each_altname(d, name_node) {
			if (!sscanf(name_node->name, name, &i))
				continue;
			if (i < 0 || i >= max_netdevices)
				continue;

			/* avoid cases where sscanf is not exact inverse of printf */
			snprintf(buf, IFNAMSIZ, name, i);
			if (!strncmp(buf, name_node->name, IFNAMSIZ))
				__set_bit(i, inuse);
		}
		if (!sscanf(d->name, name, &i))
			continue;
		if (i < 0 || i >= max_netdevices)
			continue;

		/* avoid cases where sscanf is not exact inverse of printf */
		snprintf(buf, IFNAMSIZ, name, i);
		if (!strncmp(buf, d->name, IFNAMSIZ))
			__set_bit(i, inuse);
	}

	i = find_first_zero_bit(inuse, max_netdevices);
	bitmap_free(inuse);
	if (i == max_netdevices)
		return -ENFILE;

	/* 'res' and 'name' could overlap, use 'buf' as an intermediate buffer */
	strscpy(buf, name, IFNAMSIZ);
	snprintf(res, IFNAMSIZ, buf, i);
	return i;
}

/* Returns negative errno or allocated unit id (see __dev_alloc_name()) */
static int dev_prep_valid_name(struct net *net, struct net_device *dev,
			       const char *want_name, char *out_name,
			       int dup_errno)
{
	if (!dev_valid_name(want_name))
		return -EINVAL;

	if (strchr(want_name, '%'))
		return __dev_alloc_name(net, want_name, out_name);

	if (netdev_name_in_use(net, want_name))
		return -dup_errno;
	if (out_name != want_name)
		strscpy(out_name, want_name, IFNAMSIZ);
	return 0;
}

/**
 *	dev_alloc_name - allocate a name for a device
 *	@dev: device
 *	@name: name format string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. It scans list of devices to build up a free map, then chooses
 *	the first empty slot. The caller must hold the dev_base or rtnl lock
 *	while allocating the name and adding the device in order to avoid
 *	duplicates.
 *	Limited to bits_per_byte * page size devices (ie 32K on most platforms).
 *	Returns the number of the unit assigned or a negative errno code.
 */

int dev_alloc_name(struct net_device *dev, const char *name)
{
	return dev_prep_valid_name(dev_net(dev), dev, name, dev->name, ENFILE);
}
EXPORT_SYMBOL(dev_alloc_name);

static int dev_get_valid_name(struct net *net, struct net_device *dev,
			      const char *name)
{
	int ret;

	ret = dev_prep_valid_name(net, dev, name, dev->name, EEXIST);
	return ret < 0 ? ret : 0;
}

int netif_change_name(struct net_device *dev, const char *newname)
{
	struct net *net = dev_net(dev);
	unsigned char old_assign_type;
	char oldname[IFNAMSIZ];
	int err = 0;
	int ret;

	ASSERT_RTNL_NET(net);

	if (!strncmp(newname, dev->name, IFNAMSIZ))
		return 0;

	memcpy(oldname, dev->name, IFNAMSIZ);

	write_seqlock_bh(&netdev_rename_lock);
	err = dev_get_valid_name(net, dev, newname);
	write_sequnlock_bh(&netdev_rename_lock);

	if (err < 0)
		return err;

	if (oldname[0] && !strchr(oldname, '%'))
		netdev_info(dev, "renamed from %s%s\n", oldname,
			    dev->flags & IFF_UP ? " (while UP)" : "");

	old_assign_type = dev->name_assign_type;
	WRITE_ONCE(dev->name_assign_type, NET_NAME_RENAMED);

rollback:
	ret = device_rename(&dev->dev, dev->name);
	if (ret) {
		write_seqlock_bh(&netdev_rename_lock);
		memcpy(dev->name, oldname, IFNAMSIZ);
		write_sequnlock_bh(&netdev_rename_lock);
		WRITE_ONCE(dev->name_assign_type, old_assign_type);
		return ret;
	}

	netdev_adjacent_rename_links(dev, oldname);

	netdev_name_node_del(dev->name_node);

	synchronize_net();

	netdev_name_node_add(net, dev->name_node);

	ret = call_netdevice_notifiers(NETDEV_CHANGENAME, dev);
	ret = notifier_to_errno(ret);

	if (ret) {
		/* err >= 0 after dev_alloc_name() or stores the first errno */
		if (err >= 0) {
			err = ret;
			write_seqlock_bh(&netdev_rename_lock);
			memcpy(dev->name, oldname, IFNAMSIZ);
			write_sequnlock_bh(&netdev_rename_lock);
			memcpy(oldname, newname, IFNAMSIZ);
			WRITE_ONCE(dev->name_assign_type, old_assign_type);
			old_assign_type = NET_NAME_RENAMED;
			goto rollback;
		} else {
			netdev_err(dev, "name change rollback failed: %d\n",
				   ret);
		}
	}

	return err;
}

int netif_set_alias(struct net_device *dev, const char *alias, size_t len)
{
	struct dev_ifalias *new_alias = NULL;

	if (len >= IFALIASZ)
		return -EINVAL;

	if (len) {
		new_alias = kmalloc(sizeof(*new_alias) + len + 1, GFP_KERNEL);
		if (!new_alias)
			return -ENOMEM;

		memcpy(new_alias->ifalias, alias, len);
		new_alias->ifalias[len] = 0;
	}

	mutex_lock(&ifalias_mutex);
	new_alias = rcu_replace_pointer(dev->ifalias, new_alias,
					mutex_is_locked(&ifalias_mutex));
	mutex_unlock(&ifalias_mutex);

	if (new_alias)
		kfree_rcu(new_alias, rcuhead);

	return len;
}

/**
 *	dev_get_alias - get ifalias of a device
 *	@dev: device
 *	@name: buffer to store name of ifalias
 *	@len: size of buffer
 *
 *	get ifalias for a device.  Caller must make sure dev cannot go
 *	away,  e.g. rcu read lock or own a reference count to device.
 */
int dev_get_alias(const struct net_device *dev, char *name, size_t len)
{
	const struct dev_ifalias *alias;
	int ret = 0;

	rcu_read_lock();
	alias = rcu_dereference(dev->ifalias);
	if (alias)
		ret = snprintf(name, len, "%s", alias->ifalias);
	rcu_read_unlock();

	return ret;
}

/**
 *	netdev_features_change - device changes features
 *	@dev: device to cause notification
 *
 *	Called to indicate a device has changed features.
 */
void netdev_features_change(struct net_device *dev)
{
	call_netdevice_notifiers(NETDEV_FEAT_CHANGE, dev);
}
EXPORT_SYMBOL(netdev_features_change);

void netif_state_change(struct net_device *dev)
{
	netdev_ops_assert_locked_or_invisible(dev);

	if (dev->flags & IFF_UP) {
		struct netdev_notifier_change_info change_info = {
			.info.dev = dev,
		};

		call_netdevice_notifiers_info(NETDEV_CHANGE,
					      &change_info.info);
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0, GFP_KERNEL, 0, NULL);
	}
}

/**
 * __netdev_notify_peers - notify network peers about existence of @dev,
 * to be called when rtnl lock is already held.
 * @dev: network device
 *
 * Generate traffic such that interested network peers are aware of
 * @dev, such as by generating a gratuitous ARP. This may be used when
 * a device wants to inform the rest of the network about some sort of
 * reconfiguration such as a failover event or virtual machine
 * migration.
 */
void __netdev_notify_peers(struct net_device *dev)
{
	ASSERT_RTNL();
	call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, dev);
	call_netdevice_notifiers(NETDEV_RESEND_IGMP, dev);
}
EXPORT_SYMBOL(__netdev_notify_peers);

/**
 * netdev_notify_peers - notify network peers about existence of @dev
 * @dev: network device
 *
 * Generate traffic such that interested network peers are aware of
 * @dev, such as by generating a gratuitous ARP. This may be used when
 * a device wants to inform the rest of the network about some sort of
 * reconfiguration such as a failover event or virtual machine
 * migration.
 */
void netdev_notify_peers(struct net_device *dev)
{
	rtnl_lock();
	__netdev_notify_peers(dev);
	rtnl_unlock();
}
EXPORT_SYMBOL(netdev_notify_peers);

static int napi_threaded_poll(void *data);

static int napi_kthread_create(struct napi_struct *n)
{
	int err = 0;

	/* Create and wake up the kthread once to put it in
	 * TASK_INTERRUPTIBLE mode to avoid the blocked task
	 * warning and work with loadavg.
	 */
	n->thread = kthread_run(napi_threaded_poll, n, "napi/%s-%d",
				n->dev->name, n->napi_id);
	if (IS_ERR(n->thread)) {
		err = PTR_ERR(n->thread);
		pr_err("kthread_run failed with err %d\n", err);
		n->thread = NULL;
	}

	return err;
}

static int __dev_open(struct net_device *dev, struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int ret;

	ASSERT_RTNL();
	dev_addr_check(dev);

	if (!netif_device_present(dev)) {
		/* may be detached because parent is runtime-suspended */
		if (dev->dev.parent)
			pm_runtime_resume(dev->dev.parent);
		if (!netif_device_present(dev))
			return -ENODEV;
	}

	/* Block netpoll from trying to do any rx path servicing.
	 * If we don't do this there is a chance ndo_poll_controller
	 * or ndo_poll may be running while we open the device
	 */
	netpoll_poll_disable(dev);

	ret = call_netdevice_notifiers_extack(NETDEV_PRE_UP, dev, extack);
	ret = notifier_to_errno(ret);
	if (ret)
		return ret;

	set_bit(__LINK_STATE_START, &dev->state);

	netdev_ops_assert_locked(dev);

	if (ops->ndo_validate_addr)
		ret = ops->ndo_validate_addr(dev);

	if (!ret && ops->ndo_open)
		ret = ops->ndo_open(dev);

	netpoll_poll_enable(dev);

	if (ret)
		clear_bit(__LINK_STATE_START, &dev->state);
	else {
		netif_set_up(dev, true);
		dev_set_rx_mode(dev);
		dev_activate(dev);
		add_device_randomness(dev->dev_addr, dev->addr_len);
	}

	return ret;
}

int netif_open(struct net_device *dev, struct netlink_ext_ack *extack)
{
	int ret;

	if (dev->flags & IFF_UP)
		return 0;

	ret = __dev_open(dev, extack);
	if (ret < 0)
		return ret;

	rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP | IFF_RUNNING, GFP_KERNEL, 0, NULL);
	call_netdevice_notifiers(NETDEV_UP, dev);

	return ret;
}

static void __dev_close_many(struct list_head *head)
{
	struct net_device *dev;

	ASSERT_RTNL();
	might_sleep();

	list_for_each_entry(dev, head, close_list) {
		/* Temporarily disable netpoll until the interface is down */
		netpoll_poll_disable(dev);

		call_netdevice_notifiers(NETDEV_GOING_DOWN, dev);

		clear_bit(__LINK_STATE_START, &dev->state);

		/* Synchronize to scheduled poll. We cannot touch poll list, it
		 * can be even on different cpu. So just clear netif_running().
		 *
		 * dev->stop() will invoke napi_disable() on all of it's
		 * napi_struct instances on this device.
		 */
		smp_mb__after_atomic(); /* Commit netif_running(). */
	}

	dev_deactivate_many(head);

	list_for_each_entry(dev, head, close_list) {
		const struct net_device_ops *ops = dev->netdev_ops;

		/*
		 *	Call the device specific close. This cannot fail.
		 *	Only if device is UP
		 *
		 *	We allow it to be called even after a DETACH hot-plug
		 *	event.
		 */

		netdev_ops_assert_locked(dev);

		if (ops->ndo_stop)
			ops->ndo_stop(dev);

		netif_set_up(dev, false);
		netpoll_poll_enable(dev);
	}
}

static void __dev_close(struct net_device *dev)
{
	LIST_HEAD(single);

	list_add(&dev->close_list, &single);
	__dev_close_many(&single);
	list_del(&single);
}

void netif_close_many(struct list_head *head, bool unlink)
{
	struct net_device *dev, *tmp;

	/* Remove the devices that don't need to be closed */
	list_for_each_entry_safe(dev, tmp, head, close_list)
		if (!(dev->flags & IFF_UP))
			list_del_init(&dev->close_list);

	__dev_close_many(head);

	list_for_each_entry_safe(dev, tmp, head, close_list) {
		rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP | IFF_RUNNING, GFP_KERNEL, 0, NULL);
		call_netdevice_notifiers(NETDEV_DOWN, dev);
		if (unlink)
			list_del_init(&dev->close_list);
	}
}
EXPORT_SYMBOL_NS_GPL(netif_close_many, "NETDEV_INTERNAL");

void netif_close(struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		LIST_HEAD(single);

		list_add(&dev->close_list, &single);
		netif_close_many(&single, true);
		list_del(&single);
	}
}
EXPORT_SYMBOL(netif_close);

void netif_disable_lro(struct net_device *dev)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	dev->wanted_features &= ~NETIF_F_LRO;
	netdev_update_features(dev);

	if (unlikely(dev->features & NETIF_F_LRO))
		netdev_WARN(dev, "failed to disable LRO!\n");

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		netdev_lock_ops(lower_dev);
		netif_disable_lro(lower_dev);
		netdev_unlock_ops(lower_dev);
	}
}
EXPORT_IPV6_MOD(netif_disable_lro);

/**
 *	dev_disable_gro_hw - disable HW Generic Receive Offload on a device
 *	@dev: device
 *
 *	Disable HW Generic Receive Offload (GRO_HW) on a net device.  Must be
 *	called under RTNL.  This is needed if Generic XDP is installed on
 *	the device.
 */
static void dev_disable_gro_hw(struct net_device *dev)
{
	dev->wanted_features &= ~NETIF_F_GRO_HW;
	netdev_update_features(dev);

	if (unlikely(dev->features & NETIF_F_GRO_HW))
		netdev_WARN(dev, "failed to disable GRO_HW!\n");
}

const char *netdev_cmd_to_name(enum netdev_cmd cmd)
{
#define N(val) 						\
	case NETDEV_##val:				\
		return "NETDEV_" __stringify(val);
	switch (cmd) {
	N(UP) N(DOWN) N(REBOOT) N(CHANGE) N(REGISTER) N(UNREGISTER)
	N(CHANGEMTU) N(CHANGEADDR) N(GOING_DOWN) N(CHANGENAME) N(FEAT_CHANGE)
	N(BONDING_FAILOVER) N(PRE_UP) N(PRE_TYPE_CHANGE) N(POST_TYPE_CHANGE)
	N(POST_INIT) N(PRE_UNINIT) N(RELEASE) N(NOTIFY_PEERS) N(JOIN)
	N(CHANGEUPPER) N(RESEND_IGMP) N(PRECHANGEMTU) N(CHANGEINFODATA)
	N(BONDING_INFO) N(PRECHANGEUPPER) N(CHANGELOWERSTATE)
	N(UDP_TUNNEL_PUSH_INFO) N(UDP_TUNNEL_DROP_INFO) N(CHANGE_TX_QUEUE_LEN)
	N(CVLAN_FILTER_PUSH_INFO) N(CVLAN_FILTER_DROP_INFO)
	N(SVLAN_FILTER_PUSH_INFO) N(SVLAN_FILTER_DROP_INFO)
	N(PRE_CHANGEADDR) N(OFFLOAD_XSTATS_ENABLE) N(OFFLOAD_XSTATS_DISABLE)
	N(OFFLOAD_XSTATS_REPORT_USED) N(OFFLOAD_XSTATS_REPORT_DELTA)
	N(XDP_FEAT_CHANGE)
	}
#undef N
	return "UNKNOWN_NETDEV_EVENT";
}
EXPORT_SYMBOL_GPL(netdev_cmd_to_name);

static int call_netdevice_notifier(struct notifier_block *nb, unsigned long val,
				   struct net_device *dev)
{
	struct netdev_notifier_info info = {
		.dev = dev,
	};

	return nb->notifier_call(nb, val, &info);
}

static int call_netdevice_register_notifiers(struct notifier_block *nb,
					     struct net_device *dev)
{
	int err;

	err = call_netdevice_notifier(nb, NETDEV_REGISTER, dev);
	err = notifier_to_errno(err);
	if (err)
		return err;

	if (!(dev->flags & IFF_UP))
		return 0;

	call_netdevice_notifier(nb, NETDEV_UP, dev);
	return 0;
}

static void call_netdevice_unregister_notifiers(struct notifier_block *nb,
						struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		call_netdevice_notifier(nb, NETDEV_GOING_DOWN,
					dev);
		call_netdevice_notifier(nb, NETDEV_DOWN, dev);
	}
	call_netdevice_notifier(nb, NETDEV_UNREGISTER, dev);
}

static int call_netdevice_register_net_notifiers(struct notifier_block *nb,
						 struct net *net)
{
	struct net_device *dev;
	int err;

	for_each_netdev(net, dev) {
		netdev_lock_ops(dev);
		err = call_netdevice_register_notifiers(nb, dev);
		netdev_unlock_ops(dev);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	for_each_netdev_continue_reverse(net, dev)
		call_netdevice_unregister_notifiers(nb, dev);
	return err;
}

static void call_netdevice_unregister_net_notifiers(struct notifier_block *nb,
						    struct net *net)
{
	struct net_device *dev;

	for_each_netdev(net, dev)
		call_netdevice_unregister_notifiers(nb, dev);
}

static int dev_boot_phase = 1;

/**
 * register_netdevice_notifier - register a network notifier block
 * @nb: notifier
 *
 * Register a notifier to be called when network device events occur.
 * The notifier passed is linked into the kernel structures and must
 * not be reused until it has been unregistered. A negative errno code
 * is returned on a failure.
 *
 * When registered all registration and up events are replayed
 * to the new notifier to allow device to have a race free
 * view of the network device list.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	struct net *net;
	int err;

	/* Close race with setup_net() and cleanup_net() */
	down_write(&pernet_ops_rwsem);

	/* When RTNL is removed, we need protection for netdev_chain. */
	rtnl_lock();

	err = raw_notifier_chain_register(&netdev_chain, nb);
	if (err)
		goto unlock;
	if (dev_boot_phase)
		goto unlock;
	for_each_net(net) {
		__rtnl_net_lock(net);
		err = call_netdevice_register_net_notifiers(nb, net);
		__rtnl_net_unlock(net);
		if (err)
			goto rollback;
	}

unlock:
	rtnl_unlock();
	up_write(&pernet_ops_rwsem);
	return err;

rollback:
	for_each_net_continue_reverse(net) {
		__rtnl_net_lock(net);
		call_netdevice_unregister_net_notifiers(nb, net);
		__rtnl_net_unlock(net);
	}

	raw_notifier_chain_unregister(&netdev_chain, nb);
	goto unlock;
}
EXPORT_SYMBOL(register_netdevice_notifier);

/**
 * unregister_netdevice_notifier - unregister a network notifier block
 * @nb: notifier
 *
 * Unregister a notifier previously registered by
 * register_netdevice_notifier(). The notifier is unlinked into the
 * kernel structures and may then be reused. A negative errno code
 * is returned on a failure.
 *
 * After unregistering unregister and down device events are synthesized
 * for all devices on the device list to the removed notifier to remove
 * the need for special case cleanup code.
 */

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	struct net *net;
	int err;

	/* Close race with setup_net() and cleanup_net() */
	down_write(&pernet_ops_rwsem);
	rtnl_lock();
	err = raw_notifier_chain_unregister(&netdev_chain, nb);
	if (err)
		goto unlock;

	for_each_net(net) {
		__rtnl_net_lock(net);
		call_netdevice_unregister_net_notifiers(nb, net);
		__rtnl_net_unlock(net);
	}

unlock:
	rtnl_unlock();
	up_write(&pernet_ops_rwsem);
	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier);

static int __register_netdevice_notifier_net(struct net *net,
					     struct notifier_block *nb,
					     bool ignore_call_fail)
{
	int err;

	err = raw_notifier_chain_register(&net->netdev_chain, nb);
	if (err)
		return err;
	if (dev_boot_phase)
		return 0;

	err = call_netdevice_register_net_notifiers(nb, net);
	if (err && !ignore_call_fail)
		goto chain_unregister;

	return 0;

chain_unregister:
	raw_notifier_chain_unregister(&net->netdev_chain, nb);
	return err;
}

static int __unregister_netdevice_notifier_net(struct net *net,
					       struct notifier_block *nb)
{
	int err;

	err = raw_notifier_chain_unregister(&net->netdev_chain, nb);
	if (err)
		return err;

	call_netdevice_unregister_net_notifiers(nb, net);
	return 0;
}

/**
 * register_netdevice_notifier_net - register a per-netns network notifier block
 * @net: network namespace
 * @nb: notifier
 *
 * Register a notifier to be called when network device events occur.
 * The notifier passed is linked into the kernel structures and must
 * not be reused until it has been unregistered. A negative errno code
 * is returned on a failure.
 *
 * When registered all registration and up events are replayed
 * to the new notifier to allow device to have a race free
 * view of the network device list.
 */

int register_netdevice_notifier_net(struct net *net, struct notifier_block *nb)
{
	int err;

	rtnl_net_lock(net);
	err = __register_netdevice_notifier_net(net, nb, false);
	rtnl_net_unlock(net);

	return err;
}
EXPORT_SYMBOL(register_netdevice_notifier_net);

/**
 * unregister_netdevice_notifier_net - unregister a per-netns
 *                                     network notifier block
 * @net: network namespace
 * @nb: notifier
 *
 * Unregister a notifier previously registered by
 * register_netdevice_notifier_net(). The notifier is unlinked from the
 * kernel structures and may then be reused. A negative errno code
 * is returned on a failure.
 *
 * After unregistering unregister and down device events are synthesized
 * for all devices on the device list to the removed notifier to remove
 * the need for special case cleanup code.
 */

int unregister_netdevice_notifier_net(struct net *net,
				      struct notifier_block *nb)
{
	int err;

	rtnl_net_lock(net);
	err = __unregister_netdevice_notifier_net(net, nb);
	rtnl_net_unlock(net);

	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier_net);

static void __move_netdevice_notifier_net(struct net *src_net,
					  struct net *dst_net,
					  struct notifier_block *nb)
{
	__unregister_netdevice_notifier_net(src_net, nb);
	__register_netdevice_notifier_net(dst_net, nb, true);
}

static void rtnl_net_dev_lock(struct net_device *dev)
{
	bool again;

	do {
		struct net *net;

		again = false;

		/* netns might be being dismantled. */
		rcu_read_lock();
		net = dev_net_rcu(dev);
		net_passive_inc(net);
		rcu_read_unlock();

		rtnl_net_lock(net);

#ifdef CONFIG_NET_NS
		/* dev might have been moved to another netns. */
		if (!net_eq(net, rcu_access_pointer(dev->nd_net.net))) {
			rtnl_net_unlock(net);
			net_passive_dec(net);
			again = true;
		}
#endif
	} while (again);
}

static void rtnl_net_dev_unlock(struct net_device *dev)
{
	struct net *net = dev_net(dev);

	rtnl_net_unlock(net);
	net_passive_dec(net);
}

int register_netdevice_notifier_dev_net(struct net_device *dev,
					struct notifier_block *nb,
					struct netdev_net_notifier *nn)
{
	int err;

	rtnl_net_dev_lock(dev);
	err = __register_netdevice_notifier_net(dev_net(dev), nb, false);
	if (!err) {
		nn->nb = nb;
		list_add(&nn->list, &dev->net_notifier_list);
	}
	rtnl_net_dev_unlock(dev);

	return err;
}
EXPORT_SYMBOL(register_netdevice_notifier_dev_net);

int unregister_netdevice_notifier_dev_net(struct net_device *dev,
					  struct notifier_block *nb,
					  struct netdev_net_notifier *nn)
{
	int err;

	rtnl_net_dev_lock(dev);
	list_del(&nn->list);
	err = __unregister_netdevice_notifier_net(dev_net(dev), nb);
	rtnl_net_dev_unlock(dev);

	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier_dev_net);

static void move_netdevice_notifiers_dev_net(struct net_device *dev,
					     struct net *net)
{
	struct netdev_net_notifier *nn;

	list_for_each_entry(nn, &dev->net_notifier_list, list)
		__move_netdevice_notifier_net(dev_net(dev), net, nn->nb);
}

/**
 *	call_netdevice_notifiers_info - call all network notifier blocks
 *	@val: value passed unmodified to notifier function
 *	@info: notifier information data
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 */

int call_netdevice_notifiers_info(unsigned long val,
				  struct netdev_notifier_info *info)
{
	struct net *net = dev_net(info->dev);
	int ret;

	ASSERT_RTNL();

	/* Run per-netns notifier block chain first, then run the global one.
	 * Hopefully, one day, the global one is going to be removed after
	 * all notifier block registrators get converted to be per-netns.
	 */
	ret = raw_notifier_call_chain(&net->netdev_chain, val, info);
	if (ret & NOTIFY_STOP_MASK)
		return ret;
	return raw_notifier_call_chain(&netdev_chain, val, info);
}

/**
 *	call_netdevice_notifiers_info_robust - call per-netns notifier blocks
 *	                                       for and rollback on error
 *	@val_up: value passed unmodified to notifier function
 *	@val_down: value passed unmodified to the notifier function when
 *	           recovering from an error on @val_up
 *	@info: notifier information data
 *
 *	Call all per-netns network notifier blocks, but not notifier blocks on
 *	the global notifier chain. Parameters and return value are as for
 *	raw_notifier_call_chain_robust().
 */

static int
call_netdevice_notifiers_info_robust(unsigned long val_up,
				     unsigned long val_down,
				     struct netdev_notifier_info *info)
{
	struct net *net = dev_net(info->dev);

	ASSERT_RTNL();

	return raw_notifier_call_chain_robust(&net->netdev_chain,
					      val_up, val_down, info);
}

static int call_netdevice_notifiers_extack(unsigned long val,
					   struct net_device *dev,
					   struct netlink_ext_ack *extack)
{
	struct netdev_notifier_info info = {
		.dev = dev,
		.extack = extack,
	};

	return call_netdevice_notifiers_info(val, &info);
}

/**
 *	call_netdevice_notifiers - call all network notifier blocks
 *      @val: value passed unmodified to notifier function
 *      @dev: net_device pointer passed unmodified to notifier function
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 */

int call_netdevice_notifiers(unsigned long val, struct net_device *dev)
{
	return call_netdevice_notifiers_extack(val, dev, NULL);
}
EXPORT_SYMBOL(call_netdevice_notifiers);

/**
 *	call_netdevice_notifiers_mtu - call all network notifier blocks
 *	@val: value passed unmodified to notifier function
 *	@dev: net_device pointer passed unmodified to notifier function
 *	@arg: additional u32 argument passed to the notifier function
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 */
static int call_netdevice_notifiers_mtu(unsigned long val,
					struct net_device *dev, u32 arg)
{
	struct netdev_notifier_info_ext info = {
		.info.dev = dev,
		.ext.mtu = arg,
	};

	BUILD_BUG_ON(offsetof(struct netdev_notifier_info_ext, info) != 0);

	return call_netdevice_notifiers_info(val, &info.info);
}

#ifdef CONFIG_NET_INGRESS
static DEFINE_STATIC_KEY_FALSE(ingress_needed_key);

void net_inc_ingress_queue(void)
{
	static_branch_inc(&ingress_needed_key);
}
EXPORT_SYMBOL_GPL(net_inc_ingress_queue);

void net_dec_ingress_queue(void)
{
	static_branch_dec(&ingress_needed_key);
}
EXPORT_SYMBOL_GPL(net_dec_ingress_queue);
#endif

#ifdef CONFIG_NET_EGRESS
static DEFINE_STATIC_KEY_FALSE(egress_needed_key);

void net_inc_egress_queue(void)
{
	static_branch_inc(&egress_needed_key);
}
EXPORT_SYMBOL_GPL(net_inc_egress_queue);

void net_dec_egress_queue(void)
{
	static_branch_dec(&egress_needed_key);
}
EXPORT_SYMBOL_GPL(net_dec_egress_queue);
#endif

#ifdef CONFIG_NET_CLS_ACT
DEFINE_STATIC_KEY_FALSE(tcf_sw_enabled_key);
EXPORT_SYMBOL(tcf_sw_enabled_key);
#endif

DEFINE_STATIC_KEY_FALSE(netstamp_needed_key);
EXPORT_SYMBOL(netstamp_needed_key);
#ifdef CONFIG_JUMP_LABEL
static atomic_t netstamp_needed_deferred;
static atomic_t netstamp_wanted;
static void netstamp_clear(struct work_struct *work)
{
	int deferred = atomic_xchg(&netstamp_needed_deferred, 0);
	int wanted;

	wanted = atomic_add_return(deferred, &netstamp_wanted);
	if (wanted > 0)
		static_branch_enable(&netstamp_needed_key);
	else
		static_branch_disable(&netstamp_needed_key);
}
static DECLARE_WORK(netstamp_work, netstamp_clear);
#endif

void net_enable_timestamp(void)
{
#ifdef CONFIG_JUMP_LABEL
	int wanted = atomic_read(&netstamp_wanted);

	while (wanted > 0) {
		if (atomic_try_cmpxchg(&netstamp_wanted, &wanted, wanted + 1))
			return;
	}
	atomic_inc(&netstamp_needed_deferred);
	schedule_work(&netstamp_work);
#else
	static_branch_inc(&netstamp_needed_key);
#endif
}
EXPORT_SYMBOL(net_enable_timestamp);

void net_disable_timestamp(void)
{
#ifdef CONFIG_JUMP_LABEL
	int wanted = atomic_read(&netstamp_wanted);

	while (wanted > 1) {
		if (atomic_try_cmpxchg(&netstamp_wanted, &wanted, wanted - 1))
			return;
	}
	atomic_dec(&netstamp_needed_deferred);
	schedule_work(&netstamp_work);
#else
	static_branch_dec(&netstamp_needed_key);
#endif
}
EXPORT_SYMBOL(net_disable_timestamp);

static inline void net_timestamp_set(struct sk_buff *skb)
{
	skb->tstamp = 0;
	skb->tstamp_type = SKB_CLOCK_REALTIME;
	if (static_branch_unlikely(&netstamp_needed_key))
		skb->tstamp = ktime_get_real();
}

#define net_timestamp_check(COND, SKB)				\
	if (static_branch_unlikely(&netstamp_needed_key)) {	\
		if ((COND) && !(SKB)->tstamp)			\
			(SKB)->tstamp = ktime_get_real();	\
	}							\

bool is_skb_forwardable(const struct net_device *dev, const struct sk_buff *skb)
{
	return __is_skb_forwardable(dev, skb, true);
}
EXPORT_SYMBOL_GPL(is_skb_forwardable);

static int __dev_forward_skb2(struct net_device *dev, struct sk_buff *skb,
			      bool check_mtu)
{
	int ret = ____dev_forward_skb(dev, skb, check_mtu);

	if (likely(!ret)) {
		skb->protocol = eth_type_trans(skb, dev);
		skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);
	}

	return ret;
}

int __dev_forward_skb(struct net_device *dev, struct sk_buff *skb)
{
	return __dev_forward_skb2(dev, skb, true);
}
EXPORT_SYMBOL_GPL(__dev_forward_skb);

/**
 * dev_forward_skb - loopback an skb to another netif
 *
 * @dev: destination network device
 * @skb: buffer to forward
 *
 * return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped, but freed)
 *
 * dev_forward_skb can be used for injecting an skb from the
 * start_xmit function of one device into the receive queue
 * of another device.
 *
 * The receiving device may be in another namespace, so
 * we have to clear all information in the skb that could
 * impact namespace isolation.
 */
int dev_forward_skb(struct net_device *dev, struct sk_buff *skb)
{
	return __dev_forward_skb(dev, skb) ?: netif_rx_internal(skb);
}
EXPORT_SYMBOL_GPL(dev_forward_skb);

int dev_forward_skb_nomtu(struct net_device *dev, struct sk_buff *skb)
{
	return __dev_forward_skb2(dev, skb, false) ?: netif_rx_internal(skb);
}

static inline int deliver_skb(struct sk_buff *skb,
			      struct packet_type *pt_prev,
			      struct net_device *orig_dev)
{
	if (unlikely(skb_orphan_frags_rx(skb, GFP_ATOMIC)))
		return -ENOMEM;
	refcount_inc(&skb->users);
	return pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
}

static inline void deliver_ptype_list_skb(struct sk_buff *skb,
					  struct packet_type **pt,
					  struct net_device *orig_dev,
					  __be16 type,
					  struct list_head *ptype_list)
{
	struct packet_type *ptype, *pt_prev = *pt;

	list_for_each_entry_rcu(ptype, ptype_list, list) {
		if (ptype->type != type)
			continue;
		if (pt_prev)
			deliver_skb(skb, pt_prev, orig_dev);
		pt_prev = ptype;
	}
	*pt = pt_prev;
}

static inline bool skb_loop_sk(struct packet_type *ptype, struct sk_buff *skb)
{
	if (!ptype->af_packet_priv || !skb->sk)
		return false;

	if (ptype->id_match)
		return ptype->id_match(ptype, skb->sk);
	else if ((struct sock *)ptype->af_packet_priv == skb->sk)
		return true;

	return false;
}

/**
 * dev_nit_active_rcu - return true if any network interface taps are in use
 *
 * The caller must hold the RCU lock
 *
 * @dev: network device to check for the presence of taps
 */
bool dev_nit_active_rcu(const struct net_device *dev)
{
	/* Callers may hold either RCU or RCU BH lock */
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());

	return !list_empty(&dev_net(dev)->ptype_all) ||
	       !list_empty(&dev->ptype_all);
}
EXPORT_SYMBOL_GPL(dev_nit_active_rcu);

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */

void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev)
{
	struct packet_type *ptype, *pt_prev = NULL;
	struct list_head *ptype_list;
	struct sk_buff *skb2 = NULL;

	rcu_read_lock();
	ptype_list = &dev_net_rcu(dev)->ptype_all;
again:
	list_for_each_entry_rcu(ptype, ptype_list, list) {
		if (READ_ONCE(ptype->ignore_outgoing))
			continue;

		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		if (skb_loop_sk(ptype, skb))
			continue;

		if (pt_prev) {
			deliver_skb(skb2, pt_prev, skb->dev);
			pt_prev = ptype;
			continue;
		}

		/* need to clone skb, done only once */
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (!skb2)
			goto out_unlock;

		net_timestamp_set(skb2);

		/* skb->nh should be correctly
		 * set by sender, so that the second statement is
		 * just protection against buggy protocols.
		 */
		skb_reset_mac_header(skb2);

		if (skb_network_header(skb2) < skb2->data ||
		    skb_network_header(skb2) > skb_tail_pointer(skb2)) {
			net_crit_ratelimited("protocol %04x is buggy, dev %s\n",
					     ntohs(skb2->protocol),
					     dev->name);
			skb_reset_network_header(skb2);
		}

		skb2->transport_header = skb2->network_header;
		skb2->pkt_type = PACKET_OUTGOING;
		pt_prev = ptype;
	}

	if (ptype_list != &dev->ptype_all) {
		ptype_list = &dev->ptype_all;
		goto again;
	}
out_unlock:
	if (pt_prev) {
		if (!skb_orphan_frags_rx(skb2, GFP_ATOMIC))
			pt_prev->func(skb2, skb->dev, pt_prev, skb->dev);
		else
			kfree_skb(skb2);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(dev_queue_xmit_nit);

/**
 * netif_setup_tc - Handle tc mappings on real_num_tx_queues change
 * @dev: Network device
 * @txq: number of queues available
 *
 * If real_num_tx_queues is changed the tc mappings may no longer be
 * valid. To resolve this verify the tc mapping remains valid and if
 * not NULL the mapping. With no priorities mapping to this
 * offset/count pair it will no longer be used. In the worst case TC0
 * is invalid nothing can be done so disable priority mappings. If is
 * expected that drivers will fix this mapping if they can before
 * calling netif_set_real_num_tx_queues.
 */
static void netif_setup_tc(struct net_device *dev, unsigned int txq)
{
	int i;
	struct netdev_tc_txq *tc = &dev->tc_to_txq[0];

	/* If TC0 is invalidated disable TC mapping */
	if (tc->offset + tc->count > txq) {
		netdev_warn(dev, "Number of in use tx queues changed invalidating tc mappings. Priority traffic classification disabled!\n");
		dev->num_tc = 0;
		return;
	}

	/* Invalidated prio to tc mappings set to TC0 */
	for (i = 1; i < TC_BITMASK + 1; i++) {
		int q = netdev_get_prio_tc_map(dev, i);

		tc = &dev->tc_to_txq[q];
		if (tc->offset + tc->count > txq) {
			netdev_warn(dev, "Number of in use tx queues changed. Priority %i to tc mapping %i is no longer valid. Setting map to 0\n",
				    i, q);
			netdev_set_prio_tc_map(dev, i, 0);
		}
	}
}

int netdev_txq_to_tc(struct net_device *dev, unsigned int txq)
{
	if (dev->num_tc) {
		struct netdev_tc_txq *tc = &dev->tc_to_txq[0];
		int i;

		/* walk through the TCs and see if it falls into any of them */
		for (i = 0; i < TC_MAX_QUEUE; i++, tc++) {
			if ((txq - tc->offset) < tc->count)
				return i;
		}

		/* didn't find it, just return -1 to indicate no match */
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(netdev_txq_to_tc);

#ifdef CONFIG_XPS
static struct static_key xps_needed __read_mostly;
static struct static_key xps_rxqs_needed __read_mostly;
static DEFINE_MUTEX(xps_map_mutex);
#define xmap_dereference(P)		\
	rcu_dereference_protected((P), lockdep_is_held(&xps_map_mutex))

static bool remove_xps_queue(struct xps_dev_maps *dev_maps,
			     struct xps_dev_maps *old_maps, int tci, u16 index)
{
	struct xps_map *map = NULL;
	int pos;

	map = xmap_dereference(dev_maps->attr_map[tci]);
	if (!map)
		return false;

	for (pos = map->len; pos--;) {
		if (map->queues[pos] != index)
			continue;

		if (map->len > 1) {
			map->queues[pos] = map->queues[--map->len];
			break;
		}

		if (old_maps)
			RCU_INIT_POINTER(old_maps->attr_map[tci], NULL);
		RCU_INIT_POINTER(dev_maps->attr_map[tci], NULL);
		kfree_rcu(map, rcu);
		return false;
	}

	return true;
}

static bool remove_xps_queue_cpu(struct net_device *dev,
				 struct xps_dev_maps *dev_maps,
				 int cpu, u16 offset, u16 count)
{
	int num_tc = dev_maps->num_tc;
	bool active = false;
	int tci;

	for (tci = cpu * num_tc; num_tc--; tci++) {
		int i, j;

		for (i = count, j = offset; i--; j++) {
			if (!remove_xps_queue(dev_maps, NULL, tci, j))
				break;
		}

		active |= i < 0;
	}

	return active;
}

static void reset_xps_maps(struct net_device *dev,
			   struct xps_dev_maps *dev_maps,
			   enum xps_map_type type)
{
	static_key_slow_dec_cpuslocked(&xps_needed);
	if (type == XPS_RXQS)
		static_key_slow_dec_cpuslocked(&xps_rxqs_needed);

	RCU_INIT_POINTER(dev->xps_maps[type], NULL);

	kfree_rcu(dev_maps, rcu);
}

static void clean_xps_maps(struct net_device *dev, enum xps_map_type type,
			   u16 offset, u16 count)
{
	struct xps_dev_maps *dev_maps;
	bool active = false;
	int i, j;

	dev_maps = xmap_dereference(dev->xps_maps[type]);
	if (!dev_maps)
		return;

	for (j = 0; j < dev_maps->nr_ids; j++)
		active |= remove_xps_queue_cpu(dev, dev_maps, j, offset, count);
	if (!active)
		reset_xps_maps(dev, dev_maps, type);

	if (type == XPS_CPUS) {
		for (i = offset + (count - 1); count--; i--)
			netdev_queue_numa_node_write(
				netdev_get_tx_queue(dev, i), NUMA_NO_NODE);
	}
}

static void netif_reset_xps_queues(struct net_device *dev, u16 offset,
				   u16 count)
{
	if (!static_key_false(&xps_needed))
		return;

	cpus_read_lock();
	mutex_lock(&xps_map_mutex);

	if (static_key_false(&xps_rxqs_needed))
		clean_xps_maps(dev, XPS_RXQS, offset, count);

	clean_xps_maps(dev, XPS_CPUS, offset, count);

	mutex_unlock(&xps_map_mutex);
	cpus_read_unlock();
}

static void netif_reset_xps_queues_gt(struct net_device *dev, u16 index)
{
	netif_reset_xps_queues(dev, index, dev->num_tx_queues - index);
}

static struct xps_map *expand_xps_map(struct xps_map *map, int attr_index,
				      u16 index, bool is_rxqs_map)
{
	struct xps_map *new_map;
	int alloc_len = XPS_MIN_MAP_ALLOC;
	int i, pos;

	for (pos = 0; map && pos < map->len; pos++) {
		if (map->queues[pos] != index)
			continue;
		return map;
	}

	/* Need to add tx-queue to this CPU's/rx-queue's existing map */
	if (map) {
		if (pos < map->alloc_len)
			return map;

		alloc_len = map->alloc_len * 2;
	}

	/* Need to allocate new map to store tx-queue on this CPU's/rx-queue's
	 *  map
	 */
	if (is_rxqs_map)
		new_map = kzalloc(XPS_MAP_SIZE(alloc_len), GFP_KERNEL);
	else
		new_map = kzalloc_node(XPS_MAP_SIZE(alloc_len), GFP_KERNEL,
				       cpu_to_node(attr_index));
	if (!new_map)
		return NULL;

	for (i = 0; i < pos; i++)
		new_map->queues[i] = map->queues[i];
	new_map->alloc_len = alloc_len;
	new_map->len = pos;

	return new_map;
}

/* Copy xps maps at a given index */
static void xps_copy_dev_maps(struct xps_dev_maps *dev_maps,
			      struct xps_dev_maps *new_dev_maps, int index,
			      int tc, bool skip_tc)
{
	int i, tci = index * dev_maps->num_tc;
	struct xps_map *map;

	/* copy maps belonging to foreign traffic classes */
	for (i = 0; i < dev_maps->num_tc; i++, tci++) {
		if (i == tc && skip_tc)
			continue;

		/* fill in the new device map from the old device map */
		map = xmap_dereference(dev_maps->attr_map[tci]);
		RCU_INIT_POINTER(new_dev_maps->attr_map[tci], map);
	}
}

/* Must be called under cpus_read_lock */
int __netif_set_xps_queue(struct net_device *dev, const unsigned long *mask,
			  u16 index, enum xps_map_type type)
{
	struct xps_dev_maps *dev_maps, *new_dev_maps = NULL, *old_dev_maps = NULL;
	const unsigned long *online_mask = NULL;
	bool active = false, copy = false;
	int i, j, tci, numa_node_id = -2;
	int maps_sz, num_tc = 1, tc = 0;
	struct xps_map *map, *new_map;
	unsigned int nr_ids;

	WARN_ON_ONCE(index >= dev->num_tx_queues);

	if (dev->num_tc) {
		/* Do not allow XPS on subordinate device directly */
		num_tc = dev->num_tc;
		if (num_tc < 0)
			return -EINVAL;

		/* If queue belongs to subordinate dev use its map */
		dev = netdev_get_tx_queue(dev, index)->sb_dev ? : dev;

		tc = netdev_txq_to_tc(dev, index);
		if (tc < 0)
			return -EINVAL;
	}

	mutex_lock(&xps_map_mutex);

	dev_maps = xmap_dereference(dev->xps_maps[type]);
	if (type == XPS_RXQS) {
		maps_sz = XPS_RXQ_DEV_MAPS_SIZE(num_tc, dev->num_rx_queues);
		nr_ids = dev->num_rx_queues;
	} else {
		maps_sz = XPS_CPU_DEV_MAPS_SIZE(num_tc);
		if (num_possible_cpus() > 1)
			online_mask = cpumask_bits(cpu_online_mask);
		nr_ids = nr_cpu_ids;
	}

	if (maps_sz < L1_CACHE_BYTES)
		maps_sz = L1_CACHE_BYTES;

	/* The old dev_maps could be larger or smaller than the one we're
	 * setting up now, as dev->num_tc or nr_ids could have been updated in
	 * between. We could try to be smart, but let's be safe instead and only
	 * copy foreign traffic classes if the two map sizes match.
	 */
	if (dev_maps &&
	    dev_maps->num_tc == num_tc && dev_maps->nr_ids == nr_ids)
		copy = true;

	/* allocate memory for queue storage */
	for (j = -1; j = netif_attrmask_next_and(j, online_mask, mask, nr_ids),
	     j < nr_ids;) {
		if (!new_dev_maps) {
			new_dev_maps = kzalloc(maps_sz, GFP_KERNEL);
			if (!new_dev_maps) {
				mutex_unlock(&xps_map_mutex);
				return -ENOMEM;
			}

			new_dev_maps->nr_ids = nr_ids;
			new_dev_maps->num_tc = num_tc;
		}

		tci = j * num_tc + tc;
		map = copy ? xmap_dereference(dev_maps->attr_map[tci]) : NULL;

		map = expand_xps_map(map, j, index, type == XPS_RXQS);
		if (!map)
			goto error;

		RCU_INIT_POINTER(new_dev_maps->attr_map[tci], map);
	}

	if (!new_dev_maps)
		goto out_no_new_maps;

	if (!dev_maps) {
		/* Increment static keys at most once per type */
		static_key_slow_inc_cpuslocked(&xps_needed);
		if (type == XPS_RXQS)
			static_key_slow_inc_cpuslocked(&xps_rxqs_needed);
	}

	for (j = 0; j < nr_ids; j++) {
		bool skip_tc = false;

		tci = j * num_tc + tc;
		if (netif_attr_test_mask(j, mask, nr_ids) &&
		    netif_attr_test_online(j, online_mask, nr_ids)) {
			/* add tx-queue to CPU/rx-queue maps */
			int pos = 0;

			skip_tc = true;

			map = xmap_dereference(new_dev_maps->attr_map[tci]);
			while ((pos < map->len) && (map->queues[pos] != index))
				pos++;

			if (pos == map->len)
				map->queues[map->len++] = index;
#ifdef CONFIG_NUMA
			if (type == XPS_CPUS) {
				if (numa_node_id == -2)
					numa_node_id = cpu_to_node(j);
				else if (numa_node_id != cpu_to_node(j))
					numa_node_id = -1;
			}
#endif
		}

		if (copy)
			xps_copy_dev_maps(dev_maps, new_dev_maps, j, tc,
					  skip_tc);
	}

	rcu_assign_pointer(dev->xps_maps[type], new_dev_maps);

	/* Cleanup old maps */
	if (!dev_maps)
		goto out_no_old_maps;

	for (j = 0; j < dev_maps->nr_ids; j++) {
		for (i = num_tc, tci = j * dev_maps->num_tc; i--; tci++) {
			map = xmap_dereference(dev_maps->attr_map[tci]);
			if (!map)
				continue;

			if (copy) {
				new_map = xmap_dereference(new_dev_maps->attr_map[tci]);
				if (map == new_map)
					continue;
			}

			RCU_INIT_POINTER(dev_maps->attr_map[tci], NULL);
			kfree_rcu(map, rcu);
		}
	}

	old_dev_maps = dev_maps;

out_no_old_maps:
	dev_maps = new_dev_maps;
	active = true;

out_no_new_maps:
	if (type == XPS_CPUS)
		/* update Tx queue numa node */
		netdev_queue_numa_node_write(netdev_get_tx_queue(dev, index),
					     (numa_node_id >= 0) ?
					     numa_node_id : NUMA_NO_NODE);

	if (!dev_maps)
		goto out_no_maps;

	/* removes tx-queue from unused CPUs/rx-queues */
	for (j = 0; j < dev_maps->nr_ids; j++) {
		tci = j * dev_maps->num_tc;

		for (i = 0; i < dev_maps->num_tc; i++, tci++) {
			if (i == tc &&
			    netif_attr_test_mask(j, mask, dev_maps->nr_ids) &&
			    netif_attr_test_online(j, online_mask, dev_maps->nr_ids))
				continue;

			active |= remove_xps_queue(dev_maps,
						   copy ? old_dev_maps : NULL,
						   tci, index);
		}
	}

	if (old_dev_maps)
		kfree_rcu(old_dev_maps, rcu);

	/* free map if not active */
	if (!active)
		reset_xps_maps(dev, dev_maps, type);

out_no_maps:
	mutex_unlock(&xps_map_mutex);

	return 0;
error:
	/* remove any maps that we added */
	for (j = 0; j < nr_ids; j++) {
		for (i = num_tc, tci = j * num_tc; i--; tci++) {
			new_map = xmap_dereference(new_dev_maps->attr_map[tci]);
			map = copy ?
			      xmap_dereference(dev_maps->attr_map[tci]) :
			      NULL;
			if (new_map && new_map != map)
				kfree(new_map);
		}
	}

	mutex_unlock(&xps_map_mutex);

	kfree(new_dev_maps);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(__netif_set_xps_queue);

int netif_set_xps_queue(struct net_device *dev, const struct cpumask *mask,
			u16 index)
{
	int ret;

	cpus_read_lock();
	ret =  __netif_set_xps_queue(dev, cpumask_bits(mask), index, XPS_CPUS);
	cpus_read_unlock();

	return ret;
}
EXPORT_SYMBOL(netif_set_xps_queue);

#endif
static void netdev_unbind_all_sb_channels(struct net_device *dev)
{
	struct netdev_queue *txq = &dev->_tx[dev->num_tx_queues];

	/* Unbind any subordinate channels */
	while (txq-- != &dev->_tx[0]) {
		if (txq->sb_dev)
			netdev_unbind_sb_channel(dev, txq->sb_dev);
	}
}

void netdev_reset_tc(struct net_device *dev)
{
#ifdef CONFIG_XPS
	netif_reset_xps_queues_gt(dev, 0);
#endif
	netdev_unbind_all_sb_channels(dev);

	/* Reset TC configuration of device */
	dev->num_tc = 0;
	memset(dev->tc_to_txq, 0, sizeof(dev->tc_to_txq));
	memset(dev->prio_tc_map, 0, sizeof(dev->prio_tc_map));
}
EXPORT_SYMBOL(netdev_reset_tc);

int netdev_set_tc_queue(struct net_device *dev, u8 tc, u16 count, u16 offset)
{
	if (tc >= dev->num_tc)
		return -EINVAL;

#ifdef CONFIG_XPS
	netif_reset_xps_queues(dev, offset, count);
#endif
	dev->tc_to_txq[tc].count = count;
	dev->tc_to_txq[tc].offset = offset;
	return 0;
}
EXPORT_SYMBOL(netdev_set_tc_queue);

int netdev_set_num_tc(struct net_device *dev, u8 num_tc)
{
	if (num_tc > TC_MAX_QUEUE)
		return -EINVAL;

#ifdef CONFIG_XPS
	netif_reset_xps_queues_gt(dev, 0);
#endif
	netdev_unbind_all_sb_channels(dev);

	dev->num_tc = num_tc;
	return 0;
}
EXPORT_SYMBOL(netdev_set_num_tc);

void netdev_unbind_sb_channel(struct net_device *dev,
			      struct net_device *sb_dev)
{
	struct netdev_queue *txq = &dev->_tx[dev->num_tx_queues];

#ifdef CONFIG_XPS
	netif_reset_xps_queues_gt(sb_dev, 0);
#endif
	memset(sb_dev->tc_to_txq, 0, sizeof(sb_dev->tc_to_txq));
	memset(sb_dev->prio_tc_map, 0, sizeof(sb_dev->prio_tc_map));

	while (txq-- != &dev->_tx[0]) {
		if (txq->sb_dev == sb_dev)
			txq->sb_dev = NULL;
	}
}
EXPORT_SYMBOL(netdev_unbind_sb_channel);

int netdev_bind_sb_channel_queue(struct net_device *dev,
				 struct net_device *sb_dev,
				 u8 tc, u16 count, u16 offset)
{
	/* Make certain the sb_dev and dev are already configured */
	if (sb_dev->num_tc >= 0 || tc >= dev->num_tc)
		return -EINVAL;

	/* We cannot hand out queues we don't have */
	if ((offset + count) > dev->real_num_tx_queues)
		return -EINVAL;

	/* Record the mapping */
	sb_dev->tc_to_txq[tc].count = count;
	sb_dev->tc_to_txq[tc].offset = offset;

	/* Provide a way for Tx queue to find the tc_to_txq map or
	 * XPS map for itself.
	 */
	while (count--)
		netdev_get_tx_queue(dev, count + offset)->sb_dev = sb_dev;

	return 0;
}
EXPORT_SYMBOL(netdev_bind_sb_channel_queue);

int netdev_set_sb_channel(struct net_device *dev, u16 channel)
{
	/* Do not use a multiqueue device to represent a subordinate channel */
	if (netif_is_multiqueue(dev))
		return -ENODEV;

	/* We allow channels 1 - 32767 to be used for subordinate channels.
	 * Channel 0 is meant to be "native" mode and used only to represent
	 * the main root device. We allow writing 0 to reset the device back
	 * to normal mode after being used as a subordinate channel.
	 */
	if (channel > S16_MAX)
		return -EINVAL;

	dev->num_tc = -channel;

	return 0;
}
EXPORT_SYMBOL(netdev_set_sb_channel);

/*
 * Routine to help set real_num_tx_queues. To avoid skbs mapped to queues
 * greater than real_num_tx_queues stale skbs on the qdisc must be flushed.
 */
int netif_set_real_num_tx_queues(struct net_device *dev, unsigned int txq)
{
	bool disabling;
	int rc;

	disabling = txq < dev->real_num_tx_queues;

	if (txq < 1 || txq > dev->num_tx_queues)
		return -EINVAL;

	if (dev->reg_state == NETREG_REGISTERED ||
	    dev->reg_state == NETREG_UNREGISTERING) {
		netdev_ops_assert_locked(dev);

		rc = netdev_queue_update_kobjects(dev, dev->real_num_tx_queues,
						  txq);
		if (rc)
			return rc;

		if (dev->num_tc)
			netif_setup_tc(dev, txq);

		net_shaper_set_real_num_tx_queues(dev, txq);

		dev_qdisc_change_real_num_tx(dev, txq);

		dev->real_num_tx_queues = txq;

		if (disabling) {
			synchronize_net();
			qdisc_reset_all_tx_gt(dev, txq);
#ifdef CONFIG_XPS
			netif_reset_xps_queues_gt(dev, txq);
#endif
		}
	} else {
		dev->real_num_tx_queues = txq;
	}

	return 0;
}
EXPORT_SYMBOL(netif_set_real_num_tx_queues);

/**
 *	netif_set_real_num_rx_queues - set actual number of RX queues used
 *	@dev: Network device
 *	@rxq: Actual number of RX queues
 *
 *	This must be called either with the rtnl_lock held or before
 *	registration of the net device.  Returns 0 on success, or a
 *	negative error code.  If called before registration, it always
 *	succeeds.
 */
int netif_set_real_num_rx_queues(struct net_device *dev, unsigned int rxq)
{
	int rc;

	if (rxq < 1 || rxq > dev->num_rx_queues)
		return -EINVAL;

	if (dev->reg_state == NETREG_REGISTERED) {
		netdev_ops_assert_locked(dev);

		rc = net_rx_queue_update_kobjects(dev, dev->real_num_rx_queues,
						  rxq);
		if (rc)
			return rc;
	}

	dev->real_num_rx_queues = rxq;
	return 0;
}
EXPORT_SYMBOL(netif_set_real_num_rx_queues);

/**
 *	netif_set_real_num_queues - set actual number of RX and TX queues used
 *	@dev: Network device
 *	@txq: Actual number of TX queues
 *	@rxq: Actual number of RX queues
 *
 *	Set the real number of both TX and RX queues.
 *	Does nothing if the number of queues is already correct.
 */
int netif_set_real_num_queues(struct net_device *dev,
			      unsigned int txq, unsigned int rxq)
{
	unsigned int old_rxq = dev->real_num_rx_queues;
	int err;

	if (txq < 1 || txq > dev->num_tx_queues ||
	    rxq < 1 || rxq > dev->num_rx_queues)
		return -EINVAL;

	/* Start from increases, so the error path only does decreases -
	 * decreases can't fail.
	 */
	if (rxq > dev->real_num_rx_queues) {
		err = netif_set_real_num_rx_queues(dev, rxq);
		if (err)
			return err;
	}
	if (txq > dev->real_num_tx_queues) {
		err = netif_set_real_num_tx_queues(dev, txq);
		if (err)
			goto undo_rx;
	}
	if (rxq < dev->real_num_rx_queues)
		WARN_ON(netif_set_real_num_rx_queues(dev, rxq));
	if (txq < dev->real_num_tx_queues)
		WARN_ON(netif_set_real_num_tx_queues(dev, txq));

	return 0;
undo_rx:
	WARN_ON(netif_set_real_num_rx_queues(dev, old_rxq));
	return err;
}
EXPORT_SYMBOL(netif_set_real_num_queues);

/**
 * netif_set_tso_max_size() - set the max size of TSO frames supported
 * @dev:	netdev to update
 * @size:	max skb->len of a TSO frame
 *
 * Set the limit on the size of TSO super-frames the device can handle.
 * Unless explicitly set the stack will assume the value of
 * %GSO_LEGACY_MAX_SIZE.
 */
void netif_set_tso_max_size(struct net_device *dev, unsigned int size)
{
	dev->tso_max_size = min(GSO_MAX_SIZE, size);
	if (size < READ_ONCE(dev->gso_max_size))
		netif_set_gso_max_size(dev, size);
	if (size < READ_ONCE(dev->gso_ipv4_max_size))
		netif_set_gso_ipv4_max_size(dev, size);
}
EXPORT_SYMBOL(netif_set_tso_max_size);

/**
 * netif_set_tso_max_segs() - set the max number of segs supported for TSO
 * @dev:	netdev to update
 * @segs:	max number of TCP segments
 *
 * Set the limit on the number of TCP segments the device can generate from
 * a single TSO super-frame.
 * Unless explicitly set the stack will assume the value of %GSO_MAX_SEGS.
 */
void netif_set_tso_max_segs(struct net_device *dev, unsigned int segs)
{
	dev->tso_max_segs = segs;
	if (segs < READ_ONCE(dev->gso_max_segs))
		netif_set_gso_max_segs(dev, segs);
}
EXPORT_SYMBOL(netif_set_tso_max_segs);

/**
 * netif_inherit_tso_max() - copy all TSO limits from a lower device to an upper
 * @to:		netdev to update
 * @from:	netdev from which to copy the limits
 */
void netif_inherit_tso_max(struct net_device *to, const struct net_device *from)
{
	netif_set_tso_max_size(to, from->tso_max_size);
	netif_set_tso_max_segs(to, from->tso_max_segs);
}
EXPORT_SYMBOL(netif_inherit_tso_max);

/**
 * netif_get_num_default_rss_queues - default number of RSS queues
 *
 * Default value is the number of physical cores if there are only 1 or 2, or
 * divided by 2 if there are more.
 */
int netif_get_num_default_rss_queues(void)
{
	cpumask_var_t cpus;
	int cpu, count = 0;

	if (unlikely(is_kdump_kernel() || !zalloc_cpumask_var(&cpus, GFP_KERNEL)))
		return 1;

	cpumask_copy(cpus, cpu_online_mask);
	for_each_cpu(cpu, cpus) {
		++count;
		cpumask_andnot(cpus, cpus, topology_sibling_cpumask(cpu));
	}
	free_cpumask_var(cpus);

	return count > 2 ? DIV_ROUND_UP(count, 2) : count;
}
EXPORT_SYMBOL(netif_get_num_default_rss_queues);

static void __netif_reschedule(struct Qdisc *q)
{
	struct softnet_data *sd;
	unsigned long flags;

	local_irq_save(flags);
	sd = this_cpu_ptr(&softnet_data);
	q->next_sched = NULL;
	*sd->output_queue_tailp = q;
	sd->output_queue_tailp = &q->next_sched;
	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_restore(flags);
}

void __netif_schedule(struct Qdisc *q)
{
	if (!test_and_set_bit(__QDISC_STATE_SCHED, &q->state))
		__netif_reschedule(q);
}
EXPORT_SYMBOL(__netif_schedule);

struct dev_kfree_skb_cb {
	enum skb_drop_reason reason;
};

static struct dev_kfree_skb_cb *get_kfree_skb_cb(const struct sk_buff *skb)
{
	return (struct dev_kfree_skb_cb *)skb->cb;
}

void netif_schedule_queue(struct netdev_queue *txq)
{
	rcu_read_lock();
	if (!netif_xmit_stopped(txq)) {
		struct Qdisc *q = rcu_dereference(txq->qdisc);

		__netif_schedule(q);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(netif_schedule_queue);

void netif_tx_wake_queue(struct netdev_queue *dev_queue)
{
	if (test_and_clear_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state)) {
		struct Qdisc *q;

		rcu_read_lock();
		q = rcu_dereference(dev_queue->qdisc);
		__netif_schedule(q);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(netif_tx_wake_queue);

void dev_kfree_skb_irq_reason(struct sk_buff *skb, enum skb_drop_reason reason)
{
	unsigned long flags;

	if (unlikely(!skb))
		return;

	if (likely(refcount_read(&skb->users) == 1)) {
		smp_rmb();
		refcount_set(&skb->users, 0);
	} else if (likely(!refcount_dec_and_test(&skb->users))) {
		return;
	}
	get_kfree_skb_cb(skb)->reason = reason;
	local_irq_save(flags);
	skb->next = __this_cpu_read(softnet_data.completion_queue);
	__this_cpu_write(softnet_data.completion_queue, skb);
	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(dev_kfree_skb_irq_reason);

void dev_kfree_skb_any_reason(struct sk_buff *skb, enum skb_drop_reason reason)
{
	if (in_hardirq() || irqs_disabled())
		dev_kfree_skb_irq_reason(skb, reason);
	else
		kfree_skb_reason(skb, reason);
}
EXPORT_SYMBOL(dev_kfree_skb_any_reason);


/**
 * netif_device_detach - mark device as removed
 * @dev: network device
 *
 * Mark device as removed from system and therefore no longer available.
 */
void netif_device_detach(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
		netif_tx_stop_all_queues(dev);
	}
}
EXPORT_SYMBOL(netif_device_detach);

/**
 * netif_device_attach - mark device as attached
 * @dev: network device
 *
 * Mark device as attached from system and restart if needed.
 */
void netif_device_attach(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
		netif_tx_wake_all_queues(dev);
		netdev_watchdog_up(dev);
	}
}
EXPORT_SYMBOL(netif_device_attach);

/*
 * Returns a Tx hash based on the given packet descriptor a Tx queues' number
 * to be used as a distribution range.
 */
static u16 skb_tx_hash(const struct net_device *dev,
		       const struct net_device *sb_dev,
		       struct sk_buff *skb)
{
	u32 hash;
	u16 qoffset = 0;
	u16 qcount = dev->real_num_tx_queues;

	if (dev->num_tc) {
		u8 tc = netdev_get_prio_tc_map(dev, skb->priority);

		qoffset = sb_dev->tc_to_txq[tc].offset;
		qcount = sb_dev->tc_to_txq[tc].count;
		if (unlikely(!qcount)) {
			net_warn_ratelimited("%s: invalid qcount, qoffset %u for tc %u\n",
					     sb_dev->name, qoffset, tc);
			qoffset = 0;
			qcount = dev->real_num_tx_queues;
		}
	}

	if (skb_rx_queue_recorded(skb)) {
		DEBUG_NET_WARN_ON_ONCE(qcount == 0);
		hash = skb_get_rx_queue(skb);
		if (hash >= qoffset)
			hash -= qoffset;
		while (unlikely(hash >= qcount))
			hash -= qcount;
		return hash + qoffset;
	}

	return (u16) reciprocal_scale(skb_get_hash(skb), qcount) + qoffset;
}

void skb_warn_bad_offload(const struct sk_buff *skb)
{
	static const netdev_features_t null_features;
	struct net_device *dev = skb->dev;
	const char *name = "";

	if (!net_ratelimit())
		return;

	if (dev) {
		if (dev->dev.parent)
			name = dev_driver_string(dev->dev.parent);
		else
			name = netdev_name(dev);
	}
	skb_dump(KERN_WARNING, skb, false);
	WARN(1, "%s: caps=(%pNF, %pNF)\n",
	     name, dev ? &dev->features : &null_features,
	     skb->sk ? &skb->sk->sk_route_caps : &null_features);
}

/*
 * Invalidate hardware checksum when packet is to be mangled, and
 * complete checksum manually on outgoing path.
 */
int skb_checksum_help(struct sk_buff *skb)
{
	__wsum csum;
	int ret = 0, offset;

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		goto out_set_summed;

	if (unlikely(skb_is_gso(skb))) {
		skb_warn_bad_offload(skb);
		return -EINVAL;
	}

	if (!skb_frags_readable(skb)) {
		return -EFAULT;
	}

	/* Before computing a checksum, we should make sure no frag could
	 * be modified by an external entity : checksum could be wrong.
	 */
	if (skb_has_shared_frag(skb)) {
		ret = __skb_linearize(skb);
		if (ret)
			goto out;
	}

	offset = skb_checksum_start_offset(skb);
	ret = -EINVAL;
	if (unlikely(offset >= skb_headlen(skb))) {
		DO_ONCE_LITE(skb_dump, KERN_ERR, skb, false);
		WARN_ONCE(true, "offset (%d) >= skb_headlen() (%u)\n",
			  offset, skb_headlen(skb));
		goto out;
	}
	csum = skb_checksum(skb, offset, skb->len - offset, 0);

	offset += skb->csum_offset;
	if (unlikely(offset + sizeof(__sum16) > skb_headlen(skb))) {
		DO_ONCE_LITE(skb_dump, KERN_ERR, skb, false);
		WARN_ONCE(true, "offset+2 (%zu) > skb_headlen() (%u)\n",
			  offset + sizeof(__sum16), skb_headlen(skb));
		goto out;
	}
	ret = skb_ensure_writable(skb, offset + sizeof(__sum16));
	if (ret)
		goto out;

	*(__sum16 *)(skb->data + offset) = csum_fold(csum) ?: CSUM_MANGLED_0;
out_set_summed:
	skb->ip_summed = CHECKSUM_NONE;
out:
	return ret;
}
EXPORT_SYMBOL(skb_checksum_help);

#ifdef CONFIG_NET_CRC32C
int skb_crc32c_csum_help(struct sk_buff *skb)
{
	u32 crc;
	int ret = 0, offset, start;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		goto out;

	if (unlikely(skb_is_gso(skb)))
		goto out;

	/* Before computing a checksum, we should make sure no frag could
	 * be modified by an external entity : checksum could be wrong.
	 */
	if (unlikely(skb_has_shared_frag(skb))) {
		ret = __skb_linearize(skb);
		if (ret)
			goto out;
	}
	start = skb_checksum_start_offset(skb);
	offset = start + offsetof(struct sctphdr, checksum);
	if (WARN_ON_ONCE(offset >= skb_headlen(skb))) {
		ret = -EINVAL;
		goto out;
	}

	ret = skb_ensure_writable(skb, offset + sizeof(__le32));
	if (ret)
		goto out;

	crc = ~skb_crc32c(skb, start, skb->len - start, ~0);
	*(__le32 *)(skb->data + offset) = cpu_to_le32(crc);
	skb_reset_csum_not_inet(skb);
out:
	return ret;
}
EXPORT_SYMBOL(skb_crc32c_csum_help);
#endif /* CONFIG_NET_CRC32C */

__be16 skb_network_protocol(struct sk_buff *skb, int *depth)
{
	__be16 type = skb->protocol;

	/* Tunnel gso handlers can set protocol to ethernet. */
	if (type == htons(ETH_P_TEB)) {
		struct ethhdr *eth;

		if (unlikely(!pskb_may_pull(skb, sizeof(struct ethhdr))))
			return 0;

		eth = (struct ethhdr *)skb->data;
		type = eth->h_proto;
	}

	return vlan_get_protocol_and_depth(skb, type, depth);
}


/* Take action when hardware reception checksum errors are detected. */
#ifdef CONFIG_BUG
static void do_netdev_rx_csum_fault(struct net_device *dev, struct sk_buff *skb)
{
	netdev_err(dev, "hw csum failure\n");
	skb_dump(KERN_ERR, skb, true);
	dump_stack();
}

void netdev_rx_csum_fault(struct net_device *dev, struct sk_buff *skb)
{
	DO_ONCE_LITE(do_netdev_rx_csum_fault, dev, skb);
}
EXPORT_SYMBOL(netdev_rx_csum_fault);
#endif

/* XXX: check that highmem exists at all on the given machine. */
static int illegal_highdma(struct net_device *dev, struct sk_buff *skb)
{
#ifdef CONFIG_HIGHMEM
	int i;

	if (!(dev->features & NETIF_F_HIGHDMA)) {
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			struct page *page = skb_frag_page(frag);

			if (page && PageHighMem(page))
				return 1;
		}
	}
#endif
	return 0;
}

/* If MPLS offload request, verify we are testing hardware MPLS features
 * instead of standard features for the netdev.
 */
#if IS_ENABLED(CONFIG_NET_MPLS_GSO)
static netdev_features_t net_mpls_features(struct sk_buff *skb,
					   netdev_features_t features,
					   __be16 type)
{
	if (eth_p_mpls(type))
		features &= skb->dev->mpls_features;

	return features;
}
#else
static netdev_features_t net_mpls_features(struct sk_buff *skb,
					   netdev_features_t features,
					   __be16 type)
{
	return features;
}
#endif

static netdev_features_t harmonize_features(struct sk_buff *skb,
	netdev_features_t features)
{
	__be16 type;

	type = skb_network_protocol(skb, NULL);
	features = net_mpls_features(skb, features, type);

	if (skb->ip_summed != CHECKSUM_NONE &&
	    !can_checksum_protocol(features, type)) {
		features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
	}
	if (illegal_highdma(skb->dev, skb))
		features &= ~NETIF_F_SG;

	return features;
}

netdev_features_t passthru_features_check(struct sk_buff *skb,
					  struct net_device *dev,
					  netdev_features_t features)
{
	return features;
}
EXPORT_SYMBOL(passthru_features_check);

static netdev_features_t dflt_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
	return vlan_features_check(skb, features);
}

static netdev_features_t gso_features_check(const struct sk_buff *skb,
					    struct net_device *dev,
					    netdev_features_t features)
{
	u16 gso_segs = skb_shinfo(skb)->gso_segs;

	if (gso_segs > READ_ONCE(dev->gso_max_segs))
		return features & ~NETIF_F_GSO_MASK;

	if (unlikely(skb->len >= netif_get_gso_max_size(dev, skb)))
		return features & ~NETIF_F_GSO_MASK;

	if (!skb_shinfo(skb)->gso_type) {
		skb_warn_bad_offload(skb);
		return features & ~NETIF_F_GSO_MASK;
	}

	/* Support for GSO partial features requires software
	 * intervention before we can actually process the packets
	 * so we need to strip support for any partial features now
	 * and we can pull them back in after we have partially
	 * segmented the frame.
	 */
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL))
		features &= ~dev->gso_partial_features;

	/* Make sure to clear the IPv4 ID mangling feature if the
	 * IPv4 header has the potential to be fragmented.
	 */
	if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4) {
		struct iphdr *iph = skb->encapsulation ?
				    inner_ip_hdr(skb) : ip_hdr(skb);

		if (!(iph->frag_off & htons(IP_DF)))
			features &= ~NETIF_F_TSO_MANGLEID;
	}

	return features;
}

netdev_features_t netif_skb_features(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	netdev_features_t features = dev->features;

	if (skb_is_gso(skb))
		features = gso_features_check(skb, dev, features);

	/* If encapsulation offload request, verify we are testing
	 * hardware encapsulation features instead of standard
	 * features for the netdev
	 */
	if (skb->encapsulation)
		features &= dev->hw_enc_features;

	if (skb_vlan_tagged(skb))
		features = netdev_intersect_features(features,
						     dev->vlan_features |
						     NETIF_F_HW_VLAN_CTAG_TX |
						     NETIF_F_HW_VLAN_STAG_TX);

	if (dev->netdev_ops->ndo_features_check)
		features &= dev->netdev_ops->ndo_features_check(skb, dev,
								features);
	else
		features &= dflt_features_check(skb, dev, features);

	return harmonize_features(skb, features);
}
EXPORT_SYMBOL(netif_skb_features);

static int xmit_one(struct sk_buff *skb, struct net_device *dev,
		    struct netdev_queue *txq, bool more)
{
	unsigned int len;
	int rc;

	if (dev_nit_active_rcu(dev))
		dev_queue_xmit_nit(skb, dev);

	len = skb->len;
	trace_net_dev_start_xmit(skb, dev);
	rc = netdev_start_xmit(skb, dev, txq, more);
	trace_net_dev_xmit(skb, rc, dev, len);

	return rc;
}

struct sk_buff *dev_hard_start_xmit(struct sk_buff *first, struct net_device *dev,
				    struct netdev_queue *txq, int *ret)
{
	struct sk_buff *skb = first;
	int rc = NETDEV_TX_OK;

	while (skb) {
		struct sk_buff *next = skb->next;

		skb_mark_not_on_list(skb);
		rc = xmit_one(skb, dev, txq, next != NULL);
		if (unlikely(!dev_xmit_complete(rc))) {
			skb->next = next;
			goto out;
		}

		skb = next;
		if (netif_tx_queue_stopped(txq) && skb) {
			rc = NETDEV_TX_BUSY;
			break;
		}
	}

out:
	*ret = rc;
	return skb;
}

static struct sk_buff *validate_xmit_vlan(struct sk_buff *skb,
					  netdev_features_t features)
{
	if (skb_vlan_tag_present(skb) &&
	    !vlan_hw_offload_capable(features, skb->vlan_proto))
		skb = __vlan_hwaccel_push_inside(skb);
	return skb;
}

int skb_csum_hwoffload_help(struct sk_buff *skb,
			    const netdev_features_t features)
{
	if (unlikely(skb_csum_is_sctp(skb)))
		return !!(features & NETIF_F_SCTP_CRC) ? 0 :
			skb_crc32c_csum_help(skb);

	if (features & NETIF_F_HW_CSUM)
		return 0;

	if (features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
		if (vlan_get_protocol(skb) == htons(ETH_P_IPV6) &&
		    skb_network_header_len(skb) != sizeof(struct ipv6hdr) &&
		    !ipv6_has_hopopt_jumbo(skb))
			goto sw_checksum;

		switch (skb->csum_offset) {
		case offsetof(struct tcphdr, check):
		case offsetof(struct udphdr, check):
			return 0;
		}
	}

sw_checksum:
	return skb_checksum_help(skb);
}
EXPORT_SYMBOL(skb_csum_hwoffload_help);

static struct sk_buff *validate_xmit_unreadable_skb(struct sk_buff *skb,
						    struct net_device *dev)
{
	struct skb_shared_info *shinfo;
	struct net_iov *niov;

	if (likely(skb_frags_readable(skb)))
		goto out;

	if (!dev->netmem_tx)
		goto out_free;

	shinfo = skb_shinfo(skb);

	if (shinfo->nr_frags > 0) {
		niov = netmem_to_net_iov(skb_frag_netmem(&shinfo->frags[0]));
		if (net_is_devmem_iov(niov) &&
		    net_devmem_iov_binding(niov)->dev != dev)
			goto out_free;
	}

out:
	return skb;

out_free:
	kfree_skb(skb);
	return NULL;
}

static struct sk_buff *validate_xmit_skb(struct sk_buff *skb, struct net_device *dev, bool *again)
{
	netdev_features_t features;

	skb = validate_xmit_unreadable_skb(skb, dev);
	if (unlikely(!skb))
		goto out_null;

	features = netif_skb_features(skb);
	skb = validate_xmit_vlan(skb, features);
	if (unlikely(!skb))
		goto out_null;

	skb = sk_validate_xmit_skb(skb, dev);
	if (unlikely(!skb))
		goto out_null;

	if (netif_needs_gso(skb, features)) {
		struct sk_buff *segs;

		segs = skb_gso_segment(skb, features);
		if (IS_ERR(segs)) {
			goto out_kfree_skb;
		} else if (segs) {
			consume_skb(skb);
			skb = segs;
		}
	} else {
		if (skb_needs_linearize(skb, features) &&
		    __skb_linearize(skb))
			goto out_kfree_skb;

		/* If packet is not checksummed and device does not
		 * support checksumming for this protocol, complete
		 * checksumming here.
		 */
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			if (skb->encapsulation)
				skb_set_inner_transport_header(skb,
							       skb_checksum_start_offset(skb));
			else
				skb_set_transport_header(skb,
							 skb_checksum_start_offset(skb));
			if (skb_csum_hwoffload_help(skb, features))
				goto out_kfree_skb;
		}
	}

	skb = validate_xmit_xfrm(skb, features, again);

	return skb;

out_kfree_skb:
	kfree_skb(skb);
out_null:
	dev_core_stats_tx_dropped_inc(dev);
	return NULL;
}

struct sk_buff *validate_xmit_skb_list(struct sk_buff *skb, struct net_device *dev, bool *again)
{
	struct sk_buff *next, *head = NULL, *tail;

	for (; skb != NULL; skb = next) {
		next = skb->next;
		skb_mark_not_on_list(skb);

		/* in case skb won't be segmented, point to itself */
		skb->prev = skb;

		skb = validate_xmit_skb(skb, dev, again);
		if (!skb)
			continue;

		if (!head)
			head = skb;
		else
			tail->next = skb;
		/* If skb was segmented, skb->prev points to
		 * the last segment. If not, it still contains skb.
		 */
		tail = skb->prev;
	}
	return head;
}
EXPORT_SYMBOL_GPL(validate_xmit_skb_list);

static void qdisc_pkt_len_init(struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);

	qdisc_skb_cb(skb)->pkt_len = skb->len;

	/* To get more precise estimation of bytes sent on wire,
	 * we add to pkt_len the headers size of all segments
	 */
	if (shinfo->gso_size && skb_transport_header_was_set(skb)) {
		u16 gso_segs = shinfo->gso_segs;
		unsigned int hdr_len;

		/* mac layer + network layer */
		if (!skb->encapsulation)
			hdr_len = skb_transport_offset(skb);
		else
			hdr_len = skb_inner_transport_offset(skb);

		/* + transport layer */
		if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))) {
			const struct tcphdr *th;
			struct tcphdr _tcphdr;

			th = skb_header_pointer(skb, hdr_len,
						sizeof(_tcphdr), &_tcphdr);
			if (likely(th))
				hdr_len += __tcp_hdrlen(th);
		} else if (shinfo->gso_type & SKB_GSO_UDP_L4) {
			struct udphdr _udphdr;

			if (skb_header_pointer(skb, hdr_len,
					       sizeof(_udphdr), &_udphdr))
				hdr_len += sizeof(struct udphdr);
		}

		if (unlikely(shinfo->gso_type & SKB_GSO_DODGY)) {
			int payload = skb->len - hdr_len;

			/* Malicious packet. */
			if (payload <= 0)
				return;
			gso_segs = DIV_ROUND_UP(payload, shinfo->gso_size);
		}
		qdisc_skb_cb(skb)->pkt_len += (gso_segs - 1) * hdr_len;
	}
}

static int dev_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *q,
			     struct sk_buff **to_free,
			     struct netdev_queue *txq)
{
	int rc;

	rc = q->enqueue(skb, q, to_free) & NET_XMIT_MASK;
	if (rc == NET_XMIT_SUCCESS)
		trace_qdisc_enqueue(q, txq, skb);
	return rc;
}

static inline int __dev_xmit_skb(struct sk_buff *skb, struct Qdisc *q,
				 struct net_device *dev,
				 struct netdev_queue *txq)
{
	spinlock_t *root_lock = qdisc_lock(q);
	struct sk_buff *to_free = NULL;
	bool contended;
	int rc;

	qdisc_calculate_pkt_len(skb, q);

	tcf_set_drop_reason(skb, SKB_DROP_REASON_QDISC_DROP);

	if (q->flags & TCQ_F_NOLOCK) {
		if (q->flags & TCQ_F_CAN_BYPASS && nolock_qdisc_is_empty(q) &&
		    qdisc_run_begin(q)) {
			/* Retest nolock_qdisc_is_empty() within the protection
			 * of q->seqlock to protect from racing with requeuing.
			 */
			if (unlikely(!nolock_qdisc_is_empty(q))) {
				rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
				__qdisc_run(q);
				qdisc_run_end(q);

				goto no_lock_out;
			}

			qdisc_bstats_cpu_update(q, skb);
			if (sch_direct_xmit(skb, q, dev, txq, NULL, true) &&
			    !nolock_qdisc_is_empty(q))
				__qdisc_run(q);

			qdisc_run_end(q);
			return NET_XMIT_SUCCESS;
		}

		rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
		qdisc_run(q);

no_lock_out:
		if (unlikely(to_free))
			kfree_skb_list_reason(to_free,
					      tcf_get_drop_reason(to_free));
		return rc;
	}

	if (unlikely(READ_ONCE(q->owner) == smp_processor_id())) {
		kfree_skb_reason(skb, SKB_DROP_REASON_TC_RECLASSIFY_LOOP);
		return NET_XMIT_DROP;
	}
	/*
	 * Heuristic to force contended enqueues to serialize on a
	 * separate lock before trying to get qdisc main lock.
	 * This permits qdisc->running owner to get the lock more
	 * often and dequeue packets faster.
	 * On PREEMPT_RT it is possible to preempt the qdisc owner during xmit
	 * and then other tasks will only enqueue packets. The packets will be
	 * sent after the qdisc owner is scheduled again. To prevent this
	 * scenario the task always serialize on the lock.
	 */
	contended = qdisc_is_running(q) || IS_ENABLED(CONFIG_PREEMPT_RT);
	if (unlikely(contended))
		spin_lock(&q->busylock);

	spin_lock(root_lock);
	if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED, &q->state))) {
		__qdisc_drop(skb, &to_free);
		rc = NET_XMIT_DROP;
	} else if ((q->flags & TCQ_F_CAN_BYPASS) && !qdisc_qlen(q) &&
		   qdisc_run_begin(q)) {
		/*
		 * This is a work-conserving queue; there are no old skbs
		 * waiting to be sent out; and the qdisc is not running -
		 * xmit the skb directly.
		 */

		qdisc_bstats_update(q, skb);

		if (sch_direct_xmit(skb, q, dev, txq, root_lock, true)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
		}

		qdisc_run_end(q);
		rc = NET_XMIT_SUCCESS;
	} else {
		WRITE_ONCE(q->owner, smp_processor_id());
		rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
		WRITE_ONCE(q->owner, -1);
		if (qdisc_run_begin(q)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
			qdisc_run_end(q);
		}
	}
	spin_unlock(root_lock);
	if (unlikely(to_free))
		kfree_skb_list_reason(to_free,
				      tcf_get_drop_reason(to_free));
	if (unlikely(contended))
		spin_unlock(&q->busylock);
	return rc;
}

#if IS_ENABLED(CONFIG_CGROUP_NET_PRIO)
static void skb_update_prio(struct sk_buff *skb)
{
	const struct netprio_map *map;
	const struct sock *sk;
	unsigned int prioidx;

	if (skb->priority)
		return;
	map = rcu_dereference_bh(skb->dev->priomap);
	if (!map)
		return;
	sk = skb_to_full_sk(skb);
	if (!sk)
		return;

	prioidx = sock_cgroup_prioidx(&sk->sk_cgrp_data);

	if (prioidx < map->priomap_len)
		skb->priority = map->priomap[prioidx];
}
#else
#define skb_update_prio(skb)
#endif

/**
 *	dev_loopback_xmit - loop back @skb
 *	@net: network namespace this loopback is happening in
 *	@sk:  sk needed to be a netfilter okfn
 *	@skb: buffer to transmit
 */
int dev_loopback_xmit(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb_reset_mac_header(skb);
	__skb_pull(skb, skb_network_offset(skb));
	skb->pkt_type = PACKET_LOOPBACK;
	if (skb->ip_summed == CHECKSUM_NONE)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	DEBUG_NET_WARN_ON_ONCE(!skb_dst(skb));
	skb_dst_force(skb);
	netif_rx(skb);
	return 0;
}
EXPORT_SYMBOL(dev_loopback_xmit);

#ifdef CONFIG_NET_EGRESS
static struct netdev_queue *
netdev_tx_queue_mapping(struct net_device *dev, struct sk_buff *skb)
{
	int qm = skb_get_queue_mapping(skb);

	return netdev_get_tx_queue(dev, netdev_cap_txqueue(dev, qm));
}

#ifndef CONFIG_PREEMPT_RT
static bool netdev_xmit_txqueue_skipped(void)
{
	return __this_cpu_read(softnet_data.xmit.skip_txqueue);
}

void netdev_xmit_skip_txqueue(bool skip)
{
	__this_cpu_write(softnet_data.xmit.skip_txqueue, skip);
}
EXPORT_SYMBOL_GPL(netdev_xmit_skip_txqueue);

#else
static bool netdev_xmit_txqueue_skipped(void)
{
	return current->net_xmit.skip_txqueue;
}

void netdev_xmit_skip_txqueue(bool skip)
{
	current->net_xmit.skip_txqueue = skip;
}
EXPORT_SYMBOL_GPL(netdev_xmit_skip_txqueue);
#endif
#endif /* CONFIG_NET_EGRESS */

#ifdef CONFIG_NET_XGRESS
static int tc_run(struct tcx_entry *entry, struct sk_buff *skb,
		  enum skb_drop_reason *drop_reason)
{
	int ret = TC_ACT_UNSPEC;
#ifdef CONFIG_NET_CLS_ACT
	struct mini_Qdisc *miniq = rcu_dereference_bh(entry->miniq);
	struct tcf_result res;

	if (!miniq)
		return ret;

	/* Global bypass */
	if (!static_branch_likely(&tcf_sw_enabled_key))
		return ret;

	/* Block-wise bypass */
	if (tcf_block_bypass_sw(miniq->block))
		return ret;

	tc_skb_cb(skb)->mru = 0;
	tc_skb_cb(skb)->post_ct = false;
	tcf_set_drop_reason(skb, *drop_reason);

	mini_qdisc_bstats_cpu_update(miniq, skb);
	ret = tcf_classify(skb, miniq->block, miniq->filter_list, &res, false);
	/* Only tcf related quirks below. */
	switch (ret) {
	case TC_ACT_SHOT:
		*drop_reason = tcf_get_drop_reason(skb);
		mini_qdisc_qstats_cpu_drop(miniq);
		break;
	case TC_ACT_OK:
	case TC_ACT_RECLASSIFY:
		skb->tc_index = TC_H_MIN(res.classid);
		break;
	}
#endif /* CONFIG_NET_CLS_ACT */
	return ret;
}

static DEFINE_STATIC_KEY_FALSE(tcx_needed_key);

void tcx_inc(void)
{
	static_branch_inc(&tcx_needed_key);
}

void tcx_dec(void)
{
	static_branch_dec(&tcx_needed_key);
}

static __always_inline enum tcx_action_base
tcx_run(const struct bpf_mprog_entry *entry, struct sk_buff *skb,
	const bool needs_mac)
{
	const struct bpf_mprog_fp *fp;
	const struct bpf_prog *prog;
	int ret = TCX_NEXT;

	if (needs_mac)
		__skb_push(skb, skb->mac_len);
	bpf_mprog_foreach_prog(entry, fp, prog) {
		bpf_compute_data_pointers(skb);
		ret = bpf_prog_run(prog, skb);
		if (ret != TCX_NEXT)
			break;
	}
	if (needs_mac)
		__skb_pull(skb, skb->mac_len);
	return tcx_action_code(skb, ret);
}

static __always_inline struct sk_buff *
sch_handle_ingress(struct sk_buff *skb, struct packet_type **pt_prev, int *ret,
		   struct net_device *orig_dev, bool *another)
{
	struct bpf_mprog_entry *entry = rcu_dereference_bh(skb->dev->tcx_ingress);
	enum skb_drop_reason drop_reason = SKB_DROP_REASON_TC_INGRESS;
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	int sch_ret;

	if (!entry)
		return skb;

	bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);
	if (*pt_prev) {
		*ret = deliver_skb(skb, *pt_prev, orig_dev);
		*pt_prev = NULL;
	}

	qdisc_skb_cb(skb)->pkt_len = skb->len;
	tcx_set_ingress(skb, true);

	if (static_branch_unlikely(&tcx_needed_key)) {
		sch_ret = tcx_run(entry, skb, true);
		if (sch_ret != TC_ACT_UNSPEC)
			goto ingress_verdict;
	}
	sch_ret = tc_run(tcx_entry(entry), skb, &drop_reason);
ingress_verdict:
	switch (sch_ret) {
	case TC_ACT_REDIRECT:
		/* skb_mac_header check was done by BPF, so we can safely
		 * push the L2 header back before redirecting to another
		 * netdev.
		 */
		__skb_push(skb, skb->mac_len);
		if (skb_do_redirect(skb) == -EAGAIN) {
			__skb_pull(skb, skb->mac_len);
			*another = true;
			break;
		}
		*ret = NET_RX_SUCCESS;
		bpf_net_ctx_clear(bpf_net_ctx);
		return NULL;
	case TC_ACT_SHOT:
		kfree_skb_reason(skb, drop_reason);
		*ret = NET_RX_DROP;
		bpf_net_ctx_clear(bpf_net_ctx);
		return NULL;
	/* used by tc_run */
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
	case TC_ACT_TRAP:
		consume_skb(skb);
		fallthrough;
	case TC_ACT_CONSUMED:
		*ret = NET_RX_SUCCESS;
		bpf_net_ctx_clear(bpf_net_ctx);
		return NULL;
	}
	bpf_net_ctx_clear(bpf_net_ctx);

	return skb;
}

static __always_inline struct sk_buff *
sch_handle_egress(struct sk_buff *skb, int *ret, struct net_device *dev)
{
	struct bpf_mprog_entry *entry = rcu_dereference_bh(dev->tcx_egress);
	enum skb_drop_reason drop_reason = SKB_DROP_REASON_TC_EGRESS;
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	int sch_ret;

	if (!entry)
		return skb;

	bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);

	/* qdisc_skb_cb(skb)->pkt_len & tcx_set_ingress() was
	 * already set by the caller.
	 */
	if (static_branch_unlikely(&tcx_needed_key)) {
		sch_ret = tcx_run(entry, skb, false);
		if (sch_ret != TC_ACT_UNSPEC)
			goto egress_verdict;
	}
	sch_ret = tc_run(tcx_entry(entry), skb, &drop_reason);
egress_verdict:
	switch (sch_ret) {
	case TC_ACT_REDIRECT:
		/* No need to push/pop skb's mac_header here on egress! */
		skb_do_redirect(skb);
		*ret = NET_XMIT_SUCCESS;
		bpf_net_ctx_clear(bpf_net_ctx);
		return NULL;
	case TC_ACT_SHOT:
		kfree_skb_reason(skb, drop_reason);
		*ret = NET_XMIT_DROP;
		bpf_net_ctx_clear(bpf_net_ctx);
		return NULL;
	/* used by tc_run */
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
	case TC_ACT_TRAP:
		consume_skb(skb);
		fallthrough;
	case TC_ACT_CONSUMED:
		*ret = NET_XMIT_SUCCESS;
		bpf_net_ctx_clear(bpf_net_ctx);
		return NULL;
	}
	bpf_net_ctx_clear(bpf_net_ctx);

	return skb;
}
#else
static __always_inline struct sk_buff *
sch_handle_ingress(struct sk_buff *skb, struct packet_type **pt_prev, int *ret,
		   struct net_device *orig_dev, bool *another)
{
	return skb;
}

static __always_inline struct sk_buff *
sch_handle_egress(struct sk_buff *skb, int *ret, struct net_device *dev)
{
	return skb;
}
#endif /* CONFIG_NET_XGRESS */

#ifdef CONFIG_XPS
static int __get_xps_queue_idx(struct net_device *dev, struct sk_buff *skb,
			       struct xps_dev_maps *dev_maps, unsigned int tci)
{
	int tc = netdev_get_prio_tc_map(dev, skb->priority);
	struct xps_map *map;
	int queue_index = -1;

	if (tc >= dev_maps->num_tc || tci >= dev_maps->nr_ids)
		return queue_index;

	tci *= dev_maps->num_tc;
	tci += tc;

	map = rcu_dereference(dev_maps->attr_map[tci]);
	if (map) {
		if (map->len == 1)
			queue_index = map->queues[0];
		else
			queue_index = map->queues[reciprocal_scale(
						skb_get_hash(skb), map->len)];
		if (unlikely(queue_index >= dev->real_num_tx_queues))
			queue_index = -1;
	}
	return queue_index;
}
#endif

static int get_xps_queue(struct net_device *dev, struct net_device *sb_dev,
			 struct sk_buff *skb)
{
#ifdef CONFIG_XPS
	struct xps_dev_maps *dev_maps;
	struct sock *sk = skb->sk;
	int queue_index = -1;

	if (!static_key_false(&xps_needed))
		return -1;

	rcu_read_lock();
	if (!static_key_false(&xps_rxqs_needed))
		goto get_cpus_map;

	dev_maps = rcu_dereference(sb_dev->xps_maps[XPS_RXQS]);
	if (dev_maps) {
		int tci = sk_rx_queue_get(sk);

		if (tci >= 0)
			queue_index = __get_xps_queue_idx(dev, skb, dev_maps,
							  tci);
	}

get_cpus_map:
	if (queue_index < 0) {
		dev_maps = rcu_dereference(sb_dev->xps_maps[XPS_CPUS]);
		if (dev_maps) {
			unsigned int tci = skb->sender_cpu - 1;

			queue_index = __get_xps_queue_idx(dev, skb, dev_maps,
							  tci);
		}
	}
	rcu_read_unlock();

	return queue_index;
#else
	return -1;
#endif
}

u16 dev_pick_tx_zero(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev)
{
	return 0;
}
EXPORT_SYMBOL(dev_pick_tx_zero);

u16 netdev_pick_tx(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev)
{
	struct sock *sk = skb->sk;
	int queue_index = sk_tx_queue_get(sk);

	sb_dev = sb_dev ? : dev;

	if (queue_index < 0 || skb->ooo_okay ||
	    queue_index >= dev->real_num_tx_queues) {
		int new_index = get_xps_queue(dev, sb_dev, skb);

		if (new_index < 0)
			new_index = skb_tx_hash(dev, sb_dev, skb);

		if (queue_index != new_index && sk &&
		    sk_fullsock(sk) &&
		    rcu_access_pointer(sk->sk_dst_cache))
			sk_tx_queue_set(sk, new_index);

		queue_index = new_index;
	}

	return queue_index;
}
EXPORT_SYMBOL(netdev_pick_tx);

struct netdev_queue *netdev_core_pick_tx(struct net_device *dev,
					 struct sk_buff *skb,
					 struct net_device *sb_dev)
{
	int queue_index = 0;

#ifdef CONFIG_XPS
	u32 sender_cpu = skb->sender_cpu - 1;

	if (sender_cpu >= (u32)NR_CPUS)
		skb->sender_cpu = raw_smp_processor_id() + 1;
#endif

	if (dev->real_num_tx_queues != 1) {
		const struct net_device_ops *ops = dev->netdev_ops;

		if (ops->ndo_select_queue)
			queue_index = ops->ndo_select_queue(dev, skb, sb_dev);
		else
			queue_index = netdev_pick_tx(dev, skb, sb_dev);

		queue_index = netdev_cap_txqueue(dev, queue_index);
	}

	skb_set_queue_mapping(skb, queue_index);
	return netdev_get_tx_queue(dev, queue_index);
}

/**
 * __dev_queue_xmit() - transmit a buffer
 * @skb:	buffer to transmit
 * @sb_dev:	suboordinate device used for L2 forwarding offload
 *
 * Queue a buffer for transmission to a network device. The caller must
 * have set the device and priority and built the buffer before calling
 * this function. The function can be called from an interrupt.
 *
 * When calling this method, interrupts MUST be enabled. This is because
 * the BH enable code must have IRQs enabled so that it will not deadlock.
 *
 * Regardless of the return value, the skb is consumed, so it is currently
 * difficult to retry a send to this method. (You can bump the ref count
 * before sending to hold a reference for retry if you are careful.)
 *
 * Return:
 * * 0				- buffer successfully transmitted
 * * positive qdisc return code	- NET_XMIT_DROP etc.
 * * negative errno		- other errors
 */
int __dev_queue_xmit(struct sk_buff *skb, struct net_device *sb_dev)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq = NULL;
	struct Qdisc *q;
	int rc = -ENOMEM;
	bool again = false;

	skb_reset_mac_header(skb);
	skb_assert_len(skb);

	if (unlikely(skb_shinfo(skb)->tx_flags &
		     (SKBTX_SCHED_TSTAMP | SKBTX_BPF)))
		__skb_tstamp_tx(skb, NULL, NULL, skb->sk, SCM_TSTAMP_SCHED);

	/* Disable soft irqs for various locks below. Also
	 * stops preemption for RCU.
	 */
	rcu_read_lock_bh();

	skb_update_prio(skb);

	qdisc_pkt_len_init(skb);
	tcx_set_ingress(skb, false);
#ifdef CONFIG_NET_EGRESS
	if (static_branch_unlikely(&egress_needed_key)) {
		if (nf_hook_egress_active()) {
			skb = nf_hook_egress(skb, &rc, dev);
			if (!skb)
				goto out;
		}

		netdev_xmit_skip_txqueue(false);

		nf_skip_egress(skb, true);
		skb = sch_handle_egress(skb, &rc, dev);
		if (!skb)
			goto out;
		nf_skip_egress(skb, false);

		if (netdev_xmit_txqueue_skipped())
			txq = netdev_tx_queue_mapping(dev, skb);
	}
#endif
	/* If device/qdisc don't need skb->dst, release it right now while
	 * its hot in this cpu cache.
	 */
	if (dev->priv_flags & IFF_XMIT_DST_RELEASE)
		skb_dst_drop(skb);
	else
		skb_dst_force(skb);

	if (!txq)
		txq = netdev_core_pick_tx(dev, skb, sb_dev);

	q = rcu_dereference_bh(txq->qdisc);

	trace_net_dev_queue(skb);
	if (q->enqueue) {
		rc = __dev_xmit_skb(skb, q, dev, txq);
		goto out;
	}

	/* The device has no queue. Common case for software devices:
	 * loopback, all the sorts of tunnels...

	 * Really, it is unlikely that netif_tx_lock protection is necessary
	 * here.  (f.e. loopback and IP tunnels are clean ignoring statistics
	 * counters.)
	 * However, it is possible, that they rely on protection
	 * made by us here.

	 * Check this and shot the lock. It is not prone from deadlocks.
	 *Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags & IFF_UP) {
		int cpu = smp_processor_id(); /* ok because BHs are off */

		/* Other cpus might concurrently change txq->xmit_lock_owner
		 * to -1 or to their cpu id, but not to our id.
		 */
		if (READ_ONCE(txq->xmit_lock_owner) != cpu) {
			if (dev_xmit_recursion())
				goto recursion_alert;

			skb = validate_xmit_skb(skb, dev, &again);
			if (!skb)
				goto out;

			HARD_TX_LOCK(dev, txq, cpu);

			if (!netif_xmit_stopped(txq)) {
				dev_xmit_recursion_inc();
				skb = dev_hard_start_xmit(skb, dev, txq, &rc);
				dev_xmit_recursion_dec();
				if (dev_xmit_complete(rc)) {
					HARD_TX_UNLOCK(dev, txq);
					goto out;
				}
			}
			HARD_TX_UNLOCK(dev, txq);
			net_crit_ratelimited("Virtual device %s asks to queue packet!\n",
					     dev->name);
		} else {
			/* Recursion is detected! It is possible,
			 * unfortunately
			 */
recursion_alert:
			net_crit_ratelimited("Dead loop on virtual device %s, fix it urgently!\n",
					     dev->name);
		}
	}

	rc = -ENETDOWN;
	rcu_read_unlock_bh();

	dev_core_stats_tx_dropped_inc(dev);
	kfree_skb_list(skb);
	return rc;
out:
	rcu_read_unlock_bh();
	return rc;
}
EXPORT_SYMBOL(__dev_queue_xmit);

int __dev_direct_xmit(struct sk_buff *skb, u16 queue_id)
{
	struct net_device *dev = skb->dev;
	struct sk_buff *orig_skb = skb;
	struct netdev_queue *txq;
	int ret = NETDEV_TX_BUSY;
	bool again = false;

	if (unlikely(!netif_running(dev) ||
		     !netif_carrier_ok(dev)))
		goto drop;

	skb = validate_xmit_skb_list(skb, dev, &again);
	if (skb != orig_skb)
		goto drop;

	skb_set_queue_mapping(skb, queue_id);
	txq = skb_get_tx_queue(dev, skb);

	local_bh_disable();

	dev_xmit_recursion_inc();
	HARD_TX_LOCK(dev, txq, smp_processor_id());
	if (!netif_xmit_frozen_or_drv_stopped(txq))
		ret = netdev_start_xmit(skb, dev, txq, false);
	HARD_TX_UNLOCK(dev, txq);
	dev_xmit_recursion_dec();

	local_bh_enable();
	return ret;
drop:
	dev_core_stats_tx_dropped_inc(dev);
	kfree_skb_list(skb);
	return NET_XMIT_DROP;
}
EXPORT_SYMBOL(__dev_direct_xmit);

/*************************************************************************
 *			Receiver routines
 *************************************************************************/
static DEFINE_PER_CPU(struct task_struct *, backlog_napi);

int weight_p __read_mostly = 64;           /* old backlog weight */
int dev_weight_rx_bias __read_mostly = 1;  /* bias for backlog weight */
int dev_weight_tx_bias __read_mostly = 1;  /* bias for output_queue quota */

/* Called with irq disabled */
static inline void ____napi_schedule(struct softnet_data *sd,
				     struct napi_struct *napi)
{
	struct task_struct *thread;

	lockdep_assert_irqs_disabled();

	if (test_bit(NAPI_STATE_THREADED, &napi->state)) {
		/* Paired with smp_mb__before_atomic() in
		 * napi_enable()/netif_set_threaded().
		 * Use READ_ONCE() to guarantee a complete
		 * read on napi->thread. Only call
		 * wake_up_process() when it's not NULL.
		 */
		thread = READ_ONCE(napi->thread);
		if (thread) {
			if (use_backlog_threads() && thread == raw_cpu_read(backlog_napi))
				goto use_local_napi;

			set_bit(NAPI_STATE_SCHED_THREADED, &napi->state);
			wake_up_process(thread);
			return;
		}
	}

use_local_napi:
	DEBUG_NET_WARN_ON_ONCE(!list_empty(&napi->poll_list));
	list_add_tail(&napi->poll_list, &sd->poll_list);
	WRITE_ONCE(napi->list_owner, smp_processor_id());
	/* If not called from net_rx_action()
	 * we have to raise NET_RX_SOFTIRQ.
	 */
	if (!sd->in_net_rx_action)
		raise_softirq_irqoff(NET_RX_SOFTIRQ);
}

#ifdef CONFIG_RPS

struct static_key_false rps_needed __read_mostly;
EXPORT_SYMBOL(rps_needed);
struct static_key_false rfs_needed __read_mostly;
EXPORT_SYMBOL(rfs_needed);

static u32 rfs_slot(u32 hash, const struct rps_dev_flow_table *flow_table)
{
	return hash_32(hash, flow_table->log);
}

static struct rps_dev_flow *
set_rps_cpu(struct net_device *dev, struct sk_buff *skb,
	    struct rps_dev_flow *rflow, u16 next_cpu)
{
	if (next_cpu < nr_cpu_ids) {
		u32 head;
#ifdef CONFIG_RFS_ACCEL
		struct netdev_rx_queue *rxqueue;
		struct rps_dev_flow_table *flow_table;
		struct rps_dev_flow *old_rflow;
		u16 rxq_index;
		u32 flow_id;
		int rc;

		/* Should we steer this flow to a different hardware queue? */
		if (!skb_rx_queue_recorded(skb) || !dev->rx_cpu_rmap ||
		    !(dev->features & NETIF_F_NTUPLE))
			goto out;
		rxq_index = cpu_rmap_lookup_index(dev->rx_cpu_rmap, next_cpu);
		if (rxq_index == skb_get_rx_queue(skb))
			goto out;

		rxqueue = dev->_rx + rxq_index;
		flow_table = rcu_dereference(rxqueue->rps_flow_table);
		if (!flow_table)
			goto out;
		flow_id = rfs_slot(skb_get_hash(skb), flow_table);
		rc = dev->netdev_ops->ndo_rx_flow_steer(dev, skb,
							rxq_index, flow_id);
		if (rc < 0)
			goto out;
		old_rflow = rflow;
		rflow = &flow_table->flows[flow_id];
		WRITE_ONCE(rflow->filter, rc);
		if (old_rflow->filter == rc)
			WRITE_ONCE(old_rflow->filter, RPS_NO_FILTER);
	out:
#endif
		head = READ_ONCE(per_cpu(softnet_data, next_cpu).input_queue_head);
		rps_input_queue_tail_save(&rflow->last_qtail, head);
	}

	WRITE_ONCE(rflow->cpu, next_cpu);
	return rflow;
}

/*
 * get_rps_cpu is called from netif_receive_skb and returns the target
 * CPU from the RPS map of the receiving queue for a given skb.
 * rcu_read_lock must be held on entry.
 */
static int get_rps_cpu(struct net_device *dev, struct sk_buff *skb,
		       struct rps_dev_flow **rflowp)
{
	const struct rps_sock_flow_table *sock_flow_table;
	struct netdev_rx_queue *rxqueue = dev->_rx;
	struct rps_dev_flow_table *flow_table;
	struct rps_map *map;
	int cpu = -1;
	u32 tcpu;
	u32 hash;

	if (skb_rx_queue_recorded(skb)) {
		u16 index = skb_get_rx_queue(skb);

		if (unlikely(index >= dev->real_num_rx_queues)) {
			WARN_ONCE(dev->real_num_rx_queues > 1,
				  "%s received packet on queue %u, but number "
				  "of RX queues is %u\n",
				  dev->name, index, dev->real_num_rx_queues);
			goto done;
		}
		rxqueue += index;
	}

	/* Avoid computing hash if RFS/RPS is not active for this rxqueue */

	flow_table = rcu_dereference(rxqueue->rps_flow_table);
	map = rcu_dereference(rxqueue->rps_map);
	if (!flow_table && !map)
		goto done;

	skb_reset_network_header(skb);
	hash = skb_get_hash(skb);
	if (!hash)
		goto done;

	sock_flow_table = rcu_dereference(net_hotdata.rps_sock_flow_table);
	if (flow_table && sock_flow_table) {
		struct rps_dev_flow *rflow;
		u32 next_cpu;
		u32 ident;

		/* First check into global flow table if there is a match.
		 * This READ_ONCE() pairs with WRITE_ONCE() from rps_record_sock_flow().
		 */
		ident = READ_ONCE(sock_flow_table->ents[hash & sock_flow_table->mask]);
		if ((ident ^ hash) & ~net_hotdata.rps_cpu_mask)
			goto try_rps;

		next_cpu = ident & net_hotdata.rps_cpu_mask;

		/* OK, now we know there is a match,
		 * we can look at the local (per receive queue) flow table
		 */
		rflow = &flow_table->flows[rfs_slot(hash, flow_table)];
		tcpu = rflow->cpu;

		/*
		 * If the desired CPU (where last recvmsg was done) is
		 * different from current CPU (one in the rx-queue flow
		 * table entry), switch if one of the following holds:
		 *   - Current CPU is unset (>= nr_cpu_ids).
		 *   - Current CPU is offline.
		 *   - The current CPU's queue tail has advanced beyond the
		 *     last packet that was enqueued using this table entry.
		 *     This guarantees that all previous packets for the flow
		 *     have been dequeued, thus preserving in order delivery.
		 */
		if (unlikely(tcpu != next_cpu) &&
		    (tcpu >= nr_cpu_ids || !cpu_online(tcpu) ||
		     ((int)(READ_ONCE(per_cpu(softnet_data, tcpu).input_queue_head) -
		      rflow->last_qtail)) >= 0)) {
			tcpu = next_cpu;
			rflow = set_rps_cpu(dev, skb, rflow, next_cpu);
		}

		if (tcpu < nr_cpu_ids && cpu_online(tcpu)) {
			*rflowp = rflow;
			cpu = tcpu;
			goto done;
		}
	}

try_rps:

	if (map) {
		tcpu = map->cpus[reciprocal_scale(hash, map->len)];
		if (cpu_online(tcpu)) {
			cpu = tcpu;
			goto done;
		}
	}

done:
	return cpu;
}

#ifdef CONFIG_RFS_ACCEL

/**
 * rps_may_expire_flow - check whether an RFS hardware filter may be removed
 * @dev: Device on which the filter was set
 * @rxq_index: RX queue index
 * @flow_id: Flow ID passed to ndo_rx_flow_steer()
 * @filter_id: Filter ID returned by ndo_rx_flow_steer()
 *
 * Drivers that implement ndo_rx_flow_steer() should periodically call
 * this function for each installed filter and remove the filters for
 * which it returns %true.
 */
bool rps_may_expire_flow(struct net_device *dev, u16 rxq_index,
			 u32 flow_id, u16 filter_id)
{
	struct netdev_rx_queue *rxqueue = dev->_rx + rxq_index;
	struct rps_dev_flow_table *flow_table;
	struct rps_dev_flow *rflow;
	bool expire = true;
	unsigned int cpu;

	rcu_read_lock();
	flow_table = rcu_dereference(rxqueue->rps_flow_table);
	if (flow_table && flow_id < (1UL << flow_table->log)) {
		rflow = &flow_table->flows[flow_id];
		cpu = READ_ONCE(rflow->cpu);
		if (READ_ONCE(rflow->filter) == filter_id && cpu < nr_cpu_ids &&
		    ((int)(READ_ONCE(per_cpu(softnet_data, cpu).input_queue_head) -
			   READ_ONCE(rflow->last_qtail)) <
		     (int)(10 << flow_table->log)))
			expire = false;
	}
	rcu_read_unlock();
	return expire;
}
EXPORT_SYMBOL(rps_may_expire_flow);

#endif /* CONFIG_RFS_ACCEL */

/* Called from hardirq (IPI) context */
static void rps_trigger_softirq(void *data)
{
	struct softnet_data *sd = data;

	____napi_schedule(sd, &sd->backlog);
	/* Pairs with READ_ONCE() in softnet_seq_show() */
	WRITE_ONCE(sd->received_rps, sd->received_rps + 1);
}

#endif /* CONFIG_RPS */

/* Called from hardirq (IPI) context */
static void trigger_rx_softirq(void *data)
{
	struct softnet_data *sd = data;

	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
	smp_store_release(&sd->defer_ipi_scheduled, 0);
}

/*
 * After we queued a packet into sd->input_pkt_queue,
 * we need to make sure this queue is serviced soon.
 *
 * - If this is another cpu queue, link it to our rps_ipi_list,
 *   and make sure we will process rps_ipi_list from net_rx_action().
 *
 * - If this is our own queue, NAPI schedule our backlog.
 *   Note that this also raises NET_RX_SOFTIRQ.
 */
static void napi_schedule_rps(struct softnet_data *sd)
{
	struct softnet_data *mysd = this_cpu_ptr(&softnet_data);

#ifdef CONFIG_RPS
	if (sd != mysd) {
		if (use_backlog_threads()) {
			__napi_schedule_irqoff(&sd->backlog);
			return;
		}

		sd->rps_ipi_next = mysd->rps_ipi_list;
		mysd->rps_ipi_list = sd;

		/* If not called from net_rx_action() or napi_threaded_poll()
		 * we have to raise NET_RX_SOFTIRQ.
		 */
		if (!mysd->in_net_rx_action && !mysd->in_napi_threaded_poll)
			__raise_softirq_irqoff(NET_RX_SOFTIRQ);
		return;
	}
#endif /* CONFIG_RPS */
	__napi_schedule_irqoff(&mysd->backlog);
}

void kick_defer_list_purge(struct softnet_data *sd, unsigned int cpu)
{
	unsigned long flags;

	if (use_backlog_threads()) {
		backlog_lock_irq_save(sd, &flags);

		if (!__test_and_set_bit(NAPI_STATE_SCHED, &sd->backlog.state))
			__napi_schedule_irqoff(&sd->backlog);

		backlog_unlock_irq_restore(sd, &flags);

	} else if (!cmpxchg(&sd->defer_ipi_scheduled, 0, 1)) {
		smp_call_function_single_async(cpu, &sd->defer_csd);
	}
}

#ifdef CONFIG_NET_FLOW_LIMIT
int netdev_flow_limit_table_len __read_mostly = (1 << 12);
#endif

static bool skb_flow_limit(struct sk_buff *skb, unsigned int qlen)
{
#ifdef CONFIG_NET_FLOW_LIMIT
	struct sd_flow_limit *fl;
	struct softnet_data *sd;
	unsigned int old_flow, new_flow;

	if (qlen < (READ_ONCE(net_hotdata.max_backlog) >> 1))
		return false;

	sd = this_cpu_ptr(&softnet_data);

	rcu_read_lock();
	fl = rcu_dereference(sd->flow_limit);
	if (fl) {
		new_flow = hash_32(skb_get_hash(skb), fl->log_buckets);
		old_flow = fl->history[fl->history_head];
		fl->history[fl->history_head] = new_flow;

		fl->history_head++;
		fl->history_head &= FLOW_LIMIT_HISTORY - 1;

		if (likely(fl->buckets[old_flow]))
			fl->buckets[old_flow]--;

		if (++fl->buckets[new_flow] > (FLOW_LIMIT_HISTORY >> 1)) {
			/* Pairs with READ_ONCE() in softnet_seq_show() */
			WRITE_ONCE(fl->count, fl->count + 1);
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();
#endif
	return false;
}

/*
 * enqueue_to_backlog is called to queue an skb to a per CPU backlog
 * queue (may be a remote CPU queue).
 */
static int enqueue_to_backlog(struct sk_buff *skb, int cpu,
			      unsigned int *qtail)
{
	enum skb_drop_reason reason;
	struct softnet_data *sd;
	unsigned long flags;
	unsigned int qlen;
	int max_backlog;
	u32 tail;

	reason = SKB_DROP_REASON_DEV_READY;
	if (!netif_running(skb->dev))
		goto bad_dev;

	reason = SKB_DROP_REASON_CPU_BACKLOG;
	sd = &per_cpu(softnet_data, cpu);

	qlen = skb_queue_len_lockless(&sd->input_pkt_queue);
	max_backlog = READ_ONCE(net_hotdata.max_backlog);
	if (unlikely(qlen > max_backlog))
		goto cpu_backlog_drop;
	backlog_lock_irq_save(sd, &flags);
	qlen = skb_queue_len(&sd->input_pkt_queue);
	if (qlen <= max_backlog && !skb_flow_limit(skb, qlen)) {
		if (!qlen) {
			/* Schedule NAPI for backlog device. We can use
			 * non atomic operation as we own the queue lock.
			 */
			if (!__test_and_set_bit(NAPI_STATE_SCHED,
						&sd->backlog.state))
				napi_schedule_rps(sd);
		}
		__skb_queue_tail(&sd->input_pkt_queue, skb);
		tail = rps_input_queue_tail_incr(sd);
		backlog_unlock_irq_restore(sd, &flags);

		/* save the tail outside of the critical section */
		rps_input_queue_tail_save(qtail, tail);
		return NET_RX_SUCCESS;
	}

	backlog_unlock_irq_restore(sd, &flags);

cpu_backlog_drop:
	atomic_inc(&sd->dropped);
bad_dev:
	dev_core_stats_rx_dropped_inc(skb->dev);
	kfree_skb_reason(skb, reason);
	return NET_RX_DROP;
}

static struct netdev_rx_queue *netif_get_rxqueue(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct netdev_rx_queue *rxqueue;

	rxqueue = dev->_rx;

	if (skb_rx_queue_recorded(skb)) {
		u16 index = skb_get_rx_queue(skb);

		if (unlikely(index >= dev->real_num_rx_queues)) {
			WARN_ONCE(dev->real_num_rx_queues > 1,
				  "%s received packet on queue %u, but number "
				  "of RX queues is %u\n",
				  dev->name, index, dev->real_num_rx_queues);

			return rxqueue; /* Return first rxqueue */
		}
		rxqueue += index;
	}
	return rxqueue;
}

u32 bpf_prog_run_generic_xdp(struct sk_buff *skb, struct xdp_buff *xdp,
			     const struct bpf_prog *xdp_prog)
{
	void *orig_data, *orig_data_end, *hard_start;
	struct netdev_rx_queue *rxqueue;
	bool orig_bcast, orig_host;
	u32 mac_len, frame_sz;
	__be16 orig_eth_type;
	struct ethhdr *eth;
	u32 metalen, act;
	int off;

	/* The XDP program wants to see the packet starting at the MAC
	 * header.
	 */
	mac_len = skb->data - skb_mac_header(skb);
	hard_start = skb->data - skb_headroom(skb);

	/* SKB "head" area always have tailroom for skb_shared_info */
	frame_sz = (void *)skb_end_pointer(skb) - hard_start;
	frame_sz += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	rxqueue = netif_get_rxqueue(skb);
	xdp_init_buff(xdp, frame_sz, &rxqueue->xdp_rxq);
	xdp_prepare_buff(xdp, hard_start, skb_headroom(skb) - mac_len,
			 skb_headlen(skb) + mac_len, true);
	if (skb_is_nonlinear(skb)) {
		skb_shinfo(skb)->xdp_frags_size = skb->data_len;
		xdp_buff_set_frags_flag(xdp);
	} else {
		xdp_buff_clear_frags_flag(xdp);
	}

	orig_data_end = xdp->data_end;
	orig_data = xdp->data;
	eth = (struct ethhdr *)xdp->data;
	orig_host = ether_addr_equal_64bits(eth->h_dest, skb->dev->dev_addr);
	orig_bcast = is_multicast_ether_addr_64bits(eth->h_dest);
	orig_eth_type = eth->h_proto;

	act = bpf_prog_run_xdp(xdp_prog, xdp);

	/* check if bpf_xdp_adjust_head was used */
	off = xdp->data - orig_data;
	if (off) {
		if (off > 0)
			__skb_pull(skb, off);
		else if (off < 0)
			__skb_push(skb, -off);

		skb->mac_header += off;
		skb_reset_network_header(skb);
	}

	/* check if bpf_xdp_adjust_tail was used */
	off = xdp->data_end - orig_data_end;
	if (off != 0) {
		skb_set_tail_pointer(skb, xdp->data_end - xdp->data);
		skb->len += off; /* positive on grow, negative on shrink */
	}

	/* XDP frag metadata (e.g. nr_frags) are updated in eBPF helpers
	 * (e.g. bpf_xdp_adjust_tail), we need to update data_len here.
	 */
	if (xdp_buff_has_frags(xdp))
		skb->data_len = skb_shinfo(skb)->xdp_frags_size;
	else
		skb->data_len = 0;

	/* check if XDP changed eth hdr such SKB needs update */
	eth = (struct ethhdr *)xdp->data;
	if ((orig_eth_type != eth->h_proto) ||
	    (orig_host != ether_addr_equal_64bits(eth->h_dest,
						  skb->dev->dev_addr)) ||
	    (orig_bcast != is_multicast_ether_addr_64bits(eth->h_dest))) {
		__skb_push(skb, ETH_HLEN);
		skb->pkt_type = PACKET_HOST;
		skb->protocol = eth_type_trans(skb, skb->dev);
	}

	/* Redirect/Tx gives L2 packet, code that will reuse skb must __skb_pull
	 * before calling us again on redirect path. We do not call do_redirect
	 * as we leave that up to the caller.
	 *
	 * Caller is responsible for managing lifetime of skb (i.e. calling
	 * kfree_skb in response to actions it cannot handle/XDP_DROP).
	 */
	switch (act) {
	case XDP_REDIRECT:
	case XDP_TX:
		__skb_push(skb, mac_len);
		break;
	case XDP_PASS:
		metalen = xdp->data - xdp->data_meta;
		if (metalen)
			skb_metadata_set(skb, metalen);
		break;
	}

	return act;
}

static int
netif_skb_check_for_xdp(struct sk_buff **pskb, const struct bpf_prog *prog)
{
	struct sk_buff *skb = *pskb;
	int err, hroom, troom;

	local_lock_nested_bh(&system_page_pool.bh_lock);
	err = skb_cow_data_for_xdp(this_cpu_read(system_page_pool.pool), pskb, prog);
	local_unlock_nested_bh(&system_page_pool.bh_lock);
	if (!err)
		return 0;

	/* In case we have to go down the path and also linearize,
	 * then lets do the pskb_expand_head() work just once here.
	 */
	hroom = XDP_PACKET_HEADROOM - skb_headroom(skb);
	troom = skb->tail + skb->data_len - skb->end;
	err = pskb_expand_head(skb,
			       hroom > 0 ? ALIGN(hroom, NET_SKB_PAD) : 0,
			       troom > 0 ? troom + 128 : 0, GFP_ATOMIC);
	if (err)
		return err;

	return skb_linearize(skb);
}

static u32 netif_receive_generic_xdp(struct sk_buff **pskb,
				     struct xdp_buff *xdp,
				     const struct bpf_prog *xdp_prog)
{
	struct sk_buff *skb = *pskb;
	u32 mac_len, act = XDP_DROP;

	/* Reinjected packets coming from act_mirred or similar should
	 * not get XDP generic processing.
	 */
	if (skb_is_redirected(skb))
		return XDP_PASS;

	/* XDP packets must have sufficient headroom of XDP_PACKET_HEADROOM
	 * bytes. This is the guarantee that also native XDP provides,
	 * thus we need to do it here as well.
	 */
	mac_len = skb->data - skb_mac_header(skb);
	__skb_push(skb, mac_len);

	if (skb_cloned(skb) || skb_is_nonlinear(skb) ||
	    skb_headroom(skb) < XDP_PACKET_HEADROOM) {
		if (netif_skb_check_for_xdp(pskb, xdp_prog))
			goto do_drop;
	}

	__skb_pull(*pskb, mac_len);

	act = bpf_prog_run_generic_xdp(*pskb, xdp, xdp_prog);
	switch (act) {
	case XDP_REDIRECT:
	case XDP_TX:
	case XDP_PASS:
		break;
	default:
		bpf_warn_invalid_xdp_action((*pskb)->dev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception((*pskb)->dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
	do_drop:
		kfree_skb(*pskb);
		break;
	}

	return act;
}

/* When doing generic XDP we have to bypass the qdisc layer and the
 * network taps in order to match in-driver-XDP behavior. This also means
 * that XDP packets are able to starve other packets going through a qdisc,
 * and DDOS attacks will be more effective. In-driver-XDP use dedicated TX
 * queues, so they do not have this starvation issue.
 */
void generic_xdp_tx(struct sk_buff *skb, const struct bpf_prog *xdp_prog)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	bool free_skb = true;
	int cpu, rc;

	txq = netdev_core_pick_tx(dev, skb, NULL);
	cpu = smp_processor_id();
	HARD_TX_LOCK(dev, txq, cpu);
	if (!netif_xmit_frozen_or_drv_stopped(txq)) {
		rc = netdev_start_xmit(skb, dev, txq, 0);
		if (dev_xmit_complete(rc))
			free_skb = false;
	}
	HARD_TX_UNLOCK(dev, txq);
	if (free_skb) {
		trace_xdp_exception(dev, xdp_prog, XDP_TX);
		dev_core_stats_tx_dropped_inc(dev);
		kfree_skb(skb);
	}
}

static DEFINE_STATIC_KEY_FALSE(generic_xdp_needed_key);

int do_xdp_generic(const struct bpf_prog *xdp_prog, struct sk_buff **pskb)
{
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;

	if (xdp_prog) {
		struct xdp_buff xdp;
		u32 act;
		int err;

		bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);
		act = netif_receive_generic_xdp(pskb, &xdp, xdp_prog);
		if (act != XDP_PASS) {
			switch (act) {
			case XDP_REDIRECT:
				err = xdp_do_generic_redirect((*pskb)->dev, *pskb,
							      &xdp, xdp_prog);
				if (err)
					goto out_redir;
				break;
			case XDP_TX:
				generic_xdp_tx(*pskb, xdp_prog);
				break;
			}
			bpf_net_ctx_clear(bpf_net_ctx);
			return XDP_DROP;
		}
		bpf_net_ctx_clear(bpf_net_ctx);
	}
	return XDP_PASS;
out_redir:
	bpf_net_ctx_clear(bpf_net_ctx);
	kfree_skb_reason(*pskb, SKB_DROP_REASON_XDP);
	return XDP_DROP;
}
EXPORT_SYMBOL_GPL(do_xdp_generic);

static int netif_rx_internal(struct sk_buff *skb)
{
	int ret;

	net_timestamp_check(READ_ONCE(net_hotdata.tstamp_prequeue), skb);

	trace_netif_rx(skb);

#ifdef CONFIG_RPS
	if (static_branch_unlikely(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu;

		rcu_read_lock();

		cpu = get_rps_cpu(skb->dev, skb, &rflow);
		if (cpu < 0)
			cpu = smp_processor_id();

		ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);

		rcu_read_unlock();
	} else
#endif
	{
		unsigned int qtail;

		ret = enqueue_to_backlog(skb, smp_processor_id(), &qtail);
	}
	return ret;
}

/**
 *	__netif_rx	-	Slightly optimized version of netif_rx
 *	@skb: buffer to post
 *
 *	This behaves as netif_rx except that it does not disable bottom halves.
 *	As a result this function may only be invoked from the interrupt context
 *	(either hard or soft interrupt).
 */
int __netif_rx(struct sk_buff *skb)
{
	int ret;

	lockdep_assert_once(hardirq_count() | softirq_count());

	trace_netif_rx_entry(skb);
	ret = netif_rx_internal(skb);
	trace_netif_rx_exit(ret);
	return ret;
}
EXPORT_SYMBOL(__netif_rx);

/**
 *	netif_rx	-	post buffer to the network code
 *	@skb: buffer to post
 *
 *	This function receives a packet from a device driver and queues it for
 *	the upper (protocol) levels to process via the backlog NAPI device. It
 *	always succeeds. The buffer may be dropped during processing for
 *	congestion control or by the protocol layers.
 *	The network buffer is passed via the backlog NAPI device. Modern NIC
 *	driver should use NAPI and GRO.
 *	This function can used from interrupt and from process context. The
 *	caller from process context must not disable interrupts before invoking
 *	this function.
 *
 *	return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped)
 *
 */
int netif_rx(struct sk_buff *skb)
{
	bool need_bh_off = !(hardirq_count() | softirq_count());
	int ret;

	if (need_bh_off)
		local_bh_disable();
	trace_netif_rx_entry(skb);
	ret = netif_rx_internal(skb);
	trace_netif_rx_exit(ret);
	if (need_bh_off)
		local_bh_enable();
	return ret;
}
EXPORT_SYMBOL(netif_rx);

static __latent_entropy void net_tx_action(void)
{
	struct softnet_data *sd = this_cpu_ptr(&softnet_data);

	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_disable();
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_enable();

		while (clist) {
			struct sk_buff *skb = clist;

			clist = clist->next;

			WARN_ON(refcount_read(&skb->users));
			if (likely(get_kfree_skb_cb(skb)->reason == SKB_CONSUMED))
				trace_consume_skb(skb, net_tx_action);
			else
				trace_kfree_skb(skb, net_tx_action,
						get_kfree_skb_cb(skb)->reason, NULL);

			if (skb->fclone != SKB_FCLONE_UNAVAILABLE)
				__kfree_skb(skb);
			else
				__napi_kfree_skb(skb,
						 get_kfree_skb_cb(skb)->reason);
		}
	}

	if (sd->output_queue) {
		struct Qdisc *head;

		local_irq_disable();
		head = sd->output_queue;
		sd->output_queue = NULL;
		sd->output_queue_tailp = &sd->output_queue;
		local_irq_enable();

		rcu_read_lock();

		while (head) {
			struct Qdisc *q = head;
			spinlock_t *root_lock = NULL;

			head = head->next_sched;

			/* We need to make sure head->next_sched is read
			 * before clearing __QDISC_STATE_SCHED
			 */
			smp_mb__before_atomic();

			if (!(q->flags & TCQ_F_NOLOCK)) {
				root_lock = qdisc_lock(q);
				spin_lock(root_lock);
			} else if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED,
						     &q->state))) {
				/* There is a synchronize_net() between
				 * STATE_DEACTIVATED flag being set and
				 * qdisc_reset()/some_qdisc_is_busy() in
				 * dev_deactivate(), so we can safely bail out
				 * early here to avoid data race between
				 * qdisc_deactivate() and some_qdisc_is_busy()
				 * for lockless qdisc.
				 */
				clear_bit(__QDISC_STATE_SCHED, &q->state);
				continue;
			}

			clear_bit(__QDISC_STATE_SCHED, &q->state);
			qdisc_run(q);
			if (root_lock)
				spin_unlock(root_lock);
		}

		rcu_read_unlock();
	}

	xfrm_dev_backlog(sd);
}

#if IS_ENABLED(CONFIG_BRIDGE) && IS_ENABLED(CONFIG_ATM_LANE)
/* This hook is defined here for ATM LANE */
int (*br_fdb_test_addr_hook)(struct net_device *dev,
			     unsigned char *addr) __read_mostly;
EXPORT_SYMBOL_GPL(br_fdb_test_addr_hook);
#endif

/**
 *	netdev_is_rx_handler_busy - check if receive handler is registered
 *	@dev: device to check
 *
 *	Check if a receive handler is already registered for a given device.
 *	Return true if there one.
 *
 *	The caller must hold the rtnl_mutex.
 */
bool netdev_is_rx_handler_busy(struct net_device *dev)
{
	ASSERT_RTNL();
	return dev && rtnl_dereference(dev->rx_handler);
}
EXPORT_SYMBOL_GPL(netdev_is_rx_handler_busy);

/**
 *	netdev_rx_handler_register - register receive handler
 *	@dev: device to register a handler for
 *	@rx_handler: receive handler to register
 *	@rx_handler_data: data pointer that is used by rx handler
 *
 *	Register a receive handler for a device. This handler will then be
 *	called from __netif_receive_skb. A negative errno code is returned
 *	on a failure.
 *
 *	The caller must hold the rtnl_mutex.
 *
 *	For a general description of rx_handler, see enum rx_handler_result.
 */
int netdev_rx_handler_register(struct net_device *dev,
			       rx_handler_func_t *rx_handler,
			       void *rx_handler_data)
{
	if (netdev_is_rx_handler_busy(dev))
		return -EBUSY;

	if (dev->priv_flags & IFF_NO_RX_HANDLER)
		return -EINVAL;

	/* Note: rx_handler_data must be set before rx_handler */
	rcu_assign_pointer(dev->rx_handler_data, rx_handler_data);
	rcu_assign_pointer(dev->rx_handler, rx_handler);

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_rx_handler_register);

/**
 *	netdev_rx_handler_unregister - unregister receive handler
 *	@dev: device to unregister a handler from
 *
 *	Unregister a receive handler from a device.
 *
 *	The caller must hold the rtnl_mutex.
 */
void netdev_rx_handler_unregister(struct net_device *dev)
{

	ASSERT_RTNL();
	RCU_INIT_POINTER(dev->rx_handler, NULL);
	/* a reader seeing a non NULL rx_handler in a rcu_read_lock()
	 * section has a guarantee to see a non NULL rx_handler_data
	 * as well.
	 */
	synchronize_net();
	RCU_INIT_POINTER(dev->rx_handler_data, NULL);
}
EXPORT_SYMBOL_GPL(netdev_rx_handler_unregister);

/*
 * Limit the use of PFMEMALLOC reserves to those protocols that implement
 * the special handling of PFMEMALLOC skbs.
 */
static bool skb_pfmemalloc_protocol(struct sk_buff *skb)
{
	switch (skb->protocol) {
	case htons(ETH_P_ARP):
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
	case htons(ETH_P_8021Q):
	case htons(ETH_P_8021AD):
		return true;
	default:
		return false;
	}
}

static inline int nf_ingress(struct sk_buff *skb, struct packet_type **pt_prev,
			     int *ret, struct net_device *orig_dev)
{
	if (nf_hook_ingress_active(skb)) {
		int ingress_retval;

		if (*pt_prev) {
			*ret = deliver_skb(skb, *pt_prev, orig_dev);
			*pt_prev = NULL;
		}

		rcu_read_lock();
		ingress_retval = nf_hook_ingress(skb);
		rcu_read_unlock();
		return ingress_retval;
	}
	return 0;
}

static int __netif_receive_skb_core(struct sk_buff **pskb, bool pfmemalloc,
				    struct packet_type **ppt_prev)
{
	enum skb_drop_reason drop_reason = SKB_DROP_REASON_UNHANDLED_PROTO;
	struct packet_type *ptype, *pt_prev;
	rx_handler_func_t *rx_handler;
	struct sk_buff *skb = *pskb;
	struct net_device *orig_dev;
	bool deliver_exact = false;
	int ret = NET_RX_DROP;
	__be16 type;

	net_timestamp_check(!READ_ONCE(net_hotdata.tstamp_prequeue), skb);

	trace_netif_receive_skb(skb);

	orig_dev = skb->dev;

	skb_reset_network_header(skb);
#if !defined(CONFIG_DEBUG_NET)
	/* We plan to no longer reset the transport header here.
	 * Give some time to fuzzers and dev build to catch bugs
	 * in network stacks.
	 */
	if (!skb_transport_header_was_set(skb))
		skb_reset_transport_header(skb);
#endif
	skb_reset_mac_len(skb);

	pt_prev = NULL;

another_round:
	skb->skb_iif = skb->dev->ifindex;

	__this_cpu_inc(softnet_data.processed);

	if (static_branch_unlikely(&generic_xdp_needed_key)) {
		int ret2;

		migrate_disable();
		ret2 = do_xdp_generic(rcu_dereference(skb->dev->xdp_prog),
				      &skb);
		migrate_enable();

		if (ret2 != XDP_PASS) {
			ret = NET_RX_DROP;
			goto out;
		}
	}

	if (eth_type_vlan(skb->protocol)) {
		skb = skb_vlan_untag(skb);
		if (unlikely(!skb))
			goto out;
	}

	if (skb_skip_tc_classify(skb))
		goto skip_classify;

	if (pfmemalloc)
		goto skip_taps;

	list_for_each_entry_rcu(ptype, &dev_net_rcu(skb->dev)->ptype_all,
				list) {
		if (pt_prev)
			ret = deliver_skb(skb, pt_prev, orig_dev);
		pt_prev = ptype;
	}

	list_for_each_entry_rcu(ptype, &skb->dev->ptype_all, list) {
		if (pt_prev)
			ret = deliver_skb(skb, pt_prev, orig_dev);
		pt_prev = ptype;
	}

skip_taps:
#ifdef CONFIG_NET_INGRESS
	if (static_branch_unlikely(&ingress_needed_key)) {
		bool another = false;

		nf_skip_egress(skb, true);
		skb = sch_handle_ingress(skb, &pt_prev, &ret, orig_dev,
					 &another);
		if (another)
			goto another_round;
		if (!skb)
			goto out;

		nf_skip_egress(skb, false);
		if (nf_ingress(skb, &pt_prev, &ret, orig_dev) < 0)
			goto out;
	}
#endif
	skb_reset_redirect(skb);
skip_classify:
	if (pfmemalloc && !skb_pfmemalloc_protocol(skb)) {
		drop_reason = SKB_DROP_REASON_PFMEMALLOC;
		goto drop;
	}

	if (skb_vlan_tag_present(skb)) {
		if (pt_prev) {
			ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = NULL;
		}
		if (vlan_do_receive(&skb))
			goto another_round;
		else if (unlikely(!skb))
			goto out;
	}

	rx_handler = rcu_dereference(skb->dev->rx_handler);
	if (rx_handler) {
		if (pt_prev) {
			ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = NULL;
		}
		switch (rx_handler(&skb)) {
		case RX_HANDLER_CONSUMED:
			ret = NET_RX_SUCCESS;
			goto out;
		case RX_HANDLER_ANOTHER:
			goto another_round;
		case RX_HANDLER_EXACT:
			deliver_exact = true;
			break;
		case RX_HANDLER_PASS:
			break;
		default:
			BUG();
		}
	}

	if (unlikely(skb_vlan_tag_present(skb)) && !netdev_uses_dsa(skb->dev)) {
check_vlan_id:
		if (skb_vlan_tag_get_id(skb)) {
			/* Vlan id is non 0 and vlan_do_receive() above couldn't
			 * find vlan device.
			 */
			skb->pkt_type = PACKET_OTHERHOST;
		} else if (eth_type_vlan(skb->protocol)) {
			/* Outer header is 802.1P with vlan 0, inner header is
			 * 802.1Q or 802.1AD and vlan_do_receive() above could
			 * not find vlan dev for vlan id 0.
			 */
			__vlan_hwaccel_clear_tag(skb);
			skb = skb_vlan_untag(skb);
			if (unlikely(!skb))
				goto out;
			if (vlan_do_receive(&skb))
				/* After stripping off 802.1P header with vlan 0
				 * vlan dev is found for inner header.
				 */
				goto another_round;
			else if (unlikely(!skb))
				goto out;
			else
				/* We have stripped outer 802.1P vlan 0 header.
				 * But could not find vlan dev.
				 * check again for vlan id to set OTHERHOST.
				 */
				goto check_vlan_id;
		}
		/* Note: we might in the future use prio bits
		 * and set skb->priority like in vlan_do_receive()
		 * For the time being, just ignore Priority Code Point
		 */
		__vlan_hwaccel_clear_tag(skb);
	}

	type = skb->protocol;

	/* deliver only exact match when indicated */
	if (likely(!deliver_exact)) {
		deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
				       &ptype_base[ntohs(type) &
						   PTYPE_HASH_MASK]);

		/* orig_dev and skb->dev could belong to different netns;
		 * Even in such case we need to traverse only the list
		 * coming from skb->dev, as the ptype owner (packet socket)
		 * will use dev_net(skb->dev) to do namespace filtering.
		 */
		deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
				       &dev_net_rcu(skb->dev)->ptype_specific);
	}

	deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
			       &orig_dev->ptype_specific);

	if (unlikely(skb->dev != orig_dev)) {
		deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
				       &skb->dev->ptype_specific);
	}

	if (pt_prev) {
		*ppt_prev = pt_prev;
	} else {
drop:
		if (!deliver_exact)
			dev_core_stats_rx_dropped_inc(skb->dev);
		else
			dev_core_stats_rx_nohandler_inc(skb->dev);

		kfree_skb_reason(skb, drop_reason);
		/* Jamal, now you will not able to escape explaining
		 * me how you were going to use this. :-)
		 */
		ret = NET_RX_DROP;
	}

out:
	/* The invariant here is that if *ppt_prev is not NULL
	 * then skb should also be non-NULL.
	 *
	 * Apparently *ppt_prev assignment above holds this invariant due to
	 * skb dereferencing near it.
	 */
	*pskb = skb;
	return ret;
}

static int __netif_receive_skb_one_core(struct sk_buff *skb, bool pfmemalloc)
{
	struct net_device *orig_dev = skb->dev;
	struct packet_type *pt_prev = NULL;
	int ret;

	ret = __netif_receive_skb_core(&skb, pfmemalloc, &pt_prev);
	if (pt_prev)
		ret = INDIRECT_CALL_INET(pt_prev->func, ipv6_rcv, ip_rcv, skb,
					 skb->dev, pt_prev, orig_dev);
	return ret;
}

/**
 *	netif_receive_skb_core - special purpose version of netif_receive_skb
 *	@skb: buffer to process
 *
 *	More direct receive version of netif_receive_skb().  It should
 *	only be used by callers that have a need to skip RPS and Generic XDP.
 *	Caller must also take care of handling if ``(page_is_)pfmemalloc``.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 *
 *	Return values (usually ignored):
 *	NET_RX_SUCCESS: no congestion
 *	NET_RX_DROP: packet was dropped
 */
int netif_receive_skb_core(struct sk_buff *skb)
{
	int ret;

	rcu_read_lock();
	ret = __netif_receive_skb_one_core(skb, false);
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(netif_receive_skb_core);

static inline void __netif_receive_skb_list_ptype(struct list_head *head,
						  struct packet_type *pt_prev,
						  struct net_device *orig_dev)
{
	struct sk_buff *skb, *next;

	if (!pt_prev)
		return;
	if (list_empty(head))
		return;
	if (pt_prev->list_func != NULL)
		INDIRECT_CALL_INET(pt_prev->list_func, ipv6_list_rcv,
				   ip_list_rcv, head, pt_prev, orig_dev);
	else
		list_for_each_entry_safe(skb, next, head, list) {
			skb_list_del_init(skb);
			pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
		}
}

static void __netif_receive_skb_list_core(struct list_head *head, bool pfmemalloc)
{
	/* Fast-path assumptions:
	 * - There is no RX handler.
	 * - Only one packet_type matches.
	 * If either of these fails, we will end up doing some per-packet
	 * processing in-line, then handling the 'last ptype' for the whole
	 * sublist.  This can't cause out-of-order delivery to any single ptype,
	 * because the 'last ptype' must be constant across the sublist, and all
	 * other ptypes are handled per-packet.
	 */
	/* Current (common) ptype of sublist */
	struct packet_type *pt_curr = NULL;
	/* Current (common) orig_dev of sublist */
	struct net_device *od_curr = NULL;
	struct sk_buff *skb, *next;
	LIST_HEAD(sublist);

	list_for_each_entry_safe(skb, next, head, list) {
		struct net_device *orig_dev = skb->dev;
		struct packet_type *pt_prev = NULL;

		skb_list_del_init(skb);
		__netif_receive_skb_core(&skb, pfmemalloc, &pt_prev);
		if (!pt_prev)
			continue;
		if (pt_curr != pt_prev || od_curr != orig_dev) {
			/* dispatch old sublist */
			__netif_receive_skb_list_ptype(&sublist, pt_curr, od_curr);
			/* start new sublist */
			INIT_LIST_HEAD(&sublist);
			pt_curr = pt_prev;
			od_curr = orig_dev;
		}
		list_add_tail(&skb->list, &sublist);
	}

	/* dispatch final sublist */
	__netif_receive_skb_list_ptype(&sublist, pt_curr, od_curr);
}

static int __netif_receive_skb(struct sk_buff *skb)
{
	int ret;

	if (sk_memalloc_socks() && skb_pfmemalloc(skb)) {
		unsigned int noreclaim_flag;

		/*
		 * PFMEMALLOC skbs are special, they should
		 * - be delivered to SOCK_MEMALLOC sockets only
		 * - stay away from userspace
		 * - have bounded memory usage
		 *
		 * Use PF_MEMALLOC as this saves us from propagating the allocation
		 * context down to all allocation sites.
		 */
		noreclaim_flag = memalloc_noreclaim_save();
		ret = __netif_receive_skb_one_core(skb, true);
		memalloc_noreclaim_restore(noreclaim_flag);
	} else
		ret = __netif_receive_skb_one_core(skb, false);

	return ret;
}

static void __netif_receive_skb_list(struct list_head *head)
{
	unsigned long noreclaim_flag = 0;
	struct sk_buff *skb, *next;
	bool pfmemalloc = false; /* Is current sublist PF_MEMALLOC? */

	list_for_each_entry_safe(skb, next, head, list) {
		if ((sk_memalloc_socks() && skb_pfmemalloc(skb)) != pfmemalloc) {
			struct list_head sublist;

			/* Handle the previous sublist */
			list_cut_before(&sublist, head, &skb->list);
			if (!list_empty(&sublist))
				__netif_receive_skb_list_core(&sublist, pfmemalloc);
			pfmemalloc = !pfmemalloc;
			/* See comments in __netif_receive_skb */
			if (pfmemalloc)
				noreclaim_flag = memalloc_noreclaim_save();
			else
				memalloc_noreclaim_restore(noreclaim_flag);
		}
	}
	/* Handle the remaining sublist */
	if (!list_empty(head))
		__netif_receive_skb_list_core(head, pfmemalloc);
	/* Restore pflags */
	if (pfmemalloc)
		memalloc_noreclaim_restore(noreclaim_flag);
}

static int generic_xdp_install(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct bpf_prog *old = rtnl_dereference(dev->xdp_prog);
	struct bpf_prog *new = xdp->prog;
	int ret = 0;

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		rcu_assign_pointer(dev->xdp_prog, new);
		if (old)
			bpf_prog_put(old);

		if (old && !new) {
			static_branch_dec(&generic_xdp_needed_key);
		} else if (new && !old) {
			static_branch_inc(&generic_xdp_needed_key);
			netif_disable_lro(dev);
			dev_disable_gro_hw(dev);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int netif_receive_skb_internal(struct sk_buff *skb)
{
	int ret;

	net_timestamp_check(READ_ONCE(net_hotdata.tstamp_prequeue), skb);

	if (skb_defer_rx_timestamp(skb))
		return NET_RX_SUCCESS;

	rcu_read_lock();
#ifdef CONFIG_RPS
	if (static_branch_unlikely(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu = get_rps_cpu(skb->dev, skb, &rflow);

		if (cpu >= 0) {
			ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			rcu_read_unlock();
			return ret;
		}
	}
#endif
	ret = __netif_receive_skb(skb);
	rcu_read_unlock();
	return ret;
}

void netif_receive_skb_list_internal(struct list_head *head)
{
	struct sk_buff *skb, *next;
	LIST_HEAD(sublist);

	list_for_each_entry_safe(skb, next, head, list) {
		net_timestamp_check(READ_ONCE(net_hotdata.tstamp_prequeue),
				    skb);
		skb_list_del_init(skb);
		if (!skb_defer_rx_timestamp(skb))
			list_add_tail(&skb->list, &sublist);
	}
	list_splice_init(&sublist, head);

	rcu_read_lock();
#ifdef CONFIG_RPS
	if (static_branch_unlikely(&rps_needed)) {
		list_for_each_entry_safe(skb, next, head, list) {
			struct rps_dev_flow voidflow, *rflow = &voidflow;
			int cpu = get_rps_cpu(skb->dev, skb, &rflow);

			if (cpu >= 0) {
				/* Will be handled, remove from list */
				skb_list_del_init(skb);
				enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			}
		}
	}
#endif
	__netif_receive_skb_list(head);
	rcu_read_unlock();
}

/**
 *	netif_receive_skb - process receive buffer from network
 *	@skb: buffer to process
 *
 *	netif_receive_skb() is the main receive data processing function.
 *	It always succeeds. The buffer may be dropped during processing
 *	for congestion control or by the protocol layers.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 *
 *	Return values (usually ignored):
 *	NET_RX_SUCCESS: no congestion
 *	NET_RX_DROP: packet was dropped
 */
int netif_receive_skb(struct sk_buff *skb)
{
	int ret;

	trace_netif_receive_skb_entry(skb);

	ret = netif_receive_skb_internal(skb);
	trace_netif_receive_skb_exit(ret);

	return ret;
}
EXPORT_SYMBOL(netif_receive_skb);

/**
 *	netif_receive_skb_list - process many receive buffers from network
 *	@head: list of skbs to process.
 *
 *	Since return value of netif_receive_skb() is normally ignored, and
 *	wouldn't be meaningful for a list, this function returns void.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 */
void netif_receive_skb_list(struct list_head *head)
{
	struct sk_buff *skb;

	if (list_empty(head))
		return;
	if (trace_netif_receive_skb_list_entry_enabled()) {
		list_for_each_entry(skb, head, list)
			trace_netif_receive_skb_list_entry(skb);
	}
	netif_receive_skb_list_internal(head);
	trace_netif_receive_skb_list_exit(0);
}
EXPORT_SYMBOL(netif_receive_skb_list);

/* Network device is going away, flush any packets still pending */
static void flush_backlog(struct work_struct *work)
{
	struct sk_buff *skb, *tmp;
	struct sk_buff_head list;
	struct softnet_data *sd;

	__skb_queue_head_init(&list);
	local_bh_disable();
	sd = this_cpu_ptr(&softnet_data);

	backlog_lock_irq_disable(sd);
	skb_queue_walk_safe(&sd->input_pkt_queue, skb, tmp) {
		if (READ_ONCE(skb->dev->reg_state) == NETREG_UNREGISTERING) {
			__skb_unlink(skb, &sd->input_pkt_queue);
			__skb_queue_tail(&list, skb);
			rps_input_queue_head_incr(sd);
		}
	}
	backlog_unlock_irq_enable(sd);

	local_lock_nested_bh(&softnet_data.process_queue_bh_lock);
	skb_queue_walk_safe(&sd->process_queue, skb, tmp) {
		if (READ_ONCE(skb->dev->reg_state) == NETREG_UNREGISTERING) {
			__skb_unlink(skb, &sd->process_queue);
			__skb_queue_tail(&list, skb);
			rps_input_queue_head_incr(sd);
		}
	}
	local_unlock_nested_bh(&softnet_data.process_queue_bh_lock);
	local_bh_enable();

	__skb_queue_purge_reason(&list, SKB_DROP_REASON_DEV_READY);
}

static bool flush_required(int cpu)
{
#if IS_ENABLED(CONFIG_RPS)
	struct softnet_data *sd = &per_cpu(softnet_data, cpu);
	bool do_flush;

	backlog_lock_irq_disable(sd);

	/* as insertion into process_queue happens with the rps lock held,
	 * process_queue access may race only with dequeue
	 */
	do_flush = !skb_queue_empty(&sd->input_pkt_queue) ||
		   !skb_queue_empty_lockless(&sd->process_queue);
	backlog_unlock_irq_enable(sd);

	return do_flush;
#endif
	/* without RPS we can't safely check input_pkt_queue: during a
	 * concurrent remote skb_queue_splice() we can detect as empty both
	 * input_pkt_queue and process_queue even if the latter could end-up
	 * containing a lot of packets.
	 */
	return true;
}

struct flush_backlogs {
	cpumask_t		flush_cpus;
	struct work_struct	w[];
};

static struct flush_backlogs *flush_backlogs_alloc(void)
{
	return kmalloc(struct_size_t(struct flush_backlogs, w, nr_cpu_ids),
		       GFP_KERNEL);
}

static struct flush_backlogs *flush_backlogs_fallback;
static DEFINE_MUTEX(flush_backlogs_mutex);

static void flush_all_backlogs(void)
{
	struct flush_backlogs *ptr = flush_backlogs_alloc();
	unsigned int cpu;

	if (!ptr) {
		mutex_lock(&flush_backlogs_mutex);
		ptr = flush_backlogs_fallback;
	}
	cpumask_clear(&ptr->flush_cpus);

	cpus_read_lock();

	for_each_online_cpu(cpu) {
		if (flush_required(cpu)) {
			INIT_WORK(&ptr->w[cpu], flush_backlog);
			queue_work_on(cpu, system_highpri_wq, &ptr->w[cpu]);
			__cpumask_set_cpu(cpu, &ptr->flush_cpus);
		}
	}

	/* we can have in flight packet[s] on the cpus we are not flushing,
	 * synchronize_net() in unregister_netdevice_many() will take care of
	 * them.
	 */
	for_each_cpu(cpu, &ptr->flush_cpus)
		flush_work(&ptr->w[cpu]);

	cpus_read_unlock();

	if (ptr != flush_backlogs_fallback)
		kfree(ptr);
	else
		mutex_unlock(&flush_backlogs_mutex);
}

static void net_rps_send_ipi(struct softnet_data *remsd)
{
#ifdef CONFIG_RPS
	while (remsd) {
		struct softnet_data *next = remsd->rps_ipi_next;

		if (cpu_online(remsd->cpu))
			smp_call_function_single_async(remsd->cpu, &remsd->csd);
		remsd = next;
	}
#endif
}

/*
 * net_rps_action_and_irq_enable sends any pending IPI's for rps.
 * Note: called with local irq disabled, but exits with local irq enabled.
 */
static void net_rps_action_and_irq_enable(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *remsd = sd->rps_ipi_list;

	if (!use_backlog_threads() && remsd) {
		sd->rps_ipi_list = NULL;

		local_irq_enable();

		/* Send pending IPI's to kick RPS processing on remote cpus. */
		net_rps_send_ipi(remsd);
	} else
#endif
		local_irq_enable();
}

static bool sd_has_rps_ipi_waiting(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	return !use_backlog_threads() && sd->rps_ipi_list;
#else
	return false;
#endif
}

static int process_backlog(struct napi_struct *napi, int quota)
{
	struct softnet_data *sd = container_of(napi, struct softnet_data, backlog);
	bool again = true;
	int work = 0;

	/* Check if we have pending ipi, its better to send them now,
	 * not waiting net_rx_action() end.
	 */
	if (sd_has_rps_ipi_waiting(sd)) {
		local_irq_disable();
		net_rps_action_and_irq_enable(sd);
	}

	napi->weight = READ_ONCE(net_hotdata.dev_rx_weight);
	while (again) {
		struct sk_buff *skb;

		local_lock_nested_bh(&softnet_data.process_queue_bh_lock);
		while ((skb = __skb_dequeue(&sd->process_queue))) {
			local_unlock_nested_bh(&softnet_data.process_queue_bh_lock);
			rcu_read_lock();
			__netif_receive_skb(skb);
			rcu_read_unlock();
			if (++work >= quota) {
				rps_input_queue_head_add(sd, work);
				return work;
			}

			local_lock_nested_bh(&softnet_data.process_queue_bh_lock);
		}
		local_unlock_nested_bh(&softnet_data.process_queue_bh_lock);

		backlog_lock_irq_disable(sd);
		if (skb_queue_empty(&sd->input_pkt_queue)) {
			/*
			 * Inline a custom version of __napi_complete().
			 * only current cpu owns and manipulates this napi,
			 * and NAPI_STATE_SCHED is the only possible flag set
			 * on backlog.
			 * We can use a plain write instead of clear_bit(),
			 * and we dont need an smp_mb() memory barrier.
			 */
			napi->state &= NAPIF_STATE_THREADED;
			again = false;
		} else {
			local_lock_nested_bh(&softnet_data.process_queue_bh_lock);
			skb_queue_splice_tail_init(&sd->input_pkt_queue,
						   &sd->process_queue);
			local_unlock_nested_bh(&softnet_data.process_queue_bh_lock);
		}
		backlog_unlock_irq_enable(sd);
	}

	if (work)
		rps_input_queue_head_add(sd, work);
	return work;
}

/**
 * __napi_schedule - schedule for receive
 * @n: entry to schedule
 *
 * The entry's receive function will be scheduled to run.
 * Consider using __napi_schedule_irqoff() if hard irqs are masked.
 */
void __napi_schedule(struct napi_struct *n)
{
	unsigned long flags;

	local_irq_save(flags);
	____napi_schedule(this_cpu_ptr(&softnet_data), n);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(__napi_schedule);

/**
 *	napi_schedule_prep - check if napi can be scheduled
 *	@n: napi context
 *
 * Test if NAPI routine is already running, and if not mark
 * it as running.  This is used as a condition variable to
 * insure only one NAPI poll instance runs.  We also make
 * sure there is no pending NAPI disable.
 */
bool napi_schedule_prep(struct napi_struct *n)
{
	unsigned long new, val = READ_ONCE(n->state);

	do {
		if (unlikely(val & NAPIF_STATE_DISABLE))
			return false;
		new = val | NAPIF_STATE_SCHED;

		/* Sets STATE_MISSED bit if STATE_SCHED was already set
		 * This was suggested by Alexander Duyck, as compiler
		 * emits better code than :
		 * if (val & NAPIF_STATE_SCHED)
		 *     new |= NAPIF_STATE_MISSED;
		 */
		new |= (val & NAPIF_STATE_SCHED) / NAPIF_STATE_SCHED *
						   NAPIF_STATE_MISSED;
	} while (!try_cmpxchg(&n->state, &val, new));

	return !(val & NAPIF_STATE_SCHED);
}
EXPORT_SYMBOL(napi_schedule_prep);

/**
 * __napi_schedule_irqoff - schedule for receive
 * @n: entry to schedule
 *
 * Variant of __napi_schedule() assuming hard irqs are masked.
 *
 * On PREEMPT_RT enabled kernels this maps to __napi_schedule()
 * because the interrupt disabled assumption might not be true
 * due to force-threaded interrupts and spinlock substitution.
 */
void __napi_schedule_irqoff(struct napi_struct *n)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		____napi_schedule(this_cpu_ptr(&softnet_data), n);
	else
		__napi_schedule(n);
}
EXPORT_SYMBOL(__napi_schedule_irqoff);

bool napi_complete_done(struct napi_struct *n, int work_done)
{
	unsigned long flags, val, new, timeout = 0;
	bool ret = true;

	/*
	 * 1) Don't let napi dequeue from the cpu poll list
	 *    just in case its running on a different cpu.
	 * 2) If we are busy polling, do nothing here, we have
	 *    the guarantee we will be called later.
	 */
	if (unlikely(n->state & (NAPIF_STATE_NPSVC |
				 NAPIF_STATE_IN_BUSY_POLL)))
		return false;

	if (work_done) {
		if (n->gro.bitmask)
			timeout = napi_get_gro_flush_timeout(n);
		n->defer_hard_irqs_count = napi_get_defer_hard_irqs(n);
	}
	if (n->defer_hard_irqs_count > 0) {
		n->defer_hard_irqs_count--;
		timeout = napi_get_gro_flush_timeout(n);
		if (timeout)
			ret = false;
	}

	/*
	 * When the NAPI instance uses a timeout and keeps postponing
	 * it, we need to bound somehow the time packets are kept in
	 * the GRO layer.
	 */
	gro_flush_normal(&n->gro, !!timeout);

	if (unlikely(!list_empty(&n->poll_list))) {
		/* If n->poll_list is not empty, we need to mask irqs */
		local_irq_save(flags);
		list_del_init(&n->poll_list);
		local_irq_restore(flags);
	}
	WRITE_ONCE(n->list_owner, -1);

	val = READ_ONCE(n->state);
	do {
		WARN_ON_ONCE(!(val & NAPIF_STATE_SCHED));

		new = val & ~(NAPIF_STATE_MISSED | NAPIF_STATE_SCHED |
			      NAPIF_STATE_SCHED_THREADED |
			      NAPIF_STATE_PREFER_BUSY_POLL);

		/* If STATE_MISSED was set, leave STATE_SCHED set,
		 * because we will call napi->poll() one more time.
		 * This C code was suggested by Alexander Duyck to help gcc.
		 */
		new |= (val & NAPIF_STATE_MISSED) / NAPIF_STATE_MISSED *
						    NAPIF_STATE_SCHED;
	} while (!try_cmpxchg(&n->state, &val, new));

	if (unlikely(val & NAPIF_STATE_MISSED)) {
		__napi_schedule(n);
		return false;
	}

	if (timeout)
		hrtimer_start(&n->timer, ns_to_ktime(timeout),
			      HRTIMER_MODE_REL_PINNED);
	return ret;
}
EXPORT_SYMBOL(napi_complete_done);

static void skb_defer_free_flush(struct softnet_data *sd)
{
	struct sk_buff *skb, *next;

	/* Paired with WRITE_ONCE() in skb_attempt_defer_free() */
	if (!READ_ONCE(sd->defer_list))
		return;

	spin_lock(&sd->defer_lock);
	skb = sd->defer_list;
	sd->defer_list = NULL;
	sd->defer_count = 0;
	spin_unlock(&sd->defer_lock);

	while (skb != NULL) {
		next = skb->next;
		napi_consume_skb(skb, 1);
		skb = next;
	}
}

#if defined(CONFIG_NET_RX_BUSY_POLL)

static void __busy_poll_stop(struct napi_struct *napi, bool skip_schedule)
{
	if (!skip_schedule) {
		gro_normal_list(&napi->gro);
		__napi_schedule(napi);
		return;
	}

	/* Flush too old packets. If HZ < 1000, flush all packets */
	gro_flush_normal(&napi->gro, HZ >= 1000);

	clear_bit(NAPI_STATE_SCHED, &napi->state);
}

enum {
	NAPI_F_PREFER_BUSY_POLL	= 1,
	NAPI_F_END_ON_RESCHED	= 2,
};

static void busy_poll_stop(struct napi_struct *napi, void *have_poll_lock,
			   unsigned flags, u16 budget)
{
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	bool skip_schedule = false;
	unsigned long timeout;
	int rc;

	/* Busy polling means there is a high chance device driver hard irq
	 * could not grab NAPI_STATE_SCHED, and that NAPI_STATE_MISSED was
	 * set in napi_schedule_prep().
	 * Since we are about to call napi->poll() once more, we can safely
	 * clear NAPI_STATE_MISSED.
	 *
	 * Note: x86 could use a single "lock and ..." instruction
	 * to perform these two clear_bit()
	 */
	clear_bit(NAPI_STATE_MISSED, &napi->state);
	clear_bit(NAPI_STATE_IN_BUSY_POLL, &napi->state);

	local_bh_disable();
	bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);

	if (flags & NAPI_F_PREFER_BUSY_POLL) {
		napi->defer_hard_irqs_count = napi_get_defer_hard_irqs(napi);
		timeout = napi_get_gro_flush_timeout(napi);
		if (napi->defer_hard_irqs_count && timeout) {
			hrtimer_start(&napi->timer, ns_to_ktime(timeout), HRTIMER_MODE_REL_PINNED);
			skip_schedule = true;
		}
	}

	/* All we really want here is to re-enable device interrupts.
	 * Ideally, a new ndo_busy_poll_stop() could avoid another round.
	 */
	rc = napi->poll(napi, budget);
	/* We can't gro_normal_list() here, because napi->poll() might have
	 * rearmed the napi (napi_complete_done()) in which case it could
	 * already be running on another CPU.
	 */
	trace_napi_poll(napi, rc, budget);
	netpoll_poll_unlock(have_poll_lock);
	if (rc == budget)
		__busy_poll_stop(napi, skip_schedule);
	bpf_net_ctx_clear(bpf_net_ctx);
	local_bh_enable();
}

static void __napi_busy_loop(unsigned int napi_id,
		      bool (*loop_end)(void *, unsigned long),
		      void *loop_end_arg, unsigned flags, u16 budget)
{
	unsigned long start_time = loop_end ? busy_loop_current_time() : 0;
	int (*napi_poll)(struct napi_struct *napi, int budget);
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	void *have_poll_lock = NULL;
	struct napi_struct *napi;

	WARN_ON_ONCE(!rcu_read_lock_held());

restart:
	napi_poll = NULL;

	napi = napi_by_id(napi_id);
	if (!napi)
		return;

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();
	for (;;) {
		int work = 0;

		local_bh_disable();
		bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);
		if (!napi_poll) {
			unsigned long val = READ_ONCE(napi->state);

			/* If multiple threads are competing for this napi,
			 * we avoid dirtying napi->state as much as we can.
			 */
			if (val & (NAPIF_STATE_DISABLE | NAPIF_STATE_SCHED |
				   NAPIF_STATE_IN_BUSY_POLL)) {
				if (flags & NAPI_F_PREFER_BUSY_POLL)
					set_bit(NAPI_STATE_PREFER_BUSY_POLL, &napi->state);
				goto count;
			}
			if (cmpxchg(&napi->state, val,
				    val | NAPIF_STATE_IN_BUSY_POLL |
					  NAPIF_STATE_SCHED) != val) {
				if (flags & NAPI_F_PREFER_BUSY_POLL)
					set_bit(NAPI_STATE_PREFER_BUSY_POLL, &napi->state);
				goto count;
			}
			have_poll_lock = netpoll_poll_lock(napi);
			napi_poll = napi->poll;
		}
		work = napi_poll(napi, budget);
		trace_napi_poll(napi, work, budget);
		gro_normal_list(&napi->gro);
count:
		if (work > 0)
			__NET_ADD_STATS(dev_net(napi->dev),
					LINUX_MIB_BUSYPOLLRXPACKETS, work);
		skb_defer_free_flush(this_cpu_ptr(&softnet_data));
		bpf_net_ctx_clear(bpf_net_ctx);
		local_bh_enable();

		if (!loop_end || loop_end(loop_end_arg, start_time))
			break;

		if (unlikely(need_resched())) {
			if (flags & NAPI_F_END_ON_RESCHED)
				break;
			if (napi_poll)
				busy_poll_stop(napi, have_poll_lock, flags, budget);
			if (!IS_ENABLED(CONFIG_PREEMPT_RT))
				preempt_enable();
			rcu_read_unlock();
			cond_resched();
			rcu_read_lock();
			if (loop_end(loop_end_arg, start_time))
				return;
			goto restart;
		}
		cpu_relax();
	}
	if (napi_poll)
		busy_poll_stop(napi, have_poll_lock, flags, budget);
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}

void napi_busy_loop_rcu(unsigned int napi_id,
			bool (*loop_end)(void *, unsigned long),
			void *loop_end_arg, bool prefer_busy_poll, u16 budget)
{
	unsigned flags = NAPI_F_END_ON_RESCHED;

	if (prefer_busy_poll)
		flags |= NAPI_F_PREFER_BUSY_POLL;

	__napi_busy_loop(napi_id, loop_end, loop_end_arg, flags, budget);
}

void napi_busy_loop(unsigned int napi_id,
		    bool (*loop_end)(void *, unsigned long),
		    void *loop_end_arg, bool prefer_busy_poll, u16 budget)
{
	unsigned flags = prefer_busy_poll ? NAPI_F_PREFER_BUSY_POLL : 0;

	rcu_read_lock();
	__napi_busy_loop(napi_id, loop_end, loop_end_arg, flags, budget);
	rcu_read_unlock();
}
EXPORT_SYMBOL(napi_busy_loop);

void napi_suspend_irqs(unsigned int napi_id)
{
	struct napi_struct *napi;

	rcu_read_lock();
	napi = napi_by_id(napi_id);
	if (napi) {
		unsigned long timeout = napi_get_irq_suspend_timeout(napi);

		if (timeout)
			hrtimer_start(&napi->timer, ns_to_ktime(timeout),
				      HRTIMER_MODE_REL_PINNED);
	}
	rcu_read_unlock();
}

void napi_resume_irqs(unsigned int napi_id)
{
	struct napi_struct *napi;

	rcu_read_lock();
	napi = napi_by_id(napi_id);
	if (napi) {
		/* If irq_suspend_timeout is set to 0 between the call to
		 * napi_suspend_irqs and now, the original value still
		 * determines the safety timeout as intended and napi_watchdog
		 * will resume irq processing.
		 */
		if (napi_get_irq_suspend_timeout(napi)) {
			local_bh_disable();
			napi_schedule(napi);
			local_bh_enable();
		}
	}
	rcu_read_unlock();
}

#endif /* CONFIG_NET_RX_BUSY_POLL */

static void __napi_hash_add_with_id(struct napi_struct *napi,
				    unsigned int napi_id)
{
	napi->gro.cached_napi_id = napi_id;

	WRITE_ONCE(napi->napi_id, napi_id);
	hlist_add_head_rcu(&napi->napi_hash_node,
			   &napi_hash[napi->napi_id % HASH_SIZE(napi_hash)]);
}

static void napi_hash_add_with_id(struct napi_struct *napi,
				  unsigned int napi_id)
{
	unsigned long flags;

	spin_lock_irqsave(&napi_hash_lock, flags);
	WARN_ON_ONCE(napi_by_id(napi_id));
	__napi_hash_add_with_id(napi, napi_id);
	spin_unlock_irqrestore(&napi_hash_lock, flags);
}

static void napi_hash_add(struct napi_struct *napi)
{
	unsigned long flags;

	if (test_bit(NAPI_STATE_NO_BUSY_POLL, &napi->state))
		return;

	spin_lock_irqsave(&napi_hash_lock, flags);

	/* 0..NR_CPUS range is reserved for sender_cpu use */
	do {
		if (unlikely(!napi_id_valid(++napi_gen_id)))
			napi_gen_id = MIN_NAPI_ID;
	} while (napi_by_id(napi_gen_id));

	__napi_hash_add_with_id(napi, napi_gen_id);

	spin_unlock_irqrestore(&napi_hash_lock, flags);
}

/* Warning : caller is responsible to make sure rcu grace period
 * is respected before freeing memory containing @napi
 */
static void napi_hash_del(struct napi_struct *napi)
{
	unsigned long flags;

	spin_lock_irqsave(&napi_hash_lock, flags);

	hlist_del_init_rcu(&napi->napi_hash_node);

	spin_unlock_irqrestore(&napi_hash_lock, flags);
}

static enum hrtimer_restart napi_watchdog(struct hrtimer *timer)
{
	struct napi_struct *napi;

	napi = container_of(timer, struct napi_struct, timer);

	/* Note : we use a relaxed variant of napi_schedule_prep() not setting
	 * NAPI_STATE_MISSED, since we do not react to a device IRQ.
	 */
	if (!napi_disable_pending(napi) &&
	    !test_and_set_bit(NAPI_STATE_SCHED, &napi->state)) {
		clear_bit(NAPI_STATE_PREFER_BUSY_POLL, &napi->state);
		__napi_schedule_irqoff(napi);
	}

	return HRTIMER_NORESTART;
}

static void napi_stop_kthread(struct napi_struct *napi)
{
	unsigned long val, new;

	/* Wait until the napi STATE_THREADED is unset. */
	while (true) {
		val = READ_ONCE(napi->state);

		/* If napi kthread own this napi or the napi is idle,
		 * STATE_THREADED can be unset here.
		 */
		if ((val & NAPIF_STATE_SCHED_THREADED) ||
		    !(val & NAPIF_STATE_SCHED)) {
			new = val & (~NAPIF_STATE_THREADED);
		} else {
			msleep(20);
			continue;
		}

		if (try_cmpxchg(&napi->state, &val, new))
			break;
	}

	/* Once STATE_THREADED is unset, wait for SCHED_THREADED to be unset by
	 * the kthread.
	 */
	while (true) {
		if (!test_bit(NAPIF_STATE_SCHED_THREADED, &napi->state))
			break;

		msleep(20);
	}

	kthread_stop(napi->thread);
	napi->thread = NULL;
}

int napi_set_threaded(struct napi_struct *napi,
		      enum netdev_napi_threaded threaded)
{
	if (threaded) {
		if (!napi->thread) {
			int err = napi_kthread_create(napi);

			if (err)
				return err;
		}
	}

	if (napi->config)
		napi->config->threaded = threaded;

	if (!threaded && napi->thread) {
		napi_stop_kthread(napi);
	} else {
		/* Make sure kthread is created before THREADED bit is set. */
		smp_mb__before_atomic();
		assign_bit(NAPI_STATE_THREADED, &napi->state, threaded);
	}

	return 0;
}

int netif_set_threaded(struct net_device *dev,
		       enum netdev_napi_threaded threaded)
{
	struct napi_struct *napi;
	int err = 0;

	netdev_assert_locked_or_invisible(dev);

	if (threaded) {
		list_for_each_entry(napi, &dev->napi_list, dev_list) {
			if (!napi->thread) {
				err = napi_kthread_create(napi);
				if (err) {
					threaded = NETDEV_NAPI_THREADED_DISABLED;
					break;
				}
			}
		}
	}

	WRITE_ONCE(dev->threaded, threaded);

	/* Make sure kthread is created before THREADED bit
	 * is set.
	 */
	smp_mb__before_atomic();

	/* Setting/unsetting threaded mode on a napi might not immediately
	 * take effect, if the current napi instance is actively being
	 * polled. In this case, the switch between threaded mode and
	 * softirq mode will happen in the next round of napi_schedule().
	 * This should not cause hiccups/stalls to the live traffic.
	 */
	list_for_each_entry(napi, &dev->napi_list, dev_list) {
		if (!threaded && napi->thread)
			napi_stop_kthread(napi);
		else
			assign_bit(NAPI_STATE_THREADED, &napi->state, threaded);
	}

	return err;
}

/**
 * netif_threaded_enable() - enable threaded NAPIs
 * @dev: net_device instance
 *
 * Enable threaded mode for the NAPI instances of the device. This may be useful
 * for devices where multiple NAPI instances get scheduled by a single
 * interrupt. Threaded NAPI allows moving the NAPI processing to cores other
 * than the core where IRQ is mapped.
 *
 * This function should be called before @dev is registered.
 */
void netif_threaded_enable(struct net_device *dev)
{
	WARN_ON_ONCE(netif_set_threaded(dev, NETDEV_NAPI_THREADED_ENABLED));
}
EXPORT_SYMBOL(netif_threaded_enable);

/**
 * netif_queue_set_napi - Associate queue with the napi
 * @dev: device to which NAPI and queue belong
 * @queue_index: Index of queue
 * @type: queue type as RX or TX
 * @napi: NAPI context, pass NULL to clear previously set NAPI
 *
 * Set queue with its corresponding napi context. This should be done after
 * registering the NAPI handler for the queue-vector and the queues have been
 * mapped to the corresponding interrupt vector.
 */
void netif_queue_set_napi(struct net_device *dev, unsigned int queue_index,
			  enum netdev_queue_type type, struct napi_struct *napi)
{
	struct netdev_rx_queue *rxq;
	struct netdev_queue *txq;

	if (WARN_ON_ONCE(napi && !napi->dev))
		return;
	netdev_ops_assert_locked_or_invisible(dev);

	switch (type) {
	case NETDEV_QUEUE_TYPE_RX:
		rxq = __netif_get_rx_queue(dev, queue_index);
		rxq->napi = napi;
		return;
	case NETDEV_QUEUE_TYPE_TX:
		txq = netdev_get_tx_queue(dev, queue_index);
		txq->napi = napi;
		return;
	default:
		return;
	}
}
EXPORT_SYMBOL(netif_queue_set_napi);

static void
netif_napi_irq_notify(struct irq_affinity_notify *notify,
		      const cpumask_t *mask)
{
	struct napi_struct *napi =
		container_of(notify, struct napi_struct, notify);
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap *rmap = napi->dev->rx_cpu_rmap;
	int err;
#endif

	if (napi->config && napi->dev->irq_affinity_auto)
		cpumask_copy(&napi->config->affinity_mask, mask);

#ifdef CONFIG_RFS_ACCEL
	if (napi->dev->rx_cpu_rmap_auto) {
		err = cpu_rmap_update(rmap, napi->napi_rmap_idx, mask);
		if (err)
			netdev_warn(napi->dev, "RMAP update failed (%d)\n",
				    err);
	}
#endif
}

#ifdef CONFIG_RFS_ACCEL
static void netif_napi_affinity_release(struct kref *ref)
{
	struct napi_struct *napi =
		container_of(ref, struct napi_struct, notify.kref);
	struct cpu_rmap *rmap = napi->dev->rx_cpu_rmap;

	netdev_assert_locked(napi->dev);
	WARN_ON(test_and_clear_bit(NAPI_STATE_HAS_NOTIFIER,
				   &napi->state));

	if (!napi->dev->rx_cpu_rmap_auto)
		return;
	rmap->obj[napi->napi_rmap_idx] = NULL;
	napi->napi_rmap_idx = -1;
	cpu_rmap_put(rmap);
}

int netif_enable_cpu_rmap(struct net_device *dev, unsigned int num_irqs)
{
	if (dev->rx_cpu_rmap_auto)
		return 0;

	dev->rx_cpu_rmap = alloc_irq_cpu_rmap(num_irqs);
	if (!dev->rx_cpu_rmap)
		return -ENOMEM;

	dev->rx_cpu_rmap_auto = true;
	return 0;
}
EXPORT_SYMBOL(netif_enable_cpu_rmap);

static void netif_del_cpu_rmap(struct net_device *dev)
{
	struct cpu_rmap *rmap = dev->rx_cpu_rmap;

	if (!dev->rx_cpu_rmap_auto)
		return;

	/* Free the rmap */
	cpu_rmap_put(rmap);
	dev->rx_cpu_rmap = NULL;
	dev->rx_cpu_rmap_auto = false;
}

#else
static void netif_napi_affinity_release(struct kref *ref)
{
}

int netif_enable_cpu_rmap(struct net_device *dev, unsigned int num_irqs)
{
	return 0;
}
EXPORT_SYMBOL(netif_enable_cpu_rmap);

static void netif_del_cpu_rmap(struct net_device *dev)
{
}
#endif

void netif_set_affinity_auto(struct net_device *dev)
{
	unsigned int i, maxqs, numa;

	maxqs = max(dev->num_tx_queues, dev->num_rx_queues);
	numa = dev_to_node(&dev->dev);

	for (i = 0; i < maxqs; i++)
		cpumask_set_cpu(cpumask_local_spread(i, numa),
				&dev->napi_config[i].affinity_mask);

	dev->irq_affinity_auto = true;
}
EXPORT_SYMBOL(netif_set_affinity_auto);

void netif_napi_set_irq_locked(struct napi_struct *napi, int irq)
{
	int rc;

	netdev_assert_locked_or_invisible(napi->dev);

	if (napi->irq == irq)
		return;

	/* Remove existing resources */
	if (test_and_clear_bit(NAPI_STATE_HAS_NOTIFIER, &napi->state))
		irq_set_affinity_notifier(napi->irq, NULL);

	napi->irq = irq;
	if (irq < 0 ||
	    (!napi->dev->rx_cpu_rmap_auto && !napi->dev->irq_affinity_auto))
		return;

	/* Abort for buggy drivers */
	if (napi->dev->irq_affinity_auto && WARN_ON_ONCE(!napi->config))
		return;

#ifdef CONFIG_RFS_ACCEL
	if (napi->dev->rx_cpu_rmap_auto) {
		rc = cpu_rmap_add(napi->dev->rx_cpu_rmap, napi);
		if (rc < 0)
			return;

		cpu_rmap_get(napi->dev->rx_cpu_rmap);
		napi->napi_rmap_idx = rc;
	}
#endif

	/* Use core IRQ notifier */
	napi->notify.notify = netif_napi_irq_notify;
	napi->notify.release = netif_napi_affinity_release;
	rc = irq_set_affinity_notifier(irq, &napi->notify);
	if (rc) {
		netdev_warn(napi->dev, "Unable to set IRQ notifier (%d)\n",
			    rc);
		goto put_rmap;
	}

	set_bit(NAPI_STATE_HAS_NOTIFIER, &napi->state);
	return;

put_rmap:
#ifdef CONFIG_RFS_ACCEL
	if (napi->dev->rx_cpu_rmap_auto) {
		napi->dev->rx_cpu_rmap->obj[napi->napi_rmap_idx] = NULL;
		cpu_rmap_put(napi->dev->rx_cpu_rmap);
		napi->napi_rmap_idx = -1;
	}
#endif
	napi->notify.notify = NULL;
	napi->notify.release = NULL;
}
EXPORT_SYMBOL(netif_napi_set_irq_locked);

static void napi_restore_config(struct napi_struct *n)
{
	n->defer_hard_irqs = n->config->defer_hard_irqs;
	n->gro_flush_timeout = n->config->gro_flush_timeout;
	n->irq_suspend_timeout = n->config->irq_suspend_timeout;

	if (n->dev->irq_affinity_auto &&
	    test_bit(NAPI_STATE_HAS_NOTIFIER, &n->state))
		irq_set_affinity(n->irq, &n->config->affinity_mask);

	/* a NAPI ID might be stored in the config, if so use it. if not, use
	 * napi_hash_add to generate one for us.
	 */
	if (n->config->napi_id) {
		napi_hash_add_with_id(n, n->config->napi_id);
	} else {
		napi_hash_add(n);
		n->config->napi_id = n->napi_id;
	}

	WARN_ON_ONCE(napi_set_threaded(n, n->config->threaded));
}

static void napi_save_config(struct napi_struct *n)
{
	n->config->defer_hard_irqs = n->defer_hard_irqs;
	n->config->gro_flush_timeout = n->gro_flush_timeout;
	n->config->irq_suspend_timeout = n->irq_suspend_timeout;
	napi_hash_del(n);
}

/* Netlink wants the NAPI list to be sorted by ID, if adding a NAPI which will
 * inherit an existing ID try to insert it at the right position.
 */
static void
netif_napi_dev_list_add(struct net_device *dev, struct napi_struct *napi)
{
	unsigned int new_id, pos_id;
	struct list_head *higher;
	struct napi_struct *pos;

	new_id = UINT_MAX;
	if (napi->config && napi->config->napi_id)
		new_id = napi->config->napi_id;

	higher = &dev->napi_list;
	list_for_each_entry(pos, &dev->napi_list, dev_list) {
		if (napi_id_valid(pos->napi_id))
			pos_id = pos->napi_id;
		else if (pos->config)
			pos_id = pos->config->napi_id;
		else
			pos_id = UINT_MAX;

		if (pos_id <= new_id)
			break;
		higher = &pos->dev_list;
	}
	list_add_rcu(&napi->dev_list, higher); /* adds after higher */
}

/* Double check that napi_get_frags() allocates skbs with
 * skb->head being backed by slab, not a page fragment.
 * This is to make sure bug fixed in 3226b158e67c
 * ("net: avoid 32 x truesize under-estimation for tiny skbs")
 * does not accidentally come back.
 */
static void napi_get_frags_check(struct napi_struct *napi)
{
	struct sk_buff *skb;

	local_bh_disable();
	skb = napi_get_frags(napi);
	WARN_ON_ONCE(skb && skb->head_frag);
	napi_free_frags(napi);
	local_bh_enable();
}

void netif_napi_add_weight_locked(struct net_device *dev,
				  struct napi_struct *napi,
				  int (*poll)(struct napi_struct *, int),
				  int weight)
{
	netdev_assert_locked(dev);
	if (WARN_ON(test_and_set_bit(NAPI_STATE_LISTED, &napi->state)))
		return;

	INIT_LIST_HEAD(&napi->poll_list);
	INIT_HLIST_NODE(&napi->napi_hash_node);
	hrtimer_setup(&napi->timer, napi_watchdog, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
	gro_init(&napi->gro);
	napi->skb = NULL;
	napi->poll = poll;
	if (weight > NAPI_POLL_WEIGHT)
		netdev_err_once(dev, "%s() called with weight %d\n", __func__,
				weight);
	napi->weight = weight;
	napi->dev = dev;
#ifdef CONFIG_NETPOLL
	napi->poll_owner = -1;
#endif
	napi->list_owner = -1;
	set_bit(NAPI_STATE_SCHED, &napi->state);
	set_bit(NAPI_STATE_NPSVC, &napi->state);
	netif_napi_dev_list_add(dev, napi);

	/* default settings from sysfs are applied to all NAPIs. any per-NAPI
	 * configuration will be loaded in napi_enable
	 */
	napi_set_defer_hard_irqs(napi, READ_ONCE(dev->napi_defer_hard_irqs));
	napi_set_gro_flush_timeout(napi, READ_ONCE(dev->gro_flush_timeout));

	napi_get_frags_check(napi);
	/* Create kthread for this napi if dev->threaded is set.
	 * Clear dev->threaded if kthread creation failed so that
	 * threaded mode will not be enabled in napi_enable().
	 */
	if (dev->threaded && napi_kthread_create(napi))
		dev->threaded = NETDEV_NAPI_THREADED_DISABLED;
	netif_napi_set_irq_locked(napi, -1);
}
EXPORT_SYMBOL(netif_napi_add_weight_locked);

void napi_disable_locked(struct napi_struct *n)
{
	unsigned long val, new;

	might_sleep();
	netdev_assert_locked(n->dev);

	set_bit(NAPI_STATE_DISABLE, &n->state);

	val = READ_ONCE(n->state);
	do {
		while (val & (NAPIF_STATE_SCHED | NAPIF_STATE_NPSVC)) {
			usleep_range(20, 200);
			val = READ_ONCE(n->state);
		}

		new = val | NAPIF_STATE_SCHED | NAPIF_STATE_NPSVC;
		new &= ~(NAPIF_STATE_THREADED | NAPIF_STATE_PREFER_BUSY_POLL);
	} while (!try_cmpxchg(&n->state, &val, new));

	hrtimer_cancel(&n->timer);

	if (n->config)
		napi_save_config(n);
	else
		napi_hash_del(n);

	clear_bit(NAPI_STATE_DISABLE, &n->state);
}
EXPORT_SYMBOL(napi_disable_locked);

/**
 * napi_disable() - prevent NAPI from scheduling
 * @n: NAPI context
 *
 * Stop NAPI from being scheduled on this context.
 * Waits till any outstanding processing completes.
 * Takes netdev_lock() for associated net_device.
 */
void napi_disable(struct napi_struct *n)
{
	netdev_lock(n->dev);
	napi_disable_locked(n);
	netdev_unlock(n->dev);
}
EXPORT_SYMBOL(napi_disable);

void napi_enable_locked(struct napi_struct *n)
{
	unsigned long new, val = READ_ONCE(n->state);

	if (n->config)
		napi_restore_config(n);
	else
		napi_hash_add(n);

	do {
		BUG_ON(!test_bit(NAPI_STATE_SCHED, &val));

		new = val & ~(NAPIF_STATE_SCHED | NAPIF_STATE_NPSVC);
		if (n->dev->threaded && n->thread)
			new |= NAPIF_STATE_THREADED;
	} while (!try_cmpxchg(&n->state, &val, new));
}
EXPORT_SYMBOL(napi_enable_locked);

/**
 * napi_enable() - enable NAPI scheduling
 * @n: NAPI context
 *
 * Enable scheduling of a NAPI instance.
 * Must be paired with napi_disable().
 * Takes netdev_lock() for associated net_device.
 */
void napi_enable(struct napi_struct *n)
{
	netdev_lock(n->dev);
	napi_enable_locked(n);
	netdev_unlock(n->dev);
}
EXPORT_SYMBOL(napi_enable);

/* Must be called in process context */
void __netif_napi_del_locked(struct napi_struct *napi)
{
	netdev_assert_locked(napi->dev);

	if (!test_and_clear_bit(NAPI_STATE_LISTED, &napi->state))
		return;

	/* Make sure NAPI is disabled (or was never enabled). */
	WARN_ON(!test_bit(NAPI_STATE_SCHED, &napi->state));

	if (test_and_clear_bit(NAPI_STATE_HAS_NOTIFIER, &napi->state))
		irq_set_affinity_notifier(napi->irq, NULL);

	if (napi->config) {
		napi->index = -1;
		napi->config = NULL;
	}

	list_del_rcu(&napi->dev_list);
	napi_free_frags(napi);

	gro_cleanup(&napi->gro);

	if (napi->thread) {
		kthread_stop(napi->thread);
		napi->thread = NULL;
	}
}
EXPORT_SYMBOL(__netif_napi_del_locked);

static int __napi_poll(struct napi_struct *n, bool *repoll)
{
	int work, weight;

	weight = n->weight;

	/* This NAPI_STATE_SCHED test is for avoiding a race
	 * with netpoll's poll_napi().  Only the entity which
	 * obtains the lock and sees NAPI_STATE_SCHED set will
	 * actually make the ->poll() call.  Therefore we avoid
	 * accidentally calling ->poll() when NAPI is not scheduled.
	 */
	work = 0;
	if (napi_is_scheduled(n)) {
		work = n->poll(n, weight);
		trace_napi_poll(n, work, weight);

		xdp_do_check_flushed(n);
	}

	if (unlikely(work > weight))
		netdev_err_once(n->dev, "NAPI poll function %pS returned %d, exceeding its budget of %d.\n",
				n->poll, work, weight);

	if (likely(work < weight))
		return work;

	/* Drivers must not modify the NAPI state if they
	 * consume the entire weight.  In such cases this code
	 * still "owns" the NAPI instance and therefore can
	 * move the instance around on the list at-will.
	 */
	if (unlikely(napi_disable_pending(n))) {
		napi_complete(n);
		return work;
	}

	/* The NAPI context has more processing work, but busy-polling
	 * is preferred. Exit early.
	 */
	if (napi_prefer_busy_poll(n)) {
		if (napi_complete_done(n, work)) {
			/* If timeout is not set, we need to make sure
			 * that the NAPI is re-scheduled.
			 */
			napi_schedule(n);
		}
		return work;
	}

	/* Flush too old packets. If HZ < 1000, flush all packets */
	gro_flush_normal(&n->gro, HZ >= 1000);

	/* Some drivers may have called napi_schedule
	 * prior to exhausting their budget.
	 */
	if (unlikely(!list_empty(&n->poll_list))) {
		pr_warn_once("%s: Budget exhausted after napi rescheduled\n",
			     n->dev ? n->dev->name : "backlog");
		return work;
	}

	*repoll = true;

	return work;
}

static int napi_poll(struct napi_struct *n, struct list_head *repoll)
{
	bool do_repoll = false;
	void *have;
	int work;

	list_del_init(&n->poll_list);

	have = netpoll_poll_lock(n);

	work = __napi_poll(n, &do_repoll);

	if (do_repoll) {
#if defined(CONFIG_DEBUG_NET)
		if (unlikely(!napi_is_scheduled(n)))
			pr_crit("repoll requested for device %s %ps but napi is not scheduled.\n",
				n->dev->name, n->poll);
#endif
		list_add_tail(&n->poll_list, repoll);
	}
	netpoll_poll_unlock(have);

	return work;
}

static int napi_thread_wait(struct napi_struct *napi)
{
	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		/* Testing SCHED_THREADED bit here to make sure the current
		 * kthread owns this napi and could poll on this napi.
		 * Testing SCHED bit is not enough because SCHED bit might be
		 * set by some other busy poll thread or by napi_disable().
		 */
		if (test_bit(NAPI_STATE_SCHED_THREADED, &napi->state)) {
			WARN_ON(!list_empty(&napi->poll_list));
			__set_current_state(TASK_RUNNING);
			return 0;
		}

		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

	return -1;
}

static void napi_threaded_poll_loop(struct napi_struct *napi)
{
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	struct softnet_data *sd;
	unsigned long last_qs = jiffies;

	for (;;) {
		bool repoll = false;
		void *have;

		local_bh_disable();
		bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);

		sd = this_cpu_ptr(&softnet_data);
		sd->in_napi_threaded_poll = true;

		have = netpoll_poll_lock(napi);
		__napi_poll(napi, &repoll);
		netpoll_poll_unlock(have);

		sd->in_napi_threaded_poll = false;
		barrier();

		if (sd_has_rps_ipi_waiting(sd)) {
			local_irq_disable();
			net_rps_action_and_irq_enable(sd);
		}
		skb_defer_free_flush(sd);
		bpf_net_ctx_clear(bpf_net_ctx);
		local_bh_enable();

		if (!repoll)
			break;

		rcu_softirq_qs_periodic(last_qs);
		cond_resched();
	}
}

static int napi_threaded_poll(void *data)
{
	struct napi_struct *napi = data;

	while (!napi_thread_wait(napi))
		napi_threaded_poll_loop(napi);

	return 0;
}

static __latent_entropy void net_rx_action(void)
{
	struct softnet_data *sd = this_cpu_ptr(&softnet_data);
	unsigned long time_limit = jiffies +
		usecs_to_jiffies(READ_ONCE(net_hotdata.netdev_budget_usecs));
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	int budget = READ_ONCE(net_hotdata.netdev_budget);
	LIST_HEAD(list);
	LIST_HEAD(repoll);

	bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);
start:
	sd->in_net_rx_action = true;
	local_irq_disable();
	list_splice_init(&sd->poll_list, &list);
	local_irq_enable();

	for (;;) {
		struct napi_struct *n;

		skb_defer_free_flush(sd);

		if (list_empty(&list)) {
			if (list_empty(&repoll)) {
				sd->in_net_rx_action = false;
				barrier();
				/* We need to check if ____napi_schedule()
				 * had refilled poll_list while
				 * sd->in_net_rx_action was true.
				 */
				if (!list_empty(&sd->poll_list))
					goto start;
				if (!sd_has_rps_ipi_waiting(sd))
					goto end;
			}
			break;
		}

		n = list_first_entry(&list, struct napi_struct, poll_list);
		budget -= napi_poll(n, &repoll);

		/* If softirq window is exhausted then punt.
		 * Allow this to run for 2 jiffies since which will allow
		 * an average latency of 1.5/HZ.
		 */
		if (unlikely(budget <= 0 ||
			     time_after_eq(jiffies, time_limit))) {
			/* Pairs with READ_ONCE() in softnet_seq_show() */
			WRITE_ONCE(sd->time_squeeze, sd->time_squeeze + 1);
			break;
		}
	}

	local_irq_disable();

	list_splice_tail_init(&sd->poll_list, &list);
	list_splice_tail(&repoll, &list);
	list_splice(&list, &sd->poll_list);
	if (!list_empty(&sd->poll_list))
		__raise_softirq_irqoff(NET_RX_SOFTIRQ);
	else
		sd->in_net_rx_action = false;

	net_rps_action_and_irq_enable(sd);
end:
	bpf_net_ctx_clear(bpf_net_ctx);
}

struct netdev_adjacent {
	struct net_device *dev;
	netdevice_tracker dev_tracker;

	/* upper master flag, there can only be one master device per list */
	bool master;

	/* lookup ignore flag */
	bool ignore;

	/* counter for the number of times this device was added to us */
	u16 ref_nr;

	/* private field for the users */
	void *private;

	struct list_head list;
	struct rcu_head rcu;
};

static struct netdev_adjacent *__netdev_find_adj(struct net_device *adj_dev,
						 struct list_head *adj_list)
{
	struct netdev_adjacent *adj;

	list_for_each_entry(adj, adj_list, list) {
		if (adj->dev == adj_dev)
			return adj;
	}
	return NULL;
}

static int ____netdev_has_upper_dev(struct net_device *upper_dev,
				    struct netdev_nested_priv *priv)
{
	struct net_device *dev = (struct net_device *)priv->data;

	return upper_dev == dev;
}

/**
 * netdev_has_upper_dev - Check if device is linked to an upper device
 * @dev: device
 * @upper_dev: upper device to check
 *
 * Find out if a device is linked to specified upper device and return true
 * in case it is. Note that this checks only immediate upper device,
 * not through a complete stack of devices. The caller must hold the RTNL lock.
 */
bool netdev_has_upper_dev(struct net_device *dev,
			  struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.data = (void *)upper_dev,
	};

	ASSERT_RTNL();

	return netdev_walk_all_upper_dev_rcu(dev, ____netdev_has_upper_dev,
					     &priv);
}
EXPORT_SYMBOL(netdev_has_upper_dev);

/**
 * netdev_has_upper_dev_all_rcu - Check if device is linked to an upper device
 * @dev: device
 * @upper_dev: upper device to check
 *
 * Find out if a device is linked to specified upper device and return true
 * in case it is. Note that this checks the entire upper device chain.
 * The caller must hold rcu lock.
 */

bool netdev_has_upper_dev_all_rcu(struct net_device *dev,
				  struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.data = (void *)upper_dev,
	};

	return !!netdev_walk_all_upper_dev_rcu(dev, ____netdev_has_upper_dev,
					       &priv);
}
EXPORT_SYMBOL(netdev_has_upper_dev_all_rcu);

/**
 * netdev_has_any_upper_dev - Check if device is linked to some device
 * @dev: device
 *
 * Find out if a device is linked to an upper device and return true in case
 * it is. The caller must hold the RTNL lock.
 */
bool netdev_has_any_upper_dev(struct net_device *dev)
{
	ASSERT_RTNL();

	return !list_empty(&dev->adj_list.upper);
}
EXPORT_SYMBOL(netdev_has_any_upper_dev);

/**
 * netdev_master_upper_dev_get - Get master upper device
 * @dev: device
 *
 * Find a master upper device and return pointer to it or NULL in case
 * it's not there. The caller must hold the RTNL lock.
 */
struct net_device *netdev_master_upper_dev_get(struct net_device *dev)
{
	struct netdev_adjacent *upper;

	ASSERT_RTNL();

	if (list_empty(&dev->adj_list.upper))
		return NULL;

	upper = list_first_entry(&dev->adj_list.upper,
				 struct netdev_adjacent, list);
	if (likely(upper->master))
		return upper->dev;
	return NULL;
}
EXPORT_SYMBOL(netdev_master_upper_dev_get);

static struct net_device *__netdev_master_upper_dev_get(struct net_device *dev)
{
	struct netdev_adjacent *upper;

	ASSERT_RTNL();

	if (list_empty(&dev->adj_list.upper))
		return NULL;

	upper = list_first_entry(&dev->adj_list.upper,
				 struct netdev_adjacent, list);
	if (likely(upper->master) && !upper->ignore)
		return upper->dev;
	return NULL;
}

/**
 * netdev_has_any_lower_dev - Check if device is linked to some device
 * @dev: device
 *
 * Find out if a device is linked to a lower device and return true in case
 * it is. The caller must hold the RTNL lock.
 */
static bool netdev_has_any_lower_dev(struct net_device *dev)
{
	ASSERT_RTNL();

	return !list_empty(&dev->adj_list.lower);
}

void *netdev_adjacent_get_private(struct list_head *adj_list)
{
	struct netdev_adjacent *adj;

	adj = list_entry(adj_list, struct netdev_adjacent, list);

	return adj->private;
}
EXPORT_SYMBOL(netdev_adjacent_get_private);

/**
 * netdev_upper_get_next_dev_rcu - Get the next dev from upper list
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next device from the dev's upper list, starting from iter
 * position. The caller must hold RCU read lock.
 */
struct net_device *netdev_upper_get_next_dev_rcu(struct net_device *dev,
						 struct list_head **iter)
{
	struct netdev_adjacent *upper;

	WARN_ON_ONCE(!rcu_read_lock_held() && !lockdep_rtnl_is_held());

	upper = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);

	if (&upper->list == &dev->adj_list.upper)
		return NULL;

	*iter = &upper->list;

	return upper->dev;
}
EXPORT_SYMBOL(netdev_upper_get_next_dev_rcu);

static struct net_device *__netdev_next_upper_dev(struct net_device *dev,
						  struct list_head **iter,
						  bool *ignore)
{
	struct netdev_adjacent *upper;

	upper = list_entry((*iter)->next, struct netdev_adjacent, list);

	if (&upper->list == &dev->adj_list.upper)
		return NULL;

	*iter = &upper->list;
	*ignore = upper->ignore;

	return upper->dev;
}

static struct net_device *netdev_next_upper_dev_rcu(struct net_device *dev,
						    struct list_head **iter)
{
	struct netdev_adjacent *upper;

	WARN_ON_ONCE(!rcu_read_lock_held() && !lockdep_rtnl_is_held());

	upper = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);

	if (&upper->list == &dev->adj_list.upper)
		return NULL;

	*iter = &upper->list;

	return upper->dev;
}

static int __netdev_walk_all_upper_dev(struct net_device *dev,
				       int (*fn)(struct net_device *dev,
					 struct netdev_nested_priv *priv),
				       struct netdev_nested_priv *priv)
{
	struct net_device *udev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;
	bool ignore;

	now = dev;
	iter = &dev->adj_list.upper;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			udev = __netdev_next_upper_dev(now, &iter, &ignore);
			if (!udev)
				break;
			if (ignore)
				continue;

			next = udev;
			niter = &udev->adj_list.upper;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}

int netdev_walk_all_upper_dev_rcu(struct net_device *dev,
				  int (*fn)(struct net_device *dev,
					    struct netdev_nested_priv *priv),
				  struct netdev_nested_priv *priv)
{
	struct net_device *udev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;

	now = dev;
	iter = &dev->adj_list.upper;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			udev = netdev_next_upper_dev_rcu(now, &iter);
			if (!udev)
				break;

			next = udev;
			niter = &udev->adj_list.upper;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_walk_all_upper_dev_rcu);

static bool __netdev_has_upper_dev(struct net_device *dev,
				   struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.flags = 0,
		.data = (void *)upper_dev,
	};

	ASSERT_RTNL();

	return __netdev_walk_all_upper_dev(dev, ____netdev_has_upper_dev,
					   &priv);
}

/**
 * netdev_lower_get_next_private - Get the next ->private from the
 *				   lower neighbour list
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next netdev_adjacent->private from the dev's lower neighbour
 * list, starting from iter position. The caller must hold either hold the
 * RTNL lock or its own locking that guarantees that the neighbour lower
 * list will remain unchanged.
 */
void *netdev_lower_get_next_private(struct net_device *dev,
				    struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry(*iter, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = lower->list.next;

	return lower->private;
}
EXPORT_SYMBOL(netdev_lower_get_next_private);

/**
 * netdev_lower_get_next_private_rcu - Get the next ->private from the
 *				       lower neighbour list, RCU
 *				       variant
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next netdev_adjacent->private from the dev's lower neighbour
 * list, starting from iter position. The caller must hold RCU read lock.
 */
void *netdev_lower_get_next_private_rcu(struct net_device *dev,
					struct list_head **iter)
{
	struct netdev_adjacent *lower;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());

	lower = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;

	return lower->private;
}
EXPORT_SYMBOL(netdev_lower_get_next_private_rcu);

/**
 * netdev_lower_get_next - Get the next device from the lower neighbour
 *                         list
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next netdev_adjacent from the dev's lower neighbour
 * list, starting from iter position. The caller must hold RTNL lock or
 * its own locking that guarantees that the neighbour lower
 * list will remain unchanged.
 */
void *netdev_lower_get_next(struct net_device *dev, struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry(*iter, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = lower->list.next;

	return lower->dev;
}
EXPORT_SYMBOL(netdev_lower_get_next);

static struct net_device *netdev_next_lower_dev(struct net_device *dev,
						struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry((*iter)->next, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;

	return lower->dev;
}

static struct net_device *__netdev_next_lower_dev(struct net_device *dev,
						  struct list_head **iter,
						  bool *ignore)
{
	struct netdev_adjacent *lower;

	lower = list_entry((*iter)->next, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;
	*ignore = lower->ignore;

	return lower->dev;
}

int netdev_walk_all_lower_dev(struct net_device *dev,
			      int (*fn)(struct net_device *dev,
					struct netdev_nested_priv *priv),
			      struct netdev_nested_priv *priv)
{
	struct net_device *ldev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;

	now = dev;
	iter = &dev->adj_list.lower;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			ldev = netdev_next_lower_dev(now, &iter);
			if (!ldev)
				break;

			next = ldev;
			niter = &ldev->adj_list.lower;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_walk_all_lower_dev);

static int __netdev_walk_all_lower_dev(struct net_device *dev,
				       int (*fn)(struct net_device *dev,
					 struct netdev_nested_priv *priv),
				       struct netdev_nested_priv *priv)
{
	struct net_device *ldev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;
	bool ignore;

	now = dev;
	iter = &dev->adj_list.lower;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			ldev = __netdev_next_lower_dev(now, &iter, &ignore);
			if (!ldev)
				break;
			if (ignore)
				continue;

			next = ldev;
			niter = &ldev->adj_list.lower;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}

struct net_device *netdev_next_lower_dev_rcu(struct net_device *dev,
					     struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);
	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;

	return lower->dev;
}
EXPORT_SYMBOL(netdev_next_lower_dev_rcu);

static u8 __netdev_upper_depth(struct net_device *dev)
{
	struct net_device *udev;
	struct list_head *iter;
	u8 max_depth = 0;
	bool ignore;

	for (iter = &dev->adj_list.upper,
	     udev = __netdev_next_upper_dev(dev, &iter, &ignore);
	     udev;
	     udev = __netdev_next_upper_dev(dev, &iter, &ignore)) {
		if (ignore)
			continue;
		if (max_depth < udev->upper_level)
			max_depth = udev->upper_level;
	}

	return max_depth;
}

static u8 __netdev_lower_depth(struct net_device *dev)
{
	struct net_device *ldev;
	struct list_head *iter;
	u8 max_depth = 0;
	bool ignore;

	for (iter = &dev->adj_list.lower,
	     ldev = __netdev_next_lower_dev(dev, &iter, &ignore);
	     ldev;
	     ldev = __netdev_next_lower_dev(dev, &iter, &ignore)) {
		if (ignore)
			continue;
		if (max_depth < ldev->lower_level)
			max_depth = ldev->lower_level;
	}

	return max_depth;
}

static int __netdev_update_upper_level(struct net_device *dev,
				       struct netdev_nested_priv *__unused)
{
	dev->upper_level = __netdev_upper_depth(dev) + 1;
	return 0;
}

#ifdef CONFIG_LOCKDEP
static LIST_HEAD(net_unlink_list);

static void net_unlink_todo(struct net_device *dev)
{
	if (list_empty(&dev->unlink_list))
		list_add_tail(&dev->unlink_list, &net_unlink_list);
}
#endif

static int __netdev_update_lower_level(struct net_device *dev,
				       struct netdev_nested_priv *priv)
{
	dev->lower_level = __netdev_lower_depth(dev) + 1;

#ifdef CONFIG_LOCKDEP
	if (!priv)
		return 0;

	if (priv->flags & NESTED_SYNC_IMM)
		dev->nested_level = dev->lower_level - 1;
	if (priv->flags & NESTED_SYNC_TODO)
		net_unlink_todo(dev);
#endif
	return 0;
}

int netdev_walk_all_lower_dev_rcu(struct net_device *dev,
				  int (*fn)(struct net_device *dev,
					    struct netdev_nested_priv *priv),
				  struct netdev_nested_priv *priv)
{
	struct net_device *ldev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;

	now = dev;
	iter = &dev->adj_list.lower;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			ldev = netdev_next_lower_dev_rcu(now, &iter);
			if (!ldev)
				break;

			next = ldev;
			niter = &ldev->adj_list.lower;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_walk_all_lower_dev_rcu);

/**
 * netdev_lower_get_first_private_rcu - Get the first ->private from the
 *				       lower neighbour list, RCU
 *				       variant
 * @dev: device
 *
 * Gets the first netdev_adjacent->private from the dev's lower neighbour
 * list. The caller must hold RCU read lock.
 */
void *netdev_lower_get_first_private_rcu(struct net_device *dev)
{
	struct netdev_adjacent *lower;

	lower = list_first_or_null_rcu(&dev->adj_list.lower,
			struct netdev_adjacent, list);
	if (lower)
		return lower->private;
	return NULL;
}
EXPORT_SYMBOL(netdev_lower_get_first_private_rcu);

/**
 * netdev_master_upper_dev_get_rcu - Get master upper device
 * @dev: device
 *
 * Find a master upper device and return pointer to it or NULL in case
 * it's not there. The caller must hold the RCU read lock.
 */
struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *dev)
{
	struct netdev_adjacent *upper;

	upper = list_first_or_null_rcu(&dev->adj_list.upper,
				       struct netdev_adjacent, list);
	if (upper && likely(upper->master))
		return upper->dev;
	return NULL;
}
EXPORT_SYMBOL(netdev_master_upper_dev_get_rcu);

static int netdev_adjacent_sysfs_add(struct net_device *dev,
			      struct net_device *adj_dev,
			      struct list_head *dev_list)
{
	char linkname[IFNAMSIZ+7];

	sprintf(linkname, dev_list == &dev->adj_list.upper ?
		"upper_%s" : "lower_%s", adj_dev->name);
	return sysfs_create_link(&(dev->dev.kobj), &(adj_dev->dev.kobj),
				 linkname);
}
static void netdev_adjacent_sysfs_del(struct net_device *dev,
			       char *name,
			       struct list_head *dev_list)
{
	char linkname[IFNAMSIZ+7];

	sprintf(linkname, dev_list == &dev->adj_list.upper ?
		"upper_%s" : "lower_%s", name);
	sysfs_remove_link(&(dev->dev.kobj), linkname);
}

static inline bool netdev_adjacent_is_neigh_list(struct net_device *dev,
						 struct net_device *adj_dev,
						 struct list_head *dev_list)
{
	return (dev_list == &dev->adj_list.upper ||
		dev_list == &dev->adj_list.lower) &&
		net_eq(dev_net(dev), dev_net(adj_dev));
}

static int __netdev_adjacent_dev_insert(struct net_device *dev,
					struct net_device *adj_dev,
					struct list_head *dev_list,
					void *private, bool master)
{
	struct netdev_adjacent *adj;
	int ret;

	adj = __netdev_find_adj(adj_dev, dev_list);

	if (adj) {
		adj->ref_nr += 1;
		pr_debug("Insert adjacency: dev %s adj_dev %s adj->ref_nr %d\n",
			 dev->name, adj_dev->name, adj->ref_nr);

		return 0;
	}

	adj = kmalloc(sizeof(*adj), GFP_KERNEL);
	if (!adj)
		return -ENOMEM;

	adj->dev = adj_dev;
	adj->master = master;
	adj->ref_nr = 1;
	adj->private = private;
	adj->ignore = false;
	netdev_hold(adj_dev, &adj->dev_tracker, GFP_KERNEL);

	pr_debug("Insert adjacency: dev %s adj_dev %s adj->ref_nr %d; dev_hold on %s\n",
		 dev->name, adj_dev->name, adj->ref_nr, adj_dev->name);

	if (netdev_adjacent_is_neigh_list(dev, adj_dev, dev_list)) {
		ret = netdev_adjacent_sysfs_add(dev, adj_dev, dev_list);
		if (ret)
			goto free_adj;
	}

	/* Ensure that master link is always the first item in list. */
	if (master) {
		ret = sysfs_create_link(&(dev->dev.kobj),
					&(adj_dev->dev.kobj), "master");
		if (ret)
			goto remove_symlinks;

		list_add_rcu(&adj->list, dev_list);
	} else {
		list_add_tail_rcu(&adj->list, dev_list);
	}

	return 0;

remove_symlinks:
	if (netdev_adjacent_is_neigh_list(dev, adj_dev, dev_list))
		netdev_adjacent_sysfs_del(dev, adj_dev->name, dev_list);
free_adj:
	netdev_put(adj_dev, &adj->dev_tracker);
	kfree(adj);

	return ret;
}

static void __netdev_adjacent_dev_remove(struct net_device *dev,
					 struct net_device *adj_dev,
					 u16 ref_nr,
					 struct list_head *dev_list)
{
	struct netdev_adjacent *adj;

	pr_debug("Remove adjacency: dev %s adj_dev %s ref_nr %d\n",
		 dev->name, adj_dev->name, ref_nr);

	adj = __netdev_find_adj(adj_dev, dev_list);

	if (!adj) {
		pr_err("Adjacency does not exist for device %s from %s\n",
		       dev->name, adj_dev->name);
		WARN_ON(1);
		return;
	}

	if (adj->ref_nr > ref_nr) {
		pr_debug("adjacency: %s to %s ref_nr - %d = %d\n",
			 dev->name, adj_dev->name, ref_nr,
			 adj->ref_nr - ref_nr);
		adj->ref_nr -= ref_nr;
		return;
	}

	if (adj->master)
		sysfs_remove_link(&(dev->dev.kobj), "master");

	if (netdev_adjacent_is_neigh_list(dev, adj_dev, dev_list))
		netdev_adjacent_sysfs_del(dev, adj_dev->name, dev_list);

	list_del_rcu(&adj->list);
	pr_debug("adjacency: dev_put for %s, because link removed from %s to %s\n",
		 adj_dev->name, dev->name, adj_dev->name);
	netdev_put(adj_dev, &adj->dev_tracker);
	kfree_rcu(adj, rcu);
}

static int __netdev_adjacent_dev_link_lists(struct net_device *dev,
					    struct net_device *upper_dev,
					    struct list_head *up_list,
					    struct list_head *down_list,
					    void *private, bool master)
{
	int ret;

	ret = __netdev_adjacent_dev_insert(dev, upper_dev, up_list,
					   private, master);
	if (ret)
		return ret;

	ret = __netdev_adjacent_dev_insert(upper_dev, dev, down_list,
					   private, false);
	if (ret) {
		__netdev_adjacent_dev_remove(dev, upper_dev, 1, up_list);
		return ret;
	}

	return 0;
}

static void __netdev_adjacent_dev_unlink_lists(struct net_device *dev,
					       struct net_device *upper_dev,
					       u16 ref_nr,
					       struct list_head *up_list,
					       struct list_head *down_list)
{
	__netdev_adjacent_dev_remove(dev, upper_dev, ref_nr, up_list);
	__netdev_adjacent_dev_remove(upper_dev, dev, ref_nr, down_list);
}

static int __netdev_adjacent_dev_link_neighbour(struct net_device *dev,
						struct net_device *upper_dev,
						void *private, bool master)
{
	return __netdev_adjacent_dev_link_lists(dev, upper_dev,
						&dev->adj_list.upper,
						&upper_dev->adj_list.lower,
						private, master);
}

static void __netdev_adjacent_dev_unlink_neighbour(struct net_device *dev,
						   struct net_device *upper_dev)
{
	__netdev_adjacent_dev_unlink_lists(dev, upper_dev, 1,
					   &dev->adj_list.upper,
					   &upper_dev->adj_list.lower);
}

static int __netdev_upper_dev_link(struct net_device *dev,
				   struct net_device *upper_dev, bool master,
				   void *upper_priv, void *upper_info,
				   struct netdev_nested_priv *priv,
				   struct netlink_ext_ack *extack)
{
	struct netdev_notifier_changeupper_info changeupper_info = {
		.info = {
			.dev = dev,
			.extack = extack,
		},
		.upper_dev = upper_dev,
		.master = master,
		.linking = true,
		.upper_info = upper_info,
	};
	struct net_device *master_dev;
	int ret = 0;

	ASSERT_RTNL();

	if (dev == upper_dev)
		return -EBUSY;

	/* To prevent loops, check if dev is not upper device to upper_dev. */
	if (__netdev_has_upper_dev(upper_dev, dev))
		return -EBUSY;

	if ((dev->lower_level + upper_dev->upper_level) > MAX_NEST_DEV)
		return -EMLINK;

	if (!master) {
		if (__netdev_has_upper_dev(dev, upper_dev))
			return -EEXIST;
	} else {
		master_dev = __netdev_master_upper_dev_get(dev);
		if (master_dev)
			return master_dev == upper_dev ? -EEXIST : -EBUSY;
	}

	ret = call_netdevice_notifiers_info(NETDEV_PRECHANGEUPPER,
					    &changeupper_info.info);
	ret = notifier_to_errno(ret);
	if (ret)
		return ret;

	ret = __netdev_adjacent_dev_link_neighbour(dev, upper_dev, upper_priv,
						   master);
	if (ret)
		return ret;

	ret = call_netdevice_notifiers_info(NETDEV_CHANGEUPPER,
					    &changeupper_info.info);
	ret = notifier_to_errno(ret);
	if (ret)
		goto rollback;

	__netdev_update_upper_level(dev, NULL);
	__netdev_walk_all_lower_dev(dev, __netdev_update_upper_level, NULL);

	__netdev_update_lower_level(upper_dev, priv);
	__netdev_walk_all_upper_dev(upper_dev, __netdev_update_lower_level,
				    priv);

	return 0;

rollback:
	__netdev_adjacent_dev_unlink_neighbour(dev, upper_dev);

	return ret;
}

/**
 * netdev_upper_dev_link - Add a link to the upper device
 * @dev: device
 * @upper_dev: new upper device
 * @extack: netlink extended ack
 *
 * Adds a link to device which is upper to this one. The caller must hold
 * the RTNL lock. On a failure a negative errno code is returned.
 * On success the reference counts are adjusted and the function
 * returns zero.
 */
int netdev_upper_dev_link(struct net_device *dev,
			  struct net_device *upper_dev,
			  struct netlink_ext_ack *extack)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_IMM | NESTED_SYNC_TODO,
		.data = NULL,
	};

	return __netdev_upper_dev_link(dev, upper_dev, false,
				       NULL, NULL, &priv, extack);
}
EXPORT_SYMBOL(netdev_upper_dev_link);

/**
 * netdev_master_upper_dev_link - Add a master link to the upper device
 * @dev: device
 * @upper_dev: new upper device
 * @upper_priv: upper device private
 * @upper_info: upper info to be passed down via notifier
 * @extack: netlink extended ack
 *
 * Adds a link to device which is upper to this one. In this case, only
 * one master upper device can be linked, although other non-master devices
 * might be linked as well. The caller must hold the RTNL lock.
 * On a failure a negative errno code is returned. On success the reference
 * counts are adjusted and the function returns zero.
 */
int netdev_master_upper_dev_link(struct net_device *dev,
				 struct net_device *upper_dev,
				 void *upper_priv, void *upper_info,
				 struct netlink_ext_ack *extack)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_IMM | NESTED_SYNC_TODO,
		.data = NULL,
	};

	return __netdev_upper_dev_link(dev, upper_dev, true,
				       upper_priv, upper_info, &priv, extack);
}
EXPORT_SYMBOL(netdev_master_upper_dev_link);

static void __netdev_upper_dev_unlink(struct net_device *dev,
				      struct net_device *upper_dev,
				      struct netdev_nested_priv *priv)
{
	struct netdev_notifier_changeupper_info changeupper_info = {
		.info = {
			.dev = dev,
		},
		.upper_dev = upper_dev,
		.linking = false,
	};

	ASSERT_RTNL();

	changeupper_info.master = netdev_master_upper_dev_get(dev) == upper_dev;

	call_netdevice_notifiers_info(NETDEV_PRECHANGEUPPER,
				      &changeupper_info.info);

	__netdev_adjacent_dev_unlink_neighbour(dev, upper_dev);

	call_netdevice_notifiers_info(NETDEV_CHANGEUPPER,
				      &changeupper_info.info);

	__netdev_update_upper_level(dev, NULL);
	__netdev_walk_all_lower_dev(dev, __netdev_update_upper_level, NULL);

	__netdev_update_lower_level(upper_dev, priv);
	__netdev_walk_all_upper_dev(upper_dev, __netdev_update_lower_level,
				    priv);
}

/**
 * netdev_upper_dev_unlink - Removes a link to upper device
 * @dev: device
 * @upper_dev: new upper device
 *
 * Removes a link to device which is upper to this one. The caller must hold
 * the RTNL lock.
 */
void netdev_upper_dev_unlink(struct net_device *dev,
			     struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_TODO,
		.data = NULL,
	};

	__netdev_upper_dev_unlink(dev, upper_dev, &priv);
}
EXPORT_SYMBOL(netdev_upper_dev_unlink);

static void __netdev_adjacent_dev_set(struct net_device *upper_dev,
				      struct net_device *lower_dev,
				      bool val)
{
	struct netdev_adjacent *adj;

	adj = __netdev_find_adj(lower_dev, &upper_dev->adj_list.lower);
	if (adj)
		adj->ignore = val;

	adj = __netdev_find_adj(upper_dev, &lower_dev->adj_list.upper);
	if (adj)
		adj->ignore = val;
}

static void netdev_adjacent_dev_disable(struct net_device *upper_dev,
					struct net_device *lower_dev)
{
	__netdev_adjacent_dev_set(upper_dev, lower_dev, true);
}

static void netdev_adjacent_dev_enable(struct net_device *upper_dev,
				       struct net_device *lower_dev)
{
	__netdev_adjacent_dev_set(upper_dev, lower_dev, false);
}

int netdev_adjacent_change_prepare(struct net_device *old_dev,
				   struct net_device *new_dev,
				   struct net_device *dev,
				   struct netlink_ext_ack *extack)
{
	struct netdev_nested_priv priv = {
		.flags = 0,
		.data = NULL,
	};
	int err;

	if (!new_dev)
		return 0;

	if (old_dev && new_dev != old_dev)
		netdev_adjacent_dev_disable(dev, old_dev);
	err = __netdev_upper_dev_link(new_dev, dev, false, NULL, NULL, &priv,
				      extack);
	if (err) {
		if (old_dev && new_dev != old_dev)
			netdev_adjacent_dev_enable(dev, old_dev);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(netdev_adjacent_change_prepare);

void netdev_adjacent_change_commit(struct net_device *old_dev,
				   struct net_device *new_dev,
				   struct net_device *dev)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_IMM | NESTED_SYNC_TODO,
		.data = NULL,
	};

	if (!new_dev || !old_dev)
		return;

	if (new_dev == old_dev)
		return;

	netdev_adjacent_dev_enable(dev, old_dev);
	__netdev_upper_dev_unlink(old_dev, dev, &priv);
}
EXPORT_SYMBOL(netdev_adjacent_change_commit);

void netdev_adjacent_change_abort(struct net_device *old_dev,
				  struct net_device *new_dev,
				  struct net_device *dev)
{
	struct netdev_nested_priv priv = {
		.flags = 0,
		.data = NULL,
	};

	if (!new_dev)
		return;

	if (old_dev && new_dev != old_dev)
		netdev_adjacent_dev_enable(dev, old_dev);

	__netdev_upper_dev_unlink(new_dev, dev, &priv);
}
EXPORT_SYMBOL(netdev_adjacent_change_abort);

/**
 * netdev_bonding_info_change - Dispatch event about slave change
 * @dev: device
 * @bonding_info: info to dispatch
 *
 * Send NETDEV_BONDING_INFO to netdev notifiers with info.
 * The caller must hold the RTNL lock.
 */
void netdev_bonding_info_change(struct net_device *dev,
				struct netdev_bonding_info *bonding_info)
{
	struct netdev_notifier_bonding_info info = {
		.info.dev = dev,
	};

	memcpy(&info.bonding_info, bonding_info,
	       sizeof(struct netdev_bonding_info));
	call_netdevice_notifiers_info(NETDEV_BONDING_INFO,
				      &info.info);
}
EXPORT_SYMBOL(netdev_bonding_info_change);

static int netdev_offload_xstats_enable_l3(struct net_device *dev,
					   struct netlink_ext_ack *extack)
{
	struct netdev_notifier_offload_xstats_info info = {
		.info.dev = dev,
		.info.extack = extack,
		.type = NETDEV_OFFLOAD_XSTATS_TYPE_L3,
	};
	int err;
	int rc;

	dev->offload_xstats_l3 = kzalloc(sizeof(*dev->offload_xstats_l3),
					 GFP_KERNEL);
	if (!dev->offload_xstats_l3)
		return -ENOMEM;

	rc = call_netdevice_notifiers_info_robust(NETDEV_OFFLOAD_XSTATS_ENABLE,
						  NETDEV_OFFLOAD_XSTATS_DISABLE,
						  &info.info);
	err = notifier_to_errno(rc);
	if (err)
		goto free_stats;

	return 0;

free_stats:
	kfree(dev->offload_xstats_l3);
	dev->offload_xstats_l3 = NULL;
	return err;
}

int netdev_offload_xstats_enable(struct net_device *dev,
				 enum netdev_offload_xstats_type type,
				 struct netlink_ext_ack *extack)
{
	ASSERT_RTNL();

	if (netdev_offload_xstats_enabled(dev, type))
		return -EALREADY;

	switch (type) {
	case NETDEV_OFFLOAD_XSTATS_TYPE_L3:
		return netdev_offload_xstats_enable_l3(dev, extack);
	}

	WARN_ON(1);
	return -EINVAL;
}
EXPORT_SYMBOL(netdev_offload_xstats_enable);

static void netdev_offload_xstats_disable_l3(struct net_device *dev)
{
	struct netdev_notifier_offload_xstats_info info = {
		.info.dev = dev,
		.type = NETDEV_OFFLOAD_XSTATS_TYPE_L3,
	};

	call_netdevice_notifiers_info(NETDEV_OFFLOAD_XSTATS_DISABLE,
				      &info.info);
	kfree(dev->offload_xstats_l3);
	dev->offload_xstats_l3 = NULL;
}

int netdev_offload_xstats_disable(struct net_device *dev,
				  enum netdev_offload_xstats_type type)
{
	ASSERT_RTNL();

	if (!netdev_offload_xstats_enabled(dev, type))
		return -EALREADY;

	switch (type) {
	case NETDEV_OFFLOAD_XSTATS_TYPE_L3:
		netdev_offload_xstats_disable_l3(dev);
		return 0;
	}

	WARN_ON(1);
	return -EINVAL;
}
EXPORT_SYMBOL(netdev_offload_xstats_disable);

static void netdev_offload_xstats_disable_all(struct net_device *dev)
{
	netdev_offload_xstats_disable(dev, NETDEV_OFFLOAD_XSTATS_TYPE_L3);
}

static struct rtnl_hw_stats64 *
netdev_offload_xstats_get_ptr(const struct net_device *dev,
			      enum netdev_offload_xstats_type type)
{
	switch (type) {
	case NETDEV_OFFLOAD_XSTATS_TYPE_L3:
		return dev->offload_xstats_l3;
	}

	WARN_ON(1);
	return NULL;
}

bool netdev_offload_xstats_enabled(const struct net_device *dev,
				   enum netdev_offload_xstats_type type)
{
	ASSERT_RTNL();

	return netdev_offload_xstats_get_ptr(dev, type);
}
EXPORT_SYMBOL(netdev_offload_xstats_enabled);

struct netdev_notifier_offload_xstats_ru {
	bool used;
};

struct netdev_notifier_offload_xstats_rd {
	struct rtnl_hw_stats64 stats;
	bool used;
};

static void netdev_hw_stats64_add(struct rtnl_hw_stats64 *dest,
				  const struct rtnl_hw_stats64 *src)
{
	dest->rx_packets	  += src->rx_packets;
	dest->tx_packets	  += src->tx_packets;
	dest->rx_bytes		  += src->rx_bytes;
	dest->tx_bytes		  += src->tx_bytes;
	dest->rx_errors		  += src->rx_errors;
	dest->tx_errors		  += src->tx_errors;
	dest->rx_dropped	  += src->rx_dropped;
	dest->tx_dropped	  += src->tx_dropped;
	dest->multicast		  += src->multicast;
}

static int netdev_offload_xstats_get_used(struct net_device *dev,
					  enum netdev_offload_xstats_type type,
					  bool *p_used,
					  struct netlink_ext_ack *extack)
{
	struct netdev_notifier_offload_xstats_ru report_used = {};
	struct netdev_notifier_offload_xstats_info info = {
		.info.dev = dev,
		.info.extack = extack,
		.type = type,
		.report_used = &report_used,
	};
	int rc;

	WARN_ON(!netdev_offload_xstats_enabled(dev, type));
	rc = call_netdevice_notifiers_info(NETDEV_OFFLOAD_XSTATS_REPORT_USED,
					   &info.info);
	*p_used = report_used.used;
	return notifier_to_errno(rc);
}

static int netdev_offload_xstats_get_stats(struct net_device *dev,
					   enum netdev_offload_xstats_type type,
					   struct rtnl_hw_stats64 *p_stats,
					   bool *p_used,
					   struct netlink_ext_ack *extack)
{
	struct netdev_notifier_offload_xstats_rd report_delta = {};
	struct netdev_notifier_offload_xstats_info info = {
		.info.dev = dev,
		.info.extack = extack,
		.type = type,
		.report_delta = &report_delta,
	};
	struct rtnl_hw_stats64 *stats;
	int rc;

	stats = netdev_offload_xstats_get_ptr(dev, type);
	if (WARN_ON(!stats))
		return -EINVAL;

	rc = call_netdevice_notifiers_info(NETDEV_OFFLOAD_XSTATS_REPORT_DELTA,
					   &info.info);

	/* Cache whatever we got, even if there was an error, otherwise the
	 * successful stats retrievals would get lost.
	 */
	netdev_hw_stats64_add(stats, &report_delta.stats);

	if (p_stats)
		*p_stats = *stats;
	*p_used = report_delta.used;

	return notifier_to_errno(rc);
}

int netdev_offload_xstats_get(struct net_device *dev,
			      enum netdev_offload_xstats_type type,
			      struct rtnl_hw_stats64 *p_stats, bool *p_used,
			      struct netlink_ext_ack *extack)
{
	ASSERT_RTNL();

	if (p_stats)
		return netdev_offload_xstats_get_stats(dev, type, p_stats,
						       p_used, extack);
	else
		return netdev_offload_xstats_get_used(dev, type, p_used,
						      extack);
}
EXPORT_SYMBOL(netdev_offload_xstats_get);

void
netdev_offload_xstats_report_delta(struct netdev_notifier_offload_xstats_rd *report_delta,
				   const struct rtnl_hw_stats64 *stats)
{
	report_delta->used = true;
	netdev_hw_stats64_add(&report_delta->stats, stats);
}
EXPORT_SYMBOL(netdev_offload_xstats_report_delta);

void
netdev_offload_xstats_report_used(struct netdev_notifier_offload_xstats_ru *report_used)
{
	report_used->used = true;
}
EXPORT_SYMBOL(netdev_offload_xstats_report_used);

void netdev_offload_xstats_push_delta(struct net_device *dev,
				      enum netdev_offload_xstats_type type,
				      const struct rtnl_hw_stats64 *p_stats)
{
	struct rtnl_hw_stats64 *stats;

	ASSERT_RTNL();

	stats = netdev_offload_xstats_get_ptr(dev, type);
	if (WARN_ON(!stats))
		return;

	netdev_hw_stats64_add(stats, p_stats);
}
EXPORT_SYMBOL(netdev_offload_xstats_push_delta);

/**
 * netdev_get_xmit_slave - Get the xmit slave of master device
 * @dev: device
 * @skb: The packet
 * @all_slaves: assume all the slaves are active
 *
 * The reference counters are not incremented so the caller must be
 * careful with locks. The caller must hold RCU lock.
 * %NULL is returned if no slave is found.
 */

struct net_device *netdev_get_xmit_slave(struct net_device *dev,
					 struct sk_buff *skb,
					 bool all_slaves)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_get_xmit_slave)
		return NULL;
	return ops->ndo_get_xmit_slave(dev, skb, all_slaves);
}
EXPORT_SYMBOL(netdev_get_xmit_slave);

static struct net_device *netdev_sk_get_lower_dev(struct net_device *dev,
						  struct sock *sk)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_sk_get_lower_dev)
		return NULL;
	return ops->ndo_sk_get_lower_dev(dev, sk);
}

/**
 * netdev_sk_get_lowest_dev - Get the lowest device in chain given device and socket
 * @dev: device
 * @sk: the socket
 *
 * %NULL is returned if no lower device is found.
 */

struct net_device *netdev_sk_get_lowest_dev(struct net_device *dev,
					    struct sock *sk)
{
	struct net_device *lower;

	lower = netdev_sk_get_lower_dev(dev, sk);
	while (lower) {
		dev = lower;
		lower = netdev_sk_get_lower_dev(dev, sk);
	}

	return dev;
}
EXPORT_SYMBOL(netdev_sk_get_lowest_dev);

static void netdev_adjacent_add_links(struct net_device *dev)
{
	struct netdev_adjacent *iter;

	struct net *net = dev_net(dev);

	list_for_each_entry(iter, &dev->adj_list.upper, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.lower);
		netdev_adjacent_sysfs_add(dev, iter->dev,
					  &dev->adj_list.upper);
	}

	list_for_each_entry(iter, &dev->adj_list.lower, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.upper);
		netdev_adjacent_sysfs_add(dev, iter->dev,
					  &dev->adj_list.lower);
	}
}

static void netdev_adjacent_del_links(struct net_device *dev)
{
	struct netdev_adjacent *iter;

	struct net *net = dev_net(dev);

	list_for_each_entry(iter, &dev->adj_list.upper, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, dev->name,
					  &iter->dev->adj_list.lower);
		netdev_adjacent_sysfs_del(dev, iter->dev->name,
					  &dev->adj_list.upper);
	}

	list_for_each_entry(iter, &dev->adj_list.lower, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, dev->name,
					  &iter->dev->adj_list.upper);
		netdev_adjacent_sysfs_del(dev, iter->dev->name,
					  &dev->adj_list.lower);
	}
}

void netdev_adjacent_rename_links(struct net_device *dev, char *oldname)
{
	struct netdev_adjacent *iter;

	struct net *net = dev_net(dev);

	list_for_each_entry(iter, &dev->adj_list.upper, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, oldname,
					  &iter->dev->adj_list.lower);
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.lower);
	}

	list_for_each_entry(iter, &dev->adj_list.lower, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, oldname,
					  &iter->dev->adj_list.upper);
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.upper);
	}
}

void *netdev_lower_dev_get_private(struct net_device *dev,
				   struct net_device *lower_dev)
{
	struct netdev_adjacent *lower;

	if (!lower_dev)
		return NULL;
	lower = __netdev_find_adj(lower_dev, &dev->adj_list.lower);
	if (!lower)
		return NULL;

	return lower->private;
}
EXPORT_SYMBOL(netdev_lower_dev_get_private);


/**
 * netdev_lower_state_changed - Dispatch event about lower device state change
 * @lower_dev: device
 * @lower_state_info: state to dispatch
 *
 * Send NETDEV_CHANGELOWERSTATE to netdev notifiers with info.
 * The caller must hold the RTNL lock.
 */
void netdev_lower_state_changed(struct net_device *lower_dev,
				void *lower_state_info)
{
	struct netdev_notifier_changelowerstate_info changelowerstate_info = {
		.info.dev = lower_dev,
	};

	ASSERT_RTNL();
	changelowerstate_info.lower_state_info = lower_state_info;
	call_netdevice_notifiers_info(NETDEV_CHANGELOWERSTATE,
				      &changelowerstate_info.info);
}
EXPORT_SYMBOL(netdev_lower_state_changed);

static void dev_change_rx_flags(struct net_device *dev, int flags)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (ops->ndo_change_rx_flags)
		ops->ndo_change_rx_flags(dev, flags);
}

static int __dev_set_promiscuity(struct net_device *dev, int inc, bool notify)
{
	unsigned int old_flags = dev->flags;
	unsigned int promiscuity, flags;
	kuid_t uid;
	kgid_t gid;

	ASSERT_RTNL();

	promiscuity = dev->promiscuity + inc;
	if (promiscuity == 0) {
		/*
		 * Avoid overflow.
		 * If inc causes overflow, untouch promisc and return error.
		 */
		if (unlikely(inc > 0)) {
			netdev_warn(dev, "promiscuity touches roof, set promiscuity failed. promiscuity feature of device might be broken.\n");
			return -EOVERFLOW;
		}
		flags = old_flags & ~IFF_PROMISC;
	} else {
		flags = old_flags | IFF_PROMISC;
	}
	WRITE_ONCE(dev->promiscuity, promiscuity);
	if (flags != old_flags) {
		WRITE_ONCE(dev->flags, flags);
		netdev_info(dev, "%s promiscuous mode\n",
			    dev->flags & IFF_PROMISC ? "entered" : "left");
		if (audit_enabled) {
			current_uid_gid(&uid, &gid);
			audit_log(audit_context(), GFP_ATOMIC,
				  AUDIT_ANOM_PROMISCUOUS,
				  "dev=%s prom=%d old_prom=%d auid=%u uid=%u gid=%u ses=%u",
				  dev->name, (dev->flags & IFF_PROMISC),
				  (old_flags & IFF_PROMISC),
				  from_kuid(&init_user_ns, audit_get_loginuid(current)),
				  from_kuid(&init_user_ns, uid),
				  from_kgid(&init_user_ns, gid),
				  audit_get_sessionid(current));
		}

		dev_change_rx_flags(dev, IFF_PROMISC);
	}
	if (notify) {
		/* The ops lock is only required to ensure consistent locking
		 * for `NETDEV_CHANGE` notifiers. This function is sometimes
		 * called without the lock, even for devices that are ops
		 * locked, such as in `dev_uc_sync_multiple` when using
		 * bonding or teaming.
		 */
		netdev_ops_assert_locked(dev);
		__dev_notify_flags(dev, old_flags, IFF_PROMISC, 0, NULL);
	}
	return 0;
}

int netif_set_promiscuity(struct net_device *dev, int inc)
{
	unsigned int old_flags = dev->flags;
	int err;

	err = __dev_set_promiscuity(dev, inc, true);
	if (err < 0)
		return err;
	if (dev->flags != old_flags)
		dev_set_rx_mode(dev);
	return err;
}

int netif_set_allmulti(struct net_device *dev, int inc, bool notify)
{
	unsigned int old_flags = dev->flags, old_gflags = dev->gflags;
	unsigned int allmulti, flags;

	ASSERT_RTNL();

	allmulti = dev->allmulti + inc;
	if (allmulti == 0) {
		/*
		 * Avoid overflow.
		 * If inc causes overflow, untouch allmulti and return error.
		 */
		if (unlikely(inc > 0)) {
			netdev_warn(dev, "allmulti touches roof, set allmulti failed. allmulti feature of device might be broken.\n");
			return -EOVERFLOW;
		}
		flags = old_flags & ~IFF_ALLMULTI;
	} else {
		flags = old_flags | IFF_ALLMULTI;
	}
	WRITE_ONCE(dev->allmulti, allmulti);
	if (flags != old_flags) {
		WRITE_ONCE(dev->flags, flags);
		netdev_info(dev, "%s allmulticast mode\n",
			    dev->flags & IFF_ALLMULTI ? "entered" : "left");
		dev_change_rx_flags(dev, IFF_ALLMULTI);
		dev_set_rx_mode(dev);
		if (notify)
			__dev_notify_flags(dev, old_flags,
					   dev->gflags ^ old_gflags, 0, NULL);
	}
	return 0;
}

/*
 *	Upload unicast and multicast address lists to device and
 *	configure RX filtering. When the device doesn't support unicast
 *	filtering it is put in promiscuous mode while unicast addresses
 *	are present.
 */
void __dev_set_rx_mode(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	/* dev_open will call this function so the list will stay sane. */
	if (!(dev->flags&IFF_UP))
		return;

	if (!netif_device_present(dev))
		return;

	if (!(dev->priv_flags & IFF_UNICAST_FLT)) {
		/* Unicast addresses changes may only happen under the rtnl,
		 * therefore calling __dev_set_promiscuity here is safe.
		 */
		if (!netdev_uc_empty(dev) && !dev->uc_promisc) {
			__dev_set_promiscuity(dev, 1, false);
			dev->uc_promisc = true;
		} else if (netdev_uc_empty(dev) && dev->uc_promisc) {
			__dev_set_promiscuity(dev, -1, false);
			dev->uc_promisc = false;
		}
	}

	if (ops->ndo_set_rx_mode)
		ops->ndo_set_rx_mode(dev);
}

void dev_set_rx_mode(struct net_device *dev)
{
	netif_addr_lock_bh(dev);
	__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
}

/**
 * netif_get_flags() - get flags reported to userspace
 * @dev: device
 *
 * Get the combination of flag bits exported through APIs to userspace.
 */
unsigned int netif_get_flags(const struct net_device *dev)
{
	unsigned int flags;

	flags = (READ_ONCE(dev->flags) & ~(IFF_PROMISC |
				IFF_ALLMULTI |
				IFF_RUNNING |
				IFF_LOWER_UP |
				IFF_DORMANT)) |
		(READ_ONCE(dev->gflags) & (IFF_PROMISC |
				IFF_ALLMULTI));

	if (netif_running(dev)) {
		if (netif_oper_up(dev))
			flags |= IFF_RUNNING;
		if (netif_carrier_ok(dev))
			flags |= IFF_LOWER_UP;
		if (netif_dormant(dev))
			flags |= IFF_DORMANT;
	}

	return flags;
}
EXPORT_SYMBOL(netif_get_flags);

int __dev_change_flags(struct net_device *dev, unsigned int flags,
		       struct netlink_ext_ack *extack)
{
	unsigned int old_flags = dev->flags;
	int ret;

	ASSERT_RTNL();

	/*
	 *	Set the flags on our device.
	 */

	dev->flags = (flags & (IFF_DEBUG | IFF_NOTRAILERS | IFF_NOARP |
			       IFF_DYNAMIC | IFF_MULTICAST | IFF_PORTSEL |
			       IFF_AUTOMEDIA)) |
		     (dev->flags & (IFF_UP | IFF_VOLATILE | IFF_PROMISC |
				    IFF_ALLMULTI));

	/*
	 *	Load in the correct multicast list now the flags have changed.
	 */

	if ((old_flags ^ flags) & IFF_MULTICAST)
		dev_change_rx_flags(dev, IFF_MULTICAST);

	dev_set_rx_mode(dev);

	/*
	 *	Have we downed the interface. We handle IFF_UP ourselves
	 *	according to user attempts to set it, rather than blindly
	 *	setting it.
	 */

	ret = 0;
	if ((old_flags ^ flags) & IFF_UP) {
		if (old_flags & IFF_UP)
			__dev_close(dev);
		else
			ret = __dev_open(dev, extack);
	}

	if ((flags ^ dev->gflags) & IFF_PROMISC) {
		int inc = (flags & IFF_PROMISC) ? 1 : -1;
		old_flags = dev->flags;

		dev->gflags ^= IFF_PROMISC;

		if (__dev_set_promiscuity(dev, inc, false) >= 0)
			if (dev->flags != old_flags)
				dev_set_rx_mode(dev);
	}

	/* NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
	 * is important. Some (broken) drivers set IFF_PROMISC, when
	 * IFF_ALLMULTI is requested not asking us and not reporting.
	 */
	if ((flags ^ dev->gflags) & IFF_ALLMULTI) {
		int inc = (flags & IFF_ALLMULTI) ? 1 : -1;

		dev->gflags ^= IFF_ALLMULTI;
		netif_set_allmulti(dev, inc, false);
	}

	return ret;
}

void __dev_notify_flags(struct net_device *dev, unsigned int old_flags,
			unsigned int gchanges, u32 portid,
			const struct nlmsghdr *nlh)
{
	unsigned int changes = dev->flags ^ old_flags;

	if (gchanges)
		rtmsg_ifinfo(RTM_NEWLINK, dev, gchanges, GFP_ATOMIC, portid, nlh);

	if (changes & IFF_UP) {
		if (dev->flags & IFF_UP)
			call_netdevice_notifiers(NETDEV_UP, dev);
		else
			call_netdevice_notifiers(NETDEV_DOWN, dev);
	}

	if (dev->flags & IFF_UP &&
	    (changes & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI | IFF_VOLATILE))) {
		struct netdev_notifier_change_info change_info = {
			.info = {
				.dev = dev,
			},
			.flags_changed = changes,
		};

		call_netdevice_notifiers_info(NETDEV_CHANGE, &change_info.info);
	}
}

int netif_change_flags(struct net_device *dev, unsigned int flags,
		       struct netlink_ext_ack *extack)
{
	int ret;
	unsigned int changes, old_flags = dev->flags, old_gflags = dev->gflags;

	ret = __dev_change_flags(dev, flags, extack);
	if (ret < 0)
		return ret;

	changes = (old_flags ^ dev->flags) | (old_gflags ^ dev->gflags);
	__dev_notify_flags(dev, old_flags, changes, 0, NULL);
	return ret;
}

int __netif_set_mtu(struct net_device *dev, int new_mtu)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (ops->ndo_change_mtu)
		return ops->ndo_change_mtu(dev, new_mtu);

	/* Pairs with all the lockless reads of dev->mtu in the stack */
	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(__netif_set_mtu, "NETDEV_INTERNAL");

int dev_validate_mtu(struct net_device *dev, int new_mtu,
		     struct netlink_ext_ack *extack)
{
	/* MTU must be positive, and in range */
	if (new_mtu < 0 || new_mtu < dev->min_mtu) {
		NL_SET_ERR_MSG(extack, "mtu less than device minimum");
		return -EINVAL;
	}

	if (dev->max_mtu > 0 && new_mtu > dev->max_mtu) {
		NL_SET_ERR_MSG(extack, "mtu greater than device maximum");
		return -EINVAL;
	}
	return 0;
}

/**
 * netif_set_mtu_ext() - Change maximum transfer unit
 * @dev: device
 * @new_mtu: new transfer unit
 * @extack: netlink extended ack
 *
 * Change the maximum transfer size of the network device.
 *
 * Return: 0 on success, -errno on failure.
 */
int netif_set_mtu_ext(struct net_device *dev, int new_mtu,
		      struct netlink_ext_ack *extack)
{
	int err, orig_mtu;

	netdev_ops_assert_locked(dev);

	if (new_mtu == dev->mtu)
		return 0;

	err = dev_validate_mtu(dev, new_mtu, extack);
	if (err)
		return err;

	if (!netif_device_present(dev))
		return -ENODEV;

	err = call_netdevice_notifiers(NETDEV_PRECHANGEMTU, dev);
	err = notifier_to_errno(err);
	if (err)
		return err;

	orig_mtu = dev->mtu;
	err = __netif_set_mtu(dev, new_mtu);

	if (!err) {
		err = call_netdevice_notifiers_mtu(NETDEV_CHANGEMTU, dev,
						   orig_mtu);
		err = notifier_to_errno(err);
		if (err) {
			/* setting mtu back and notifying everyone again,
			 * so that they have a chance to revert changes.
			 */
			__netif_set_mtu(dev, orig_mtu);
			call_netdevice_notifiers_mtu(NETDEV_CHANGEMTU, dev,
						     new_mtu);
		}
	}
	return err;
}

int netif_set_mtu(struct net_device *dev, int new_mtu)
{
	struct netlink_ext_ack extack;
	int err;

	memset(&extack, 0, sizeof(extack));
	err = netif_set_mtu_ext(dev, new_mtu, &extack);
	if (err && extack._msg)
		net_err_ratelimited("%s: %s\n", dev->name, extack._msg);
	return err;
}
EXPORT_SYMBOL(netif_set_mtu);

int netif_change_tx_queue_len(struct net_device *dev, unsigned long new_len)
{
	unsigned int orig_len = dev->tx_queue_len;
	int res;

	if (new_len != (unsigned int)new_len)
		return -ERANGE;

	if (new_len != orig_len) {
		WRITE_ONCE(dev->tx_queue_len, new_len);
		res = call_netdevice_notifiers(NETDEV_CHANGE_TX_QUEUE_LEN, dev);
		res = notifier_to_errno(res);
		if (res)
			goto err_rollback;
		res = dev_qdisc_change_tx_queue_len(dev);
		if (res)
			goto err_rollback;
	}

	return 0;

err_rollback:
	netdev_err(dev, "refused to change device tx_queue_len\n");
	WRITE_ONCE(dev->tx_queue_len, orig_len);
	return res;
}

void netif_set_group(struct net_device *dev, int new_group)
{
	dev->group = new_group;
}

/**
 * netif_pre_changeaddr_notify() - Call NETDEV_PRE_CHANGEADDR.
 * @dev: device
 * @addr: new address
 * @extack: netlink extended ack
 *
 * Return: 0 on success, -errno on failure.
 */
int netif_pre_changeaddr_notify(struct net_device *dev, const char *addr,
				struct netlink_ext_ack *extack)
{
	struct netdev_notifier_pre_changeaddr_info info = {
		.info.dev = dev,
		.info.extack = extack,
		.dev_addr = addr,
	};
	int rc;

	rc = call_netdevice_notifiers_info(NETDEV_PRE_CHANGEADDR, &info.info);
	return notifier_to_errno(rc);
}
EXPORT_SYMBOL_NS_GPL(netif_pre_changeaddr_notify, "NETDEV_INTERNAL");

int netif_set_mac_address(struct net_device *dev, struct sockaddr_storage *ss,
			  struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (!ops->ndo_set_mac_address)
		return -EOPNOTSUPP;
	if (ss->ss_family != dev->type)
		return -EINVAL;
	if (!netif_device_present(dev))
		return -ENODEV;
	err = netif_pre_changeaddr_notify(dev, ss->__data, extack);
	if (err)
		return err;
	if (memcmp(dev->dev_addr, ss->__data, dev->addr_len)) {
		err = ops->ndo_set_mac_address(dev, ss);
		if (err)
			return err;
	}
	dev->addr_assign_type = NET_ADDR_SET;
	call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	add_device_randomness(dev->dev_addr, dev->addr_len);
	return 0;
}

DECLARE_RWSEM(dev_addr_sem);

/* "sa" is a true struct sockaddr with limited "sa_data" member. */
int netif_get_mac_address(struct sockaddr *sa, struct net *net, char *dev_name)
{
	size_t size = sizeof(sa->sa_data_min);
	struct net_device *dev;
	int ret = 0;

	down_read(&dev_addr_sem);
	rcu_read_lock();

	dev = dev_get_by_name_rcu(net, dev_name);
	if (!dev) {
		ret = -ENODEV;
		goto unlock;
	}
	if (!dev->addr_len)
		memset(sa->sa_data, 0, size);
	else
		memcpy(sa->sa_data, dev->dev_addr,
		       min_t(size_t, size, dev->addr_len));
	sa->sa_family = dev->type;

unlock:
	rcu_read_unlock();
	up_read(&dev_addr_sem);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(netif_get_mac_address, "NETDEV_INTERNAL");

int netif_change_carrier(struct net_device *dev, bool new_carrier)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_change_carrier)
		return -EOPNOTSUPP;
	if (!netif_device_present(dev))
		return -ENODEV;
	return ops->ndo_change_carrier(dev, new_carrier);
}

/**
 *	dev_get_phys_port_id - Get device physical port ID
 *	@dev: device
 *	@ppid: port ID
 *
 *	Get device physical port ID
 */
int dev_get_phys_port_id(struct net_device *dev,
			 struct netdev_phys_item_id *ppid)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_get_phys_port_id)
		return -EOPNOTSUPP;
	return ops->ndo_get_phys_port_id(dev, ppid);
}

/**
 *	dev_get_phys_port_name - Get device physical port name
 *	@dev: device
 *	@name: port name
 *	@len: limit of bytes to copy to name
 *
 *	Get device physical port name
 */
int dev_get_phys_port_name(struct net_device *dev,
			   char *name, size_t len)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (ops->ndo_get_phys_port_name) {
		err = ops->ndo_get_phys_port_name(dev, name, len);
		if (err != -EOPNOTSUPP)
			return err;
	}
	return devlink_compat_phys_port_name_get(dev, name, len);
}

/**
 * netif_get_port_parent_id() - Get the device's port parent identifier
 * @dev: network device
 * @ppid: pointer to a storage for the port's parent identifier
 * @recurse: allow/disallow recursion to lower devices
 *
 * Get the devices's port parent identifier.
 *
 * Return: 0 on success, -errno on failure.
 */
int netif_get_port_parent_id(struct net_device *dev,
			     struct netdev_phys_item_id *ppid, bool recurse)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	struct netdev_phys_item_id first = { };
	struct net_device *lower_dev;
	struct list_head *iter;
	int err;

	if (ops->ndo_get_port_parent_id) {
		err = ops->ndo_get_port_parent_id(dev, ppid);
		if (err != -EOPNOTSUPP)
			return err;
	}

	err = devlink_compat_switch_id_get(dev, ppid);
	if (!recurse || err != -EOPNOTSUPP)
		return err;

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = netif_get_port_parent_id(lower_dev, ppid, true);
		if (err)
			break;
		if (!first.id_len)
			first = *ppid;
		else if (memcmp(&first, ppid, sizeof(*ppid)))
			return -EOPNOTSUPP;
	}

	return err;
}
EXPORT_SYMBOL(netif_get_port_parent_id);

/**
 *	netdev_port_same_parent_id - Indicate if two network devices have
 *	the same port parent identifier
 *	@a: first network device
 *	@b: second network device
 */
bool netdev_port_same_parent_id(struct net_device *a, struct net_device *b)
{
	struct netdev_phys_item_id a_id = { };
	struct netdev_phys_item_id b_id = { };

	if (netif_get_port_parent_id(a, &a_id, true) ||
	    netif_get_port_parent_id(b, &b_id, true))
		return false;

	return netdev_phys_item_id_same(&a_id, &b_id);
}
EXPORT_SYMBOL(netdev_port_same_parent_id);

int netif_change_proto_down(struct net_device *dev, bool proto_down)
{
	if (!dev->change_proto_down)
		return -EOPNOTSUPP;
	if (!netif_device_present(dev))
		return -ENODEV;
	if (proto_down)
		netif_carrier_off(dev);
	else
		netif_carrier_on(dev);
	WRITE_ONCE(dev->proto_down, proto_down);
	return 0;
}

/**
 *	netdev_change_proto_down_reason_locked - proto down reason
 *
 *	@dev: device
 *	@mask: proto down mask
 *	@value: proto down value
 */
void netdev_change_proto_down_reason_locked(struct net_device *dev,
					    unsigned long mask, u32 value)
{
	u32 proto_down_reason;
	int b;

	if (!mask) {
		proto_down_reason = value;
	} else {
		proto_down_reason = dev->proto_down_reason;
		for_each_set_bit(b, &mask, 32) {
			if (value & (1 << b))
				proto_down_reason |= BIT(b);
			else
				proto_down_reason &= ~BIT(b);
		}
	}
	WRITE_ONCE(dev->proto_down_reason, proto_down_reason);
}

struct bpf_xdp_link {
	struct bpf_link link;
	struct net_device *dev; /* protected by rtnl_lock, no refcnt held */
	int flags;
};

static enum bpf_xdp_mode dev_xdp_mode(struct net_device *dev, u32 flags)
{
	if (flags & XDP_FLAGS_HW_MODE)
		return XDP_MODE_HW;
	if (flags & XDP_FLAGS_DRV_MODE)
		return XDP_MODE_DRV;
	if (flags & XDP_FLAGS_SKB_MODE)
		return XDP_MODE_SKB;
	return dev->netdev_ops->ndo_bpf ? XDP_MODE_DRV : XDP_MODE_SKB;
}

static bpf_op_t dev_xdp_bpf_op(struct net_device *dev, enum bpf_xdp_mode mode)
{
	switch (mode) {
	case XDP_MODE_SKB:
		return generic_xdp_install;
	case XDP_MODE_DRV:
	case XDP_MODE_HW:
		return dev->netdev_ops->ndo_bpf;
	default:
		return NULL;
	}
}

static struct bpf_xdp_link *dev_xdp_link(struct net_device *dev,
					 enum bpf_xdp_mode mode)
{
	return dev->xdp_state[mode].link;
}

static struct bpf_prog *dev_xdp_prog(struct net_device *dev,
				     enum bpf_xdp_mode mode)
{
	struct bpf_xdp_link *link = dev_xdp_link(dev, mode);

	if (link)
		return link->link.prog;
	return dev->xdp_state[mode].prog;
}

u8 dev_xdp_prog_count(struct net_device *dev)
{
	u8 count = 0;
	int i;

	for (i = 0; i < __MAX_XDP_MODE; i++)
		if (dev->xdp_state[i].prog || dev->xdp_state[i].link)
			count++;
	return count;
}
EXPORT_SYMBOL_GPL(dev_xdp_prog_count);

u8 dev_xdp_sb_prog_count(struct net_device *dev)
{
	u8 count = 0;
	int i;

	for (i = 0; i < __MAX_XDP_MODE; i++)
		if (dev->xdp_state[i].prog &&
		    !dev->xdp_state[i].prog->aux->xdp_has_frags)
			count++;
	return count;
}

int netif_xdp_propagate(struct net_device *dev, struct netdev_bpf *bpf)
{
	if (!dev->netdev_ops->ndo_bpf)
		return -EOPNOTSUPP;

	if (dev->cfg->hds_config == ETHTOOL_TCP_DATA_SPLIT_ENABLED &&
	    bpf->command == XDP_SETUP_PROG &&
	    bpf->prog && !bpf->prog->aux->xdp_has_frags) {
		NL_SET_ERR_MSG(bpf->extack,
			       "unable to propagate XDP to device using tcp-data-split");
		return -EBUSY;
	}

	if (dev_get_min_mp_channel_count(dev)) {
		NL_SET_ERR_MSG(bpf->extack, "unable to propagate XDP to device using memory provider");
		return -EBUSY;
	}

	return dev->netdev_ops->ndo_bpf(dev, bpf);
}
EXPORT_SYMBOL_GPL(netif_xdp_propagate);

u32 dev_xdp_prog_id(struct net_device *dev, enum bpf_xdp_mode mode)
{
	struct bpf_prog *prog = dev_xdp_prog(dev, mode);

	return prog ? prog->aux->id : 0;
}

static void dev_xdp_set_link(struct net_device *dev, enum bpf_xdp_mode mode,
			     struct bpf_xdp_link *link)
{
	dev->xdp_state[mode].link = link;
	dev->xdp_state[mode].prog = NULL;
}

static void dev_xdp_set_prog(struct net_device *dev, enum bpf_xdp_mode mode,
			     struct bpf_prog *prog)
{
	dev->xdp_state[mode].link = NULL;
	dev->xdp_state[mode].prog = prog;
}

static int dev_xdp_install(struct net_device *dev, enum bpf_xdp_mode mode,
			   bpf_op_t bpf_op, struct netlink_ext_ack *extack,
			   u32 flags, struct bpf_prog *prog)
{
	struct netdev_bpf xdp;
	int err;

	netdev_ops_assert_locked(dev);

	if (dev->cfg->hds_config == ETHTOOL_TCP_DATA_SPLIT_ENABLED &&
	    prog && !prog->aux->xdp_has_frags) {
		NL_SET_ERR_MSG(extack, "unable to install XDP to device using tcp-data-split");
		return -EBUSY;
	}

	if (dev_get_min_mp_channel_count(dev)) {
		NL_SET_ERR_MSG(extack, "unable to install XDP to device using memory provider");
		return -EBUSY;
	}

	memset(&xdp, 0, sizeof(xdp));
	xdp.command = mode == XDP_MODE_HW ? XDP_SETUP_PROG_HW : XDP_SETUP_PROG;
	xdp.extack = extack;
	xdp.flags = flags;
	xdp.prog = prog;

	/* Drivers assume refcnt is already incremented (i.e, prog pointer is
	 * "moved" into driver), so they don't increment it on their own, but
	 * they do decrement refcnt when program is detached or replaced.
	 * Given net_device also owns link/prog, we need to bump refcnt here
	 * to prevent drivers from underflowing it.
	 */
	if (prog)
		bpf_prog_inc(prog);
	err = bpf_op(dev, &xdp);
	if (err) {
		if (prog)
			bpf_prog_put(prog);
		return err;
	}

	if (mode != XDP_MODE_HW)
		bpf_prog_change_xdp(dev_xdp_prog(dev, mode), prog);

	return 0;
}

static void dev_xdp_uninstall(struct net_device *dev)
{
	struct bpf_xdp_link *link;
	struct bpf_prog *prog;
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;

	ASSERT_RTNL();

	for (mode = XDP_MODE_SKB; mode < __MAX_XDP_MODE; mode++) {
		prog = dev_xdp_prog(dev, mode);
		if (!prog)
			continue;

		bpf_op = dev_xdp_bpf_op(dev, mode);
		if (!bpf_op)
			continue;

		WARN_ON(dev_xdp_install(dev, mode, bpf_op, NULL, 0, NULL));

		/* auto-detach link from net device */
		link = dev_xdp_link(dev, mode);
		if (link)
			link->dev = NULL;
		else
			bpf_prog_put(prog);

		dev_xdp_set_link(dev, mode, NULL);
	}
}

static int dev_xdp_attach(struct net_device *dev, struct netlink_ext_ack *extack,
			  struct bpf_xdp_link *link, struct bpf_prog *new_prog,
			  struct bpf_prog *old_prog, u32 flags)
{
	unsigned int num_modes = hweight32(flags & XDP_FLAGS_MODES);
	struct bpf_prog *cur_prog;
	struct net_device *upper;
	struct list_head *iter;
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;
	int err;

	ASSERT_RTNL();

	/* either link or prog attachment, never both */
	if (link && (new_prog || old_prog))
		return -EINVAL;
	/* link supports only XDP mode flags */
	if (link && (flags & ~XDP_FLAGS_MODES)) {
		NL_SET_ERR_MSG(extack, "Invalid XDP flags for BPF link attachment");
		return -EINVAL;
	}
	/* just one XDP mode bit should be set, zero defaults to drv/skb mode */
	if (num_modes > 1) {
		NL_SET_ERR_MSG(extack, "Only one XDP mode flag can be set");
		return -EINVAL;
	}
	/* avoid ambiguity if offload + drv/skb mode progs are both loaded */
	if (!num_modes && dev_xdp_prog_count(dev) > 1) {
		NL_SET_ERR_MSG(extack,
			       "More than one program loaded, unset mode is ambiguous");
		return -EINVAL;
	}
	/* old_prog != NULL implies XDP_FLAGS_REPLACE is set */
	if (old_prog && !(flags & XDP_FLAGS_REPLACE)) {
		NL_SET_ERR_MSG(extack, "XDP_FLAGS_REPLACE is not specified");
		return -EINVAL;
	}

	mode = dev_xdp_mode(dev, flags);
	/* can't replace attached link */
	if (dev_xdp_link(dev, mode)) {
		NL_SET_ERR_MSG(extack, "Can't replace active BPF XDP link");
		return -EBUSY;
	}

	/* don't allow if an upper device already has a program */
	netdev_for_each_upper_dev_rcu(dev, upper, iter) {
		if (dev_xdp_prog_count(upper) > 0) {
			NL_SET_ERR_MSG(extack, "Cannot attach when an upper device already has a program");
			return -EEXIST;
		}
	}

	cur_prog = dev_xdp_prog(dev, mode);
	/* can't replace attached prog with link */
	if (link && cur_prog) {
		NL_SET_ERR_MSG(extack, "Can't replace active XDP program with BPF link");
		return -EBUSY;
	}
	if ((flags & XDP_FLAGS_REPLACE) && cur_prog != old_prog) {
		NL_SET_ERR_MSG(extack, "Active program does not match expected");
		return -EEXIST;
	}

	/* put effective new program into new_prog */
	if (link)
		new_prog = link->link.prog;

	if (new_prog) {
		bool offload = mode == XDP_MODE_HW;
		enum bpf_xdp_mode other_mode = mode == XDP_MODE_SKB
					       ? XDP_MODE_DRV : XDP_MODE_SKB;

		if ((flags & XDP_FLAGS_UPDATE_IF_NOEXIST) && cur_prog) {
			NL_SET_ERR_MSG(extack, "XDP program already attached");
			return -EBUSY;
		}
		if (!offload && dev_xdp_prog(dev, other_mode)) {
			NL_SET_ERR_MSG(extack, "Native and generic XDP can't be active at the same time");
			return -EEXIST;
		}
		if (!offload && bpf_prog_is_offloaded(new_prog->aux)) {
			NL_SET_ERR_MSG(extack, "Using offloaded program without HW_MODE flag is not supported");
			return -EINVAL;
		}
		if (bpf_prog_is_dev_bound(new_prog->aux) && !bpf_offload_dev_match(new_prog, dev)) {
			NL_SET_ERR_MSG(extack, "Program bound to different device");
			return -EINVAL;
		}
		if (bpf_prog_is_dev_bound(new_prog->aux) && mode == XDP_MODE_SKB) {
			NL_SET_ERR_MSG(extack, "Can't attach device-bound programs in generic mode");
			return -EINVAL;
		}
		if (new_prog->expected_attach_type == BPF_XDP_DEVMAP) {
			NL_SET_ERR_MSG(extack, "BPF_XDP_DEVMAP programs can not be attached to a device");
			return -EINVAL;
		}
		if (new_prog->expected_attach_type == BPF_XDP_CPUMAP) {
			NL_SET_ERR_MSG(extack, "BPF_XDP_CPUMAP programs can not be attached to a device");
			return -EINVAL;
		}
	}

	/* don't call drivers if the effective program didn't change */
	if (new_prog != cur_prog) {
		bpf_op = dev_xdp_bpf_op(dev, mode);
		if (!bpf_op) {
			NL_SET_ERR_MSG(extack, "Underlying driver does not support XDP in native mode");
			return -EOPNOTSUPP;
		}

		err = dev_xdp_install(dev, mode, bpf_op, extack, flags, new_prog);
		if (err)
			return err;
	}

	if (link)
		dev_xdp_set_link(dev, mode, link);
	else
		dev_xdp_set_prog(dev, mode, new_prog);
	if (cur_prog)
		bpf_prog_put(cur_prog);

	return 0;
}

static int dev_xdp_attach_link(struct net_device *dev,
			       struct netlink_ext_ack *extack,
			       struct bpf_xdp_link *link)
{
	return dev_xdp_attach(dev, extack, link, NULL, NULL, link->flags);
}

static int dev_xdp_detach_link(struct net_device *dev,
			       struct netlink_ext_ack *extack,
			       struct bpf_xdp_link *link)
{
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;

	ASSERT_RTNL();

	mode = dev_xdp_mode(dev, link->flags);
	if (dev_xdp_link(dev, mode) != link)
		return -EINVAL;

	bpf_op = dev_xdp_bpf_op(dev, mode);
	WARN_ON(dev_xdp_install(dev, mode, bpf_op, NULL, 0, NULL));
	dev_xdp_set_link(dev, mode, NULL);
	return 0;
}

static void bpf_xdp_link_release(struct bpf_link *link)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);

	rtnl_lock();

	/* if racing with net_device's tear down, xdp_link->dev might be
	 * already NULL, in which case link was already auto-detached
	 */
	if (xdp_link->dev) {
		netdev_lock_ops(xdp_link->dev);
		WARN_ON(dev_xdp_detach_link(xdp_link->dev, NULL, xdp_link));
		netdev_unlock_ops(xdp_link->dev);
		xdp_link->dev = NULL;
	}

	rtnl_unlock();
}

static int bpf_xdp_link_detach(struct bpf_link *link)
{
	bpf_xdp_link_release(link);
	return 0;
}

static void bpf_xdp_link_dealloc(struct bpf_link *link)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);

	kfree(xdp_link);
}

static void bpf_xdp_link_show_fdinfo(const struct bpf_link *link,
				     struct seq_file *seq)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);
	u32 ifindex = 0;

	rtnl_lock();
	if (xdp_link->dev)
		ifindex = xdp_link->dev->ifindex;
	rtnl_unlock();

	seq_printf(seq, "ifindex:\t%u\n", ifindex);
}

static int bpf_xdp_link_fill_link_info(const struct bpf_link *link,
				       struct bpf_link_info *info)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);
	u32 ifindex = 0;

	rtnl_lock();
	if (xdp_link->dev)
		ifindex = xdp_link->dev->ifindex;
	rtnl_unlock();

	info->xdp.ifindex = ifindex;
	return 0;
}

static int bpf_xdp_link_update(struct bpf_link *link, struct bpf_prog *new_prog,
			       struct bpf_prog *old_prog)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;
	int err = 0;

	rtnl_lock();

	/* link might have been auto-released already, so fail */
	if (!xdp_link->dev) {
		err = -ENOLINK;
		goto out_unlock;
	}

	if (old_prog && link->prog != old_prog) {
		err = -EPERM;
		goto out_unlock;
	}
	old_prog = link->prog;
	if (old_prog->type != new_prog->type ||
	    old_prog->expected_attach_type != new_prog->expected_attach_type) {
		err = -EINVAL;
		goto out_unlock;
	}

	if (old_prog == new_prog) {
		/* no-op, don't disturb drivers */
		bpf_prog_put(new_prog);
		goto out_unlock;
	}

	netdev_lock_ops(xdp_link->dev);
	mode = dev_xdp_mode(xdp_link->dev, xdp_link->flags);
	bpf_op = dev_xdp_bpf_op(xdp_link->dev, mode);
	err = dev_xdp_install(xdp_link->dev, mode, bpf_op, NULL,
			      xdp_link->flags, new_prog);
	netdev_unlock_ops(xdp_link->dev);
	if (err)
		goto out_unlock;

	old_prog = xchg(&link->prog, new_prog);
	bpf_prog_put(old_prog);

out_unlock:
	rtnl_unlock();
	return err;
}

static const struct bpf_link_ops bpf_xdp_link_lops = {
	.release = bpf_xdp_link_release,
	.dealloc = bpf_xdp_link_dealloc,
	.detach = bpf_xdp_link_detach,
	.show_fdinfo = bpf_xdp_link_show_fdinfo,
	.fill_link_info = bpf_xdp_link_fill_link_info,
	.update_prog = bpf_xdp_link_update,
};

int bpf_xdp_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct net *net = current->nsproxy->net_ns;
	struct bpf_link_primer link_primer;
	struct netlink_ext_ack extack = {};
	struct bpf_xdp_link *link;
	struct net_device *dev;
	int err, fd;

	rtnl_lock();
	dev = dev_get_by_index(net, attr->link_create.target_ifindex);
	if (!dev) {
		rtnl_unlock();
		return -EINVAL;
	}

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto unlock;
	}

	bpf_link_init(&link->link, BPF_LINK_TYPE_XDP, &bpf_xdp_link_lops, prog,
		      attr->link_create.attach_type);
	link->dev = dev;
	link->flags = attr->link_create.flags;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		goto unlock;
	}

	netdev_lock_ops(dev);
	err = dev_xdp_attach_link(dev, &extack, link);
	netdev_unlock_ops(dev);
	rtnl_unlock();

	if (err) {
		link->dev = NULL;
		bpf_link_cleanup(&link_primer);
		trace_bpf_xdp_link_attach_failed(extack._msg);
		goto out_put_dev;
	}

	fd = bpf_link_settle(&link_primer);
	/* link itself doesn't hold dev's refcnt to not complicate shutdown */
	dev_put(dev);
	return fd;

unlock:
	rtnl_unlock();

out_put_dev:
	dev_put(dev);
	return err;
}

/**
 *	dev_change_xdp_fd - set or clear a bpf program for a device rx path
 *	@dev: device
 *	@extack: netlink extended ack
 *	@fd: new program fd or negative value to clear
 *	@expected_fd: old program fd that userspace expects to replace or clear
 *	@flags: xdp-related flags
 *
 *	Set or clear a bpf program for a device
 */
int dev_change_xdp_fd(struct net_device *dev, struct netlink_ext_ack *extack,
		      int fd, int expected_fd, u32 flags)
{
	enum bpf_xdp_mode mode = dev_xdp_mode(dev, flags);
	struct bpf_prog *new_prog = NULL, *old_prog = NULL;
	int err;

	ASSERT_RTNL();

	if (fd >= 0) {
		new_prog = bpf_prog_get_type_dev(fd, BPF_PROG_TYPE_XDP,
						 mode != XDP_MODE_SKB);
		if (IS_ERR(new_prog))
			return PTR_ERR(new_prog);
	}

	if (expected_fd >= 0) {
		old_prog = bpf_prog_get_type_dev(expected_fd, BPF_PROG_TYPE_XDP,
						 mode != XDP_MODE_SKB);
		if (IS_ERR(old_prog)) {
			err = PTR_ERR(old_prog);
			old_prog = NULL;
			goto err_out;
		}
	}

	err = dev_xdp_attach(dev, extack, NULL, new_prog, old_prog, flags);

err_out:
	if (err && new_prog)
		bpf_prog_put(new_prog);
	if (old_prog)
		bpf_prog_put(old_prog);
	return err;
}

u32 dev_get_min_mp_channel_count(const struct net_device *dev)
{
	int i;

	netdev_ops_assert_locked(dev);

	for (i = dev->real_num_rx_queues - 1; i >= 0; i--)
		if (dev->_rx[i].mp_params.mp_priv)
			/* The channel count is the idx plus 1. */
			return i + 1;

	return 0;
}

/**
 * dev_index_reserve() - allocate an ifindex in a namespace
 * @net: the applicable net namespace
 * @ifindex: requested ifindex, pass %0 to get one allocated
 *
 * Allocate a ifindex for a new device. Caller must either use the ifindex
 * to store the device (via list_netdevice()) or call dev_index_release()
 * to give the index up.
 *
 * Return: a suitable unique value for a new device interface number or -errno.
 */
static int dev_index_reserve(struct net *net, u32 ifindex)
{
	int err;

	if (ifindex > INT_MAX) {
		DEBUG_NET_WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (!ifindex)
		err = xa_alloc_cyclic(&net->dev_by_index, &ifindex, NULL,
				      xa_limit_31b, &net->ifindex, GFP_KERNEL);
	else
		err = xa_insert(&net->dev_by_index, ifindex, NULL, GFP_KERNEL);
	if (err < 0)
		return err;

	return ifindex;
}

static void dev_index_release(struct net *net, int ifindex)
{
	/* Expect only unused indexes, unlist_netdevice() removes the used */
	WARN_ON(xa_erase(&net->dev_by_index, ifindex));
}

static bool from_cleanup_net(void)
{
#ifdef CONFIG_NET_NS
	return current == READ_ONCE(cleanup_net_task);
#else
	return false;
#endif
}

/* Delayed registration/unregisteration */
LIST_HEAD(net_todo_list);
DECLARE_WAIT_QUEUE_HEAD(netdev_unregistering_wq);
atomic_t dev_unreg_count = ATOMIC_INIT(0);

static void net_set_todo(struct net_device *dev)
{
	list_add_tail(&dev->todo_list, &net_todo_list);
}

static netdev_features_t netdev_sync_upper_features(struct net_device *lower,
	struct net_device *upper, netdev_features_t features)
{
	netdev_features_t upper_disables = NETIF_F_UPPER_DISABLES;
	netdev_features_t feature;
	int feature_bit;

	for_each_netdev_feature(upper_disables, feature_bit) {
		feature = __NETIF_F_BIT(feature_bit);
		if (!(upper->wanted_features & feature)
		    && (features & feature)) {
			netdev_dbg(lower, "Dropping feature %pNF, upper dev %s has it off.\n",
				   &feature, upper->name);
			features &= ~feature;
		}
	}

	return features;
}

static void netdev_sync_lower_features(struct net_device *upper,
	struct net_device *lower, netdev_features_t features)
{
	netdev_features_t upper_disables = NETIF_F_UPPER_DISABLES;
	netdev_features_t feature;
	int feature_bit;

	for_each_netdev_feature(upper_disables, feature_bit) {
		feature = __NETIF_F_BIT(feature_bit);
		if (!(features & feature) && (lower->features & feature)) {
			netdev_dbg(upper, "Disabling feature %pNF on lower dev %s.\n",
				   &feature, lower->name);
			netdev_lock_ops(lower);
			lower->wanted_features &= ~feature;
			__netdev_update_features(lower);

			if (unlikely(lower->features & feature))
				netdev_WARN(upper, "failed to disable %pNF on %s!\n",
					    &feature, lower->name);
			else
				netdev_features_change(lower);
			netdev_unlock_ops(lower);
		}
	}
}

static bool netdev_has_ip_or_hw_csum(netdev_features_t features)
{
	netdev_features_t ip_csum_mask = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	bool ip_csum = (features & ip_csum_mask) == ip_csum_mask;
	bool hw_csum = features & NETIF_F_HW_CSUM;

	return ip_csum || hw_csum;
}

static netdev_features_t netdev_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	/* Fix illegal checksum combinations */
	if ((features & NETIF_F_HW_CSUM) &&
	    (features & (NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM))) {
		netdev_warn(dev, "mixed HW and IP checksum settings.\n");
		features &= ~(NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM);
	}

	/* TSO requires that SG is present as well. */
	if ((features & NETIF_F_ALL_TSO) && !(features & NETIF_F_SG)) {
		netdev_dbg(dev, "Dropping TSO features since no SG feature.\n");
		features &= ~NETIF_F_ALL_TSO;
	}

	if ((features & NETIF_F_TSO) && !(features & NETIF_F_HW_CSUM) &&
					!(features & NETIF_F_IP_CSUM)) {
		netdev_dbg(dev, "Dropping TSO features since no CSUM feature.\n");
		features &= ~NETIF_F_TSO;
		features &= ~NETIF_F_TSO_ECN;
	}

	if ((features & NETIF_F_TSO6) && !(features & NETIF_F_HW_CSUM) &&
					 !(features & NETIF_F_IPV6_CSUM)) {
		netdev_dbg(dev, "Dropping TSO6 features since no CSUM feature.\n");
		features &= ~NETIF_F_TSO6;
	}

	/* TSO with IPv4 ID mangling requires IPv4 TSO be enabled */
	if ((features & NETIF_F_TSO_MANGLEID) && !(features & NETIF_F_TSO))
		features &= ~NETIF_F_TSO_MANGLEID;

	/* TSO ECN requires that TSO is present as well. */
	if ((features & NETIF_F_ALL_TSO) == NETIF_F_TSO_ECN)
		features &= ~NETIF_F_TSO_ECN;

	/* Software GSO depends on SG. */
	if ((features & NETIF_F_GSO) && !(features & NETIF_F_SG)) {
		netdev_dbg(dev, "Dropping NETIF_F_GSO since no SG feature.\n");
		features &= ~NETIF_F_GSO;
	}

	/* GSO partial features require GSO partial be set */
	if ((features & dev->gso_partial_features) &&
	    !(features & NETIF_F_GSO_PARTIAL)) {
		netdev_dbg(dev,
			   "Dropping partially supported GSO features since no GSO partial.\n");
		features &= ~dev->gso_partial_features;
	}

	if (!(features & NETIF_F_RXCSUM)) {
		/* NETIF_F_GRO_HW implies doing RXCSUM since every packet
		 * successfully merged by hardware must also have the
		 * checksum verified by hardware.  If the user does not
		 * want to enable RXCSUM, logically, we should disable GRO_HW.
		 */
		if (features & NETIF_F_GRO_HW) {
			netdev_dbg(dev, "Dropping NETIF_F_GRO_HW since no RXCSUM feature.\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	/* LRO/HW-GRO features cannot be combined with RX-FCS */
	if (features & NETIF_F_RXFCS) {
		if (features & NETIF_F_LRO) {
			netdev_dbg(dev, "Dropping LRO feature since RX-FCS is requested.\n");
			features &= ~NETIF_F_LRO;
		}

		if (features & NETIF_F_GRO_HW) {
			netdev_dbg(dev, "Dropping HW-GRO feature since RX-FCS is requested.\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	if ((features & NETIF_F_GRO_HW) && (features & NETIF_F_LRO)) {
		netdev_dbg(dev, "Dropping LRO feature since HW-GRO is requested.\n");
		features &= ~NETIF_F_LRO;
	}

	if ((features & NETIF_F_HW_TLS_TX) && !netdev_has_ip_or_hw_csum(features)) {
		netdev_dbg(dev, "Dropping TLS TX HW offload feature since no CSUM feature.\n");
		features &= ~NETIF_F_HW_TLS_TX;
	}

	if ((features & NETIF_F_HW_TLS_RX) && !(features & NETIF_F_RXCSUM)) {
		netdev_dbg(dev, "Dropping TLS RX HW offload feature since no RXCSUM feature.\n");
		features &= ~NETIF_F_HW_TLS_RX;
	}

	if ((features & NETIF_F_GSO_UDP_L4) && !netdev_has_ip_or_hw_csum(features)) {
		netdev_dbg(dev, "Dropping USO feature since no CSUM feature.\n");
		features &= ~NETIF_F_GSO_UDP_L4;
	}

	return features;
}

int __netdev_update_features(struct net_device *dev)
{
	struct net_device *upper, *lower;
	netdev_features_t features;
	struct list_head *iter;
	int err = -1;

	ASSERT_RTNL();
	netdev_ops_assert_locked(dev);

	features = netdev_get_wanted_features(dev);

	if (dev->netdev_ops->ndo_fix_features)
		features = dev->netdev_ops->ndo_fix_features(dev, features);

	/* driver might be less strict about feature dependencies */
	features = netdev_fix_features(dev, features);

	/* some features can't be enabled if they're off on an upper device */
	netdev_for_each_upper_dev_rcu(dev, upper, iter)
		features = netdev_sync_upper_features(dev, upper, features);

	if (dev->features == features)
		goto sync_lower;

	netdev_dbg(dev, "Features changed: %pNF -> %pNF\n",
		&dev->features, &features);

	if (dev->netdev_ops->ndo_set_features)
		err = dev->netdev_ops->ndo_set_features(dev, features);
	else
		err = 0;

	if (unlikely(err < 0)) {
		netdev_err(dev,
			"set_features() failed (%d); wanted %pNF, left %pNF\n",
			err, &features, &dev->features);
		/* return non-0 since some features might have changed and
		 * it's better to fire a spurious notification than miss it
		 */
		return -1;
	}

sync_lower:
	/* some features must be disabled on lower devices when disabled
	 * on an upper device (think: bonding master or bridge)
	 */
	netdev_for_each_lower_dev(dev, lower, iter)
		netdev_sync_lower_features(dev, lower, features);

	if (!err) {
		netdev_features_t diff = features ^ dev->features;

		if (diff & NETIF_F_RX_UDP_TUNNEL_PORT) {
			/* udp_tunnel_{get,drop}_rx_info both need
			 * NETIF_F_RX_UDP_TUNNEL_PORT enabled on the
			 * device, or they won't do anything.
			 * Thus we need to update dev->features
			 * *before* calling udp_tunnel_get_rx_info,
			 * but *after* calling udp_tunnel_drop_rx_info.
			 */
			udp_tunnel_nic_lock(dev);
			if (features & NETIF_F_RX_UDP_TUNNEL_PORT) {
				dev->features = features;
				udp_tunnel_get_rx_info(dev);
			} else {
				udp_tunnel_drop_rx_info(dev);
			}
			udp_tunnel_nic_unlock(dev);
		}

		if (diff & NETIF_F_HW_VLAN_CTAG_FILTER) {
			if (features & NETIF_F_HW_VLAN_CTAG_FILTER) {
				dev->features = features;
				err |= vlan_get_rx_ctag_filter_info(dev);
			} else {
				vlan_drop_rx_ctag_filter_info(dev);
			}
		}

		if (diff & NETIF_F_HW_VLAN_STAG_FILTER) {
			if (features & NETIF_F_HW_VLAN_STAG_FILTER) {
				dev->features = features;
				err |= vlan_get_rx_stag_filter_info(dev);
			} else {
				vlan_drop_rx_stag_filter_info(dev);
			}
		}

		dev->features = features;
	}

	return err < 0 ? 0 : 1;
}

/**
 *	netdev_update_features - recalculate device features
 *	@dev: the device to check
 *
 *	Recalculate dev->features set and send notifications if it
 *	has changed. Should be called after driver or hardware dependent
 *	conditions might have changed that influence the features.
 */
void netdev_update_features(struct net_device *dev)
{
	if (__netdev_update_features(dev))
		netdev_features_change(dev);
}
EXPORT_SYMBOL(netdev_update_features);

/**
 *	netdev_change_features - recalculate device features
 *	@dev: the device to check
 *
 *	Recalculate dev->features set and send notifications even
 *	if they have not changed. Should be called instead of
 *	netdev_update_features() if also dev->vlan_features might
 *	have changed to allow the changes to be propagated to stacked
 *	VLAN devices.
 */
void netdev_change_features(struct net_device *dev)
{
	__netdev_update_features(dev);
	netdev_features_change(dev);
}
EXPORT_SYMBOL(netdev_change_features);

/**
 *	netif_stacked_transfer_operstate -	transfer operstate
 *	@rootdev: the root or lower level device to transfer state from
 *	@dev: the device to transfer operstate to
 *
 *	Transfer operational state from root to device. This is normally
 *	called when a stacking relationship exists between the root
 *	device and the device(a leaf device).
 */
void netif_stacked_transfer_operstate(const struct net_device *rootdev,
					struct net_device *dev)
{
	if (rootdev->operstate == IF_OPER_DORMANT)
		netif_dormant_on(dev);
	else
		netif_dormant_off(dev);

	if (rootdev->operstate == IF_OPER_TESTING)
		netif_testing_on(dev);
	else
		netif_testing_off(dev);

	if (netif_carrier_ok(rootdev))
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
}
EXPORT_SYMBOL(netif_stacked_transfer_operstate);

static int netif_alloc_rx_queues(struct net_device *dev)
{
	unsigned int i, count = dev->num_rx_queues;
	struct netdev_rx_queue *rx;
	size_t sz = count * sizeof(*rx);
	int err = 0;

	BUG_ON(count < 1);

	rx = kvzalloc(sz, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!rx)
		return -ENOMEM;

	dev->_rx = rx;

	for (i = 0; i < count; i++) {
		rx[i].dev = dev;

		/* XDP RX-queue setup */
		err = xdp_rxq_info_reg(&rx[i].xdp_rxq, dev, i, 0);
		if (err < 0)
			goto err_rxq_info;
	}
	return 0;

err_rxq_info:
	/* Rollback successful reg's and free other resources */
	while (i--)
		xdp_rxq_info_unreg(&rx[i].xdp_rxq);
	kvfree(dev->_rx);
	dev->_rx = NULL;
	return err;
}

static void netif_free_rx_queues(struct net_device *dev)
{
	unsigned int i, count = dev->num_rx_queues;

	/* netif_alloc_rx_queues alloc failed, resources have been unreg'ed */
	if (!dev->_rx)
		return;

	for (i = 0; i < count; i++)
		xdp_rxq_info_unreg(&dev->_rx[i].xdp_rxq);

	kvfree(dev->_rx);
}

static void netdev_init_one_queue(struct net_device *dev,
				  struct netdev_queue *queue, void *_unused)
{
	/* Initialize queue lock */
	spin_lock_init(&queue->_xmit_lock);
	netdev_set_xmit_lockdep_class(&queue->_xmit_lock, dev->type);
	queue->xmit_lock_owner = -1;
	netdev_queue_numa_node_write(queue, NUMA_NO_NODE);
	queue->dev = dev;
#ifdef CONFIG_BQL
	dql_init(&queue->dql, HZ);
#endif
}

static void netif_free_tx_queues(struct net_device *dev)
{
	kvfree(dev->_tx);
}

static int netif_alloc_netdev_queues(struct net_device *dev)
{
	unsigned int count = dev->num_tx_queues;
	struct netdev_queue *tx;
	size_t sz = count * sizeof(*tx);

	if (count < 1 || count > 0xffff)
		return -EINVAL;

	tx = kvzalloc(sz, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!tx)
		return -ENOMEM;

	dev->_tx = tx;

	netdev_for_each_tx_queue(dev, netdev_init_one_queue, NULL);
	spin_lock_init(&dev->tx_global_lock);

	return 0;
}

void netif_tx_stop_all_queues(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		netif_tx_stop_queue(txq);
	}
}
EXPORT_SYMBOL(netif_tx_stop_all_queues);

static int netdev_do_alloc_pcpu_stats(struct net_device *dev)
{
	void __percpu *v;

	/* Drivers implementing ndo_get_peer_dev must support tstat
	 * accounting, so that skb_do_redirect() can bump the dev's
	 * RX stats upon network namespace switch.
	 */
	if (dev->netdev_ops->ndo_get_peer_dev &&
	    dev->pcpu_stat_type != NETDEV_PCPU_STAT_TSTATS)
		return -EOPNOTSUPP;

	switch (dev->pcpu_stat_type) {
	case NETDEV_PCPU_STAT_NONE:
		return 0;
	case NETDEV_PCPU_STAT_LSTATS:
		v = dev->lstats = netdev_alloc_pcpu_stats(struct pcpu_lstats);
		break;
	case NETDEV_PCPU_STAT_TSTATS:
		v = dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
		break;
	case NETDEV_PCPU_STAT_DSTATS:
		v = dev->dstats = netdev_alloc_pcpu_stats(struct pcpu_dstats);
		break;
	default:
		return -EINVAL;
	}

	return v ? 0 : -ENOMEM;
}

static void netdev_do_free_pcpu_stats(struct net_device *dev)
{
	switch (dev->pcpu_stat_type) {
	case NETDEV_PCPU_STAT_NONE:
		return;
	case NETDEV_PCPU_STAT_LSTATS:
		free_percpu(dev->lstats);
		break;
	case NETDEV_PCPU_STAT_TSTATS:
		free_percpu(dev->tstats);
		break;
	case NETDEV_PCPU_STAT_DSTATS:
		free_percpu(dev->dstats);
		break;
	}
}

static void netdev_free_phy_link_topology(struct net_device *dev)
{
	struct phy_link_topology *topo = dev->link_topo;

	if (IS_ENABLED(CONFIG_PHYLIB) && topo) {
		xa_destroy(&topo->phys);
		kfree(topo);
		dev->link_topo = NULL;
	}
}

/**
 * register_netdevice() - register a network device
 * @dev: device to register
 *
 * Take a prepared network device structure and make it externally accessible.
 * A %NETDEV_REGISTER message is sent to the netdev notifier chain.
 * Callers must hold the rtnl lock - you may want register_netdev()
 * instead of this.
 */
int register_netdevice(struct net_device *dev)
{
	int ret;
	struct net *net = dev_net(dev);

	BUILD_BUG_ON(sizeof(netdev_features_t) * BITS_PER_BYTE <
		     NETDEV_FEATURE_COUNT);
	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	might_sleep();

	/* When net_device's are persistent, this will be fatal. */
	BUG_ON(dev->reg_state != NETREG_UNINITIALIZED);
	BUG_ON(!net);

	ret = ethtool_check_ops(dev->ethtool_ops);
	if (ret)
		return ret;

	/* rss ctx ID 0 is reserved for the default context, start from 1 */
	xa_init_flags(&dev->ethtool->rss_ctx, XA_FLAGS_ALLOC1);
	mutex_init(&dev->ethtool->rss_lock);

	spin_lock_init(&dev->addr_list_lock);
	netdev_set_addr_lockdep_class(dev);

	ret = dev_get_valid_name(net, dev, dev->name);
	if (ret < 0)
		goto out;

	ret = -ENOMEM;
	dev->name_node = netdev_name_node_head_alloc(dev);
	if (!dev->name_node)
		goto out;

	/* Init, if this function is available */
	if (dev->netdev_ops->ndo_init) {
		ret = dev->netdev_ops->ndo_init(dev);
		if (ret) {
			if (ret > 0)
				ret = -EIO;
			goto err_free_name;
		}
	}

	if (((dev->hw_features | dev->features) &
	     NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    (!dev->netdev_ops->ndo_vlan_rx_add_vid ||
	     !dev->netdev_ops->ndo_vlan_rx_kill_vid)) {
		netdev_WARN(dev, "Buggy VLAN acceleration in driver!\n");
		ret = -EINVAL;
		goto err_uninit;
	}

	ret = netdev_do_alloc_pcpu_stats(dev);
	if (ret)
		goto err_uninit;

	ret = dev_index_reserve(net, dev->ifindex);
	if (ret < 0)
		goto err_free_pcpu;
	dev->ifindex = ret;

	/* Transfer changeable features to wanted_features and enable
	 * software offloads (GSO and GRO).
	 */
	dev->hw_features |= (NETIF_F_SOFT_FEATURES | NETIF_F_SOFT_FEATURES_OFF);
	dev->features |= NETIF_F_SOFT_FEATURES;

	if (dev->udp_tunnel_nic_info) {
		dev->features |= NETIF_F_RX_UDP_TUNNEL_PORT;
		dev->hw_features |= NETIF_F_RX_UDP_TUNNEL_PORT;
	}

	dev->wanted_features = dev->features & dev->hw_features;

	if (!(dev->flags & IFF_LOOPBACK))
		dev->hw_features |= NETIF_F_NOCACHE_COPY;

	/* If IPv4 TCP segmentation offload is supported we should also
	 * allow the device to enable segmenting the frame with the option
	 * of ignoring a static IP ID value.  This doesn't enable the
	 * feature itself but allows the user to enable it later.
	 */
	if (dev->hw_features & NETIF_F_TSO)
		dev->hw_features |= NETIF_F_TSO_MANGLEID;
	if (dev->vlan_features & NETIF_F_TSO)
		dev->vlan_features |= NETIF_F_TSO_MANGLEID;
	if (dev->mpls_features & NETIF_F_TSO)
		dev->mpls_features |= NETIF_F_TSO_MANGLEID;
	if (dev->hw_enc_features & NETIF_F_TSO)
		dev->hw_enc_features |= NETIF_F_TSO_MANGLEID;

	/* Make NETIF_F_HIGHDMA inheritable to VLAN devices.
	 */
	dev->vlan_features |= NETIF_F_HIGHDMA;

	/* Make NETIF_F_SG inheritable to tunnel devices.
	 */
	dev->hw_enc_features |= NETIF_F_SG | NETIF_F_GSO_PARTIAL;

	/* Make NETIF_F_SG inheritable to MPLS.
	 */
	dev->mpls_features |= NETIF_F_SG;

	ret = call_netdevice_notifiers(NETDEV_POST_INIT, dev);
	ret = notifier_to_errno(ret);
	if (ret)
		goto err_ifindex_release;

	ret = netdev_register_kobject(dev);

	netdev_lock(dev);
	WRITE_ONCE(dev->reg_state, ret ? NETREG_UNREGISTERED : NETREG_REGISTERED);
	netdev_unlock(dev);

	if (ret)
		goto err_uninit_notify;

	netdev_lock_ops(dev);
	__netdev_update_features(dev);
	netdev_unlock_ops(dev);

	/*
	 *	Default initial state at registry is that the
	 *	device is present.
	 */

	set_bit(__LINK_STATE_PRESENT, &dev->state);

	linkwatch_init_dev(dev);

	dev_init_scheduler(dev);

	netdev_hold(dev, &dev->dev_registered_tracker, GFP_KERNEL);
	list_netdevice(dev);

	add_device_randomness(dev->dev_addr, dev->addr_len);

	/* If the device has permanent device address, driver should
	 * set dev_addr and also addr_assign_type should be set to
	 * NET_ADDR_PERM (default value).
	 */
	if (dev->addr_assign_type == NET_ADDR_PERM)
		memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	/* Notify protocols, that a new device appeared. */
	netdev_lock_ops(dev);
	ret = call_netdevice_notifiers(NETDEV_REGISTER, dev);
	netdev_unlock_ops(dev);
	ret = notifier_to_errno(ret);
	if (ret) {
		/* Expect explicit free_netdev() on failure */
		dev->needs_free_netdev = false;
		unregister_netdevice_queue(dev, NULL);
		goto out;
	}
	/*
	 *	Prevent userspace races by waiting until the network
	 *	device is fully setup before sending notifications.
	 */
	if (!(dev->rtnl_link_ops && dev->rtnl_link_initializing))
		rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U, GFP_KERNEL, 0, NULL);

out:
	return ret;

err_uninit_notify:
	call_netdevice_notifiers(NETDEV_PRE_UNINIT, dev);
err_ifindex_release:
	dev_index_release(net, dev->ifindex);
err_free_pcpu:
	netdev_do_free_pcpu_stats(dev);
err_uninit:
	if (dev->netdev_ops->ndo_uninit)
		dev->netdev_ops->ndo_uninit(dev);
	if (dev->priv_destructor)
		dev->priv_destructor(dev);
err_free_name:
	netdev_name_node_free(dev->name_node);
	goto out;
}
EXPORT_SYMBOL(register_netdevice);

/* Initialize the core of a dummy net device.
 * The setup steps dummy netdevs need which normal netdevs get by going
 * through register_netdevice().
 */
static void init_dummy_netdev(struct net_device *dev)
{
	/* make sure we BUG if trying to hit standard
	 * register/unregister code path
	 */
	dev->reg_state = NETREG_DUMMY;

	/* a dummy interface is started by default */
	set_bit(__LINK_STATE_PRESENT, &dev->state);
	set_bit(__LINK_STATE_START, &dev->state);

	/* Note : We dont allocate pcpu_refcnt for dummy devices,
	 * because users of this 'device' dont need to change
	 * its refcount.
	 */
}

/**
 *	register_netdev	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	This is a wrapper around register_netdevice that takes the rtnl semaphore
 *	and expands the device name if you passed a format string to
 *	alloc_netdev.
 */
int register_netdev(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	int err;

	if (rtnl_net_lock_killable(net))
		return -EINTR;

	err = register_netdevice(dev);

	rtnl_net_unlock(net);

	return err;
}
EXPORT_SYMBOL(register_netdev);

int netdev_refcnt_read(const struct net_device *dev)
{
#ifdef CONFIG_PCPU_DEV_REFCNT
	int i, refcnt = 0;

	for_each_possible_cpu(i)
		refcnt += *per_cpu_ptr(dev->pcpu_refcnt, i);
	return refcnt;
#else
	return refcount_read(&dev->dev_refcnt);
#endif
}
EXPORT_SYMBOL(netdev_refcnt_read);

int netdev_unregister_timeout_secs __read_mostly = 10;

#define WAIT_REFS_MIN_MSECS 1
#define WAIT_REFS_MAX_MSECS 250
/**
 * netdev_wait_allrefs_any - wait until all references are gone.
 * @list: list of net_devices to wait on
 *
 * This is called when unregistering network devices.
 *
 * Any protocol or device that holds a reference should register
 * for netdevice notification, and cleanup and put back the
 * reference if they receive an UNREGISTER event.
 * We can get stuck here if buggy protocols don't correctly
 * call dev_put.
 */
static struct net_device *netdev_wait_allrefs_any(struct list_head *list)
{
	unsigned long rebroadcast_time, warning_time;
	struct net_device *dev;
	int wait = 0;

	rebroadcast_time = warning_time = jiffies;

	list_for_each_entry(dev, list, todo_list)
		if (netdev_refcnt_read(dev) == 1)
			return dev;

	while (true) {
		if (time_after(jiffies, rebroadcast_time + 1 * HZ)) {
			rtnl_lock();

			/* Rebroadcast unregister notification */
			list_for_each_entry(dev, list, todo_list)
				call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

			__rtnl_unlock();
			rcu_barrier();
			rtnl_lock();

			list_for_each_entry(dev, list, todo_list)
				if (test_bit(__LINK_STATE_LINKWATCH_PENDING,
					     &dev->state)) {
					/* We must not have linkwatch events
					 * pending on unregister. If this
					 * happens, we simply run the queue
					 * unscheduled, resulting in a noop
					 * for this device.
					 */
					linkwatch_run_queue();
					break;
				}

			__rtnl_unlock();

			rebroadcast_time = jiffies;
		}

		rcu_barrier();

		if (!wait) {
			wait = WAIT_REFS_MIN_MSECS;
		} else {
			msleep(wait);
			wait = min(wait << 1, WAIT_REFS_MAX_MSECS);
		}

		list_for_each_entry(dev, list, todo_list)
			if (netdev_refcnt_read(dev) == 1)
				return dev;

		if (time_after(jiffies, warning_time +
			       READ_ONCE(netdev_unregister_timeout_secs) * HZ)) {
			list_for_each_entry(dev, list, todo_list) {
				pr_emerg("unregister_netdevice: waiting for %s to become free. Usage count = %d\n",
					 dev->name, netdev_refcnt_read(dev));
				ref_tracker_dir_print(&dev->refcnt_tracker, 10);
			}

			warning_time = jiffies;
		}
	}
}

/* The sequence is:
 *
 *	rtnl_lock();
 *	...
 *	register_netdevice(x1);
 *	register_netdevice(x2);
 *	...
 *	unregister_netdevice(y1);
 *	unregister_netdevice(y2);
 *      ...
 *	rtnl_unlock();
 *	free_netdev(y1);
 *	free_netdev(y2);
 *
 * We are invoked by rtnl_unlock().
 * This allows us to deal with problems:
 * 1) We can delete sysfs objects which invoke hotplug
 *    without deadlocking with linkwatch via keventd.
 * 2) Since we run with the RTNL semaphore not held, we can sleep
 *    safely in order to wait for the netdev refcnt to drop to zero.
 *
 * We must not return until all unregister events added during
 * the interval the lock was held have been completed.
 */
void netdev_run_todo(void)
{
	struct net_device *dev, *tmp;
	struct list_head list;
	int cnt;
#ifdef CONFIG_LOCKDEP
	struct list_head unlink_list;

	list_replace_init(&net_unlink_list, &unlink_list);

	while (!list_empty(&unlink_list)) {
		dev = list_first_entry(&unlink_list, struct net_device,
				       unlink_list);
		list_del_init(&dev->unlink_list);
		dev->nested_level = dev->lower_level - 1;
	}
#endif

	/* Snapshot list, allow later requests */
	list_replace_init(&net_todo_list, &list);

	__rtnl_unlock();

	/* Wait for rcu callbacks to finish before next phase */
	if (!list_empty(&list))
		rcu_barrier();

	list_for_each_entry_safe(dev, tmp, &list, todo_list) {
		if (unlikely(dev->reg_state != NETREG_UNREGISTERING)) {
			netdev_WARN(dev, "run_todo but not unregistering\n");
			list_del(&dev->todo_list);
			continue;
		}

		netdev_lock(dev);
		WRITE_ONCE(dev->reg_state, NETREG_UNREGISTERED);
		netdev_unlock(dev);
		linkwatch_sync_dev(dev);
	}

	cnt = 0;
	while (!list_empty(&list)) {
		dev = netdev_wait_allrefs_any(&list);
		list_del(&dev->todo_list);

		/* paranoia */
		BUG_ON(netdev_refcnt_read(dev) != 1);
		BUG_ON(!list_empty(&dev->ptype_all));
		BUG_ON(!list_empty(&dev->ptype_specific));
		WARN_ON(rcu_access_pointer(dev->ip_ptr));
		WARN_ON(rcu_access_pointer(dev->ip6_ptr));

		netdev_do_free_pcpu_stats(dev);
		if (dev->priv_destructor)
			dev->priv_destructor(dev);
		if (dev->needs_free_netdev)
			free_netdev(dev);

		cnt++;

		/* Free network device */
		kobject_put(&dev->dev.kobj);
	}
	if (cnt && atomic_sub_and_test(cnt, &dev_unreg_count))
		wake_up(&netdev_unregistering_wq);
}

/* Collate per-cpu network dstats statistics
 *
 * Read per-cpu network statistics from dev->dstats and populate the related
 * fields in @s.
 */
static void dev_fetch_dstats(struct rtnl_link_stats64 *s,
			     const struct pcpu_dstats __percpu *dstats)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		u64 rx_packets, rx_bytes, rx_drops;
		u64 tx_packets, tx_bytes, tx_drops;
		const struct pcpu_dstats *stats;
		unsigned int start;

		stats = per_cpu_ptr(dstats, cpu);
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			rx_packets = u64_stats_read(&stats->rx_packets);
			rx_bytes   = u64_stats_read(&stats->rx_bytes);
			rx_drops   = u64_stats_read(&stats->rx_drops);
			tx_packets = u64_stats_read(&stats->tx_packets);
			tx_bytes   = u64_stats_read(&stats->tx_bytes);
			tx_drops   = u64_stats_read(&stats->tx_drops);
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		s->rx_packets += rx_packets;
		s->rx_bytes   += rx_bytes;
		s->rx_dropped += rx_drops;
		s->tx_packets += tx_packets;
		s->tx_bytes   += tx_bytes;
		s->tx_dropped += tx_drops;
	}
}

/* ndo_get_stats64 implementation for dtstats-based accounting.
 *
 * Populate @s from dev->stats and dev->dstats. This is used internally by the
 * core for NETDEV_PCPU_STAT_DSTAT-type stats collection.
 */
static void dev_get_dstats64(const struct net_device *dev,
			     struct rtnl_link_stats64 *s)
{
	netdev_stats_to_stats64(s, &dev->stats);
	dev_fetch_dstats(s, dev->dstats);
}

/* Convert net_device_stats to rtnl_link_stats64. rtnl_link_stats64 has
 * all the same fields in the same order as net_device_stats, with only
 * the type differing, but rtnl_link_stats64 may have additional fields
 * at the end for newer counters.
 */
void netdev_stats_to_stats64(struct rtnl_link_stats64 *stats64,
			     const struct net_device_stats *netdev_stats)
{
	size_t i, n = sizeof(*netdev_stats) / sizeof(atomic_long_t);
	const atomic_long_t *src = (atomic_long_t *)netdev_stats;
	u64 *dst = (u64 *)stats64;

	BUILD_BUG_ON(n > sizeof(*stats64) / sizeof(u64));
	for (i = 0; i < n; i++)
		dst[i] = (unsigned long)atomic_long_read(&src[i]);
	/* zero out counters that only exist in rtnl_link_stats64 */
	memset((char *)stats64 + n * sizeof(u64), 0,
	       sizeof(*stats64) - n * sizeof(u64));
}
EXPORT_SYMBOL(netdev_stats_to_stats64);

static __cold struct net_device_core_stats __percpu *netdev_core_stats_alloc(
		struct net_device *dev)
{
	struct net_device_core_stats __percpu *p;

	p = alloc_percpu_gfp(struct net_device_core_stats,
			     GFP_ATOMIC | __GFP_NOWARN);

	if (p && cmpxchg(&dev->core_stats, NULL, p))
		free_percpu(p);

	/* This READ_ONCE() pairs with the cmpxchg() above */
	return READ_ONCE(dev->core_stats);
}

noinline void netdev_core_stats_inc(struct net_device *dev, u32 offset)
{
	/* This READ_ONCE() pairs with the write in netdev_core_stats_alloc() */
	struct net_device_core_stats __percpu *p = READ_ONCE(dev->core_stats);
	unsigned long __percpu *field;

	if (unlikely(!p)) {
		p = netdev_core_stats_alloc(dev);
		if (!p)
			return;
	}

	field = (unsigned long __percpu *)((void __percpu *)p + offset);
	this_cpu_inc(*field);
}
EXPORT_SYMBOL_GPL(netdev_core_stats_inc);

/**
 *	dev_get_stats	- get network device statistics
 *	@dev: device to get statistics from
 *	@storage: place to store stats
 *
 *	Get network statistics from device. Return @storage.
 *	The device driver may provide its own method by setting
 *	dev->netdev_ops->get_stats64 or dev->netdev_ops->get_stats;
 *	otherwise the internal statistics structure is used.
 */
struct rtnl_link_stats64 *dev_get_stats(struct net_device *dev,
					struct rtnl_link_stats64 *storage)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	const struct net_device_core_stats __percpu *p;

	/*
	 * IPv{4,6} and udp tunnels share common stat helpers and use
	 * different stat type (NETDEV_PCPU_STAT_TSTATS vs
	 * NETDEV_PCPU_STAT_DSTATS). Ensure the accounting is consistent.
	 */
	BUILD_BUG_ON(offsetof(struct pcpu_sw_netstats, rx_bytes) !=
		     offsetof(struct pcpu_dstats, rx_bytes));
	BUILD_BUG_ON(offsetof(struct pcpu_sw_netstats, rx_packets) !=
		     offsetof(struct pcpu_dstats, rx_packets));
	BUILD_BUG_ON(offsetof(struct pcpu_sw_netstats, tx_bytes) !=
		     offsetof(struct pcpu_dstats, tx_bytes));
	BUILD_BUG_ON(offsetof(struct pcpu_sw_netstats, tx_packets) !=
		     offsetof(struct pcpu_dstats, tx_packets));

	if (ops->ndo_get_stats64) {
		memset(storage, 0, sizeof(*storage));
		ops->ndo_get_stats64(dev, storage);
	} else if (ops->ndo_get_stats) {
		netdev_stats_to_stats64(storage, ops->ndo_get_stats(dev));
	} else if (dev->pcpu_stat_type == NETDEV_PCPU_STAT_TSTATS) {
		dev_get_tstats64(dev, storage);
	} else if (dev->pcpu_stat_type == NETDEV_PCPU_STAT_DSTATS) {
		dev_get_dstats64(dev, storage);
	} else {
		netdev_stats_to_stats64(storage, &dev->stats);
	}

	/* This READ_ONCE() pairs with the write in netdev_core_stats_alloc() */
	p = READ_ONCE(dev->core_stats);
	if (p) {
		const struct net_device_core_stats *core_stats;
		int i;

		for_each_possible_cpu(i) {
			core_stats = per_cpu_ptr(p, i);
			storage->rx_dropped += READ_ONCE(core_stats->rx_dropped);
			storage->tx_dropped += READ_ONCE(core_stats->tx_dropped);
			storage->rx_nohandler += READ_ONCE(core_stats->rx_nohandler);
			storage->rx_otherhost_dropped += READ_ONCE(core_stats->rx_otherhost_dropped);
		}
	}
	return storage;
}
EXPORT_SYMBOL(dev_get_stats);

/**
 *	dev_fetch_sw_netstats - get per-cpu network device statistics
 *	@s: place to store stats
 *	@netstats: per-cpu network stats to read from
 *
 *	Read per-cpu network statistics and populate the related fields in @s.
 */
void dev_fetch_sw_netstats(struct rtnl_link_stats64 *s,
			   const struct pcpu_sw_netstats __percpu *netstats)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
		const struct pcpu_sw_netstats *stats;
		unsigned int start;

		stats = per_cpu_ptr(netstats, cpu);
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			rx_packets = u64_stats_read(&stats->rx_packets);
			rx_bytes   = u64_stats_read(&stats->rx_bytes);
			tx_packets = u64_stats_read(&stats->tx_packets);
			tx_bytes   = u64_stats_read(&stats->tx_bytes);
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		s->rx_packets += rx_packets;
		s->rx_bytes   += rx_bytes;
		s->tx_packets += tx_packets;
		s->tx_bytes   += tx_bytes;
	}
}
EXPORT_SYMBOL_GPL(dev_fetch_sw_netstats);

/**
 *	dev_get_tstats64 - ndo_get_stats64 implementation
 *	@dev: device to get statistics from
 *	@s: place to store stats
 *
 *	Populate @s from dev->stats and dev->tstats. Can be used as
 *	ndo_get_stats64() callback.
 */
void dev_get_tstats64(struct net_device *dev, struct rtnl_link_stats64 *s)
{
	netdev_stats_to_stats64(s, &dev->stats);
	dev_fetch_sw_netstats(s, dev->tstats);
}
EXPORT_SYMBOL_GPL(dev_get_tstats64);

struct netdev_queue *dev_ingress_queue_create(struct net_device *dev)
{
	struct netdev_queue *queue = dev_ingress_queue(dev);

#ifdef CONFIG_NET_CLS_ACT
	if (queue)
		return queue;
	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return NULL;
	netdev_init_one_queue(dev, queue, NULL);
	RCU_INIT_POINTER(queue->qdisc, &noop_qdisc);
	RCU_INIT_POINTER(queue->qdisc_sleeping, &noop_qdisc);
	rcu_assign_pointer(dev->ingress_queue, queue);
#endif
	return queue;
}

static const struct ethtool_ops default_ethtool_ops;

void netdev_set_default_ethtool_ops(struct net_device *dev,
				    const struct ethtool_ops *ops)
{
	if (dev->ethtool_ops == &default_ethtool_ops)
		dev->ethtool_ops = ops;
}
EXPORT_SYMBOL_GPL(netdev_set_default_ethtool_ops);

/**
 * netdev_sw_irq_coalesce_default_on() - enable SW IRQ coalescing by default
 * @dev: netdev to enable the IRQ coalescing on
 *
 * Sets a conservative default for SW IRQ coalescing. Users can use
 * sysfs attributes to override the default values.
 */
void netdev_sw_irq_coalesce_default_on(struct net_device *dev)
{
	WARN_ON(dev->reg_state == NETREG_REGISTERED);

	if (!IS_ENABLED(CONFIG_PREEMPT_RT)) {
		netdev_set_gro_flush_timeout(dev, 20000);
		netdev_set_defer_hard_irqs(dev, 1);
	}
}
EXPORT_SYMBOL_GPL(netdev_sw_irq_coalesce_default_on);

/**
 * alloc_netdev_mqs - allocate network device
 * @sizeof_priv: size of private data to allocate space for
 * @name: device name format string
 * @name_assign_type: origin of device name
 * @setup: callback to initialize device
 * @txqs: the number of TX subqueues to allocate
 * @rxqs: the number of RX subqueues to allocate
 *
 * Allocates a struct net_device with private data area for driver use
 * and performs basic initialization.  Also allocates subqueue structs
 * for each queue on the device.
 */
struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name,
		unsigned char name_assign_type,
		void (*setup)(struct net_device *),
		unsigned int txqs, unsigned int rxqs)
{
	struct net_device *dev;
	size_t napi_config_sz;
	unsigned int maxqs;

	BUG_ON(strlen(name) >= sizeof(dev->name));

	if (txqs < 1) {
		pr_err("alloc_netdev: Unable to allocate device with zero queues\n");
		return NULL;
	}

	if (rxqs < 1) {
		pr_err("alloc_netdev: Unable to allocate device with zero RX queues\n");
		return NULL;
	}

	maxqs = max(txqs, rxqs);

	dev = kvzalloc(struct_size(dev, priv, sizeof_priv),
		       GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!dev)
		return NULL;

	dev->priv_len = sizeof_priv;

	ref_tracker_dir_init(&dev->refcnt_tracker, 128, "netdev");
#ifdef CONFIG_PCPU_DEV_REFCNT
	dev->pcpu_refcnt = alloc_percpu(int);
	if (!dev->pcpu_refcnt)
		goto free_dev;
	__dev_hold(dev);
#else
	refcount_set(&dev->dev_refcnt, 1);
#endif

	if (dev_addr_init(dev))
		goto free_pcpu;

	dev_mc_init(dev);
	dev_uc_init(dev);

	dev_net_set(dev, &init_net);

	dev->gso_max_size = GSO_LEGACY_MAX_SIZE;
	dev->xdp_zc_max_segs = 1;
	dev->gso_max_segs = GSO_MAX_SEGS;
	dev->gro_max_size = GRO_LEGACY_MAX_SIZE;
	dev->gso_ipv4_max_size = GSO_LEGACY_MAX_SIZE;
	dev->gro_ipv4_max_size = GRO_LEGACY_MAX_SIZE;
	dev->tso_max_size = TSO_LEGACY_MAX_SIZE;
	dev->tso_max_segs = TSO_MAX_SEGS;
	dev->upper_level = 1;
	dev->lower_level = 1;
#ifdef CONFIG_LOCKDEP
	dev->nested_level = 0;
	INIT_LIST_HEAD(&dev->unlink_list);
#endif

	INIT_LIST_HEAD(&dev->napi_list);
	INIT_LIST_HEAD(&dev->unreg_list);
	INIT_LIST_HEAD(&dev->close_list);
	INIT_LIST_HEAD(&dev->link_watch_list);
	INIT_LIST_HEAD(&dev->adj_list.upper);
	INIT_LIST_HEAD(&dev->adj_list.lower);
	INIT_LIST_HEAD(&dev->ptype_all);
	INIT_LIST_HEAD(&dev->ptype_specific);
	INIT_LIST_HEAD(&dev->net_notifier_list);
#ifdef CONFIG_NET_SCHED
	hash_init(dev->qdisc_hash);
#endif

	mutex_init(&dev->lock);

	dev->priv_flags = IFF_XMIT_DST_RELEASE | IFF_XMIT_DST_RELEASE_PERM;
	setup(dev);

	if (!dev->tx_queue_len) {
		dev->priv_flags |= IFF_NO_QUEUE;
		dev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	}

	dev->num_tx_queues = txqs;
	dev->real_num_tx_queues = txqs;
	if (netif_alloc_netdev_queues(dev))
		goto free_all;

	dev->num_rx_queues = rxqs;
	dev->real_num_rx_queues = rxqs;
	if (netif_alloc_rx_queues(dev))
		goto free_all;
	dev->ethtool = kzalloc(sizeof(*dev->ethtool), GFP_KERNEL_ACCOUNT);
	if (!dev->ethtool)
		goto free_all;

	dev->cfg = kzalloc(sizeof(*dev->cfg), GFP_KERNEL_ACCOUNT);
	if (!dev->cfg)
		goto free_all;
	dev->cfg_pending = dev->cfg;

	napi_config_sz = array_size(maxqs, sizeof(*dev->napi_config));
	dev->napi_config = kvzalloc(napi_config_sz, GFP_KERNEL_ACCOUNT);
	if (!dev->napi_config)
		goto free_all;

	strscpy(dev->name, name);
	dev->name_assign_type = name_assign_type;
	dev->group = INIT_NETDEV_GROUP;
	if (!dev->ethtool_ops)
		dev->ethtool_ops = &default_ethtool_ops;

	nf_hook_netdev_init(dev);

	return dev;

free_all:
	free_netdev(dev);
	return NULL;

free_pcpu:
#ifdef CONFIG_PCPU_DEV_REFCNT
	free_percpu(dev->pcpu_refcnt);
free_dev:
#endif
	kvfree(dev);
	return NULL;
}
EXPORT_SYMBOL(alloc_netdev_mqs);

static void netdev_napi_exit(struct net_device *dev)
{
	if (!list_empty(&dev->napi_list)) {
		struct napi_struct *p, *n;

		netdev_lock(dev);
		list_for_each_entry_safe(p, n, &dev->napi_list, dev_list)
			__netif_napi_del_locked(p);
		netdev_unlock(dev);

		synchronize_net();
	}

	kvfree(dev->napi_config);
}

/**
 * free_netdev - free network device
 * @dev: device
 *
 * This function does the last stage of destroying an allocated device
 * interface. The reference to the device object is released. If this
 * is the last reference then it will be freed.Must be called in process
 * context.
 */
void free_netdev(struct net_device *dev)
{
	might_sleep();

	/* When called immediately after register_netdevice() failed the unwind
	 * handling may still be dismantling the device. Handle that case by
	 * deferring the free.
	 */
	if (dev->reg_state == NETREG_UNREGISTERING) {
		ASSERT_RTNL();
		dev->needs_free_netdev = true;
		return;
	}

	WARN_ON(dev->cfg != dev->cfg_pending);
	kfree(dev->cfg);
	kfree(dev->ethtool);
	netif_free_tx_queues(dev);
	netif_free_rx_queues(dev);

	kfree(rcu_dereference_protected(dev->ingress_queue, 1));

	/* Flush device addresses */
	dev_addr_flush(dev);

	netdev_napi_exit(dev);

	netif_del_cpu_rmap(dev);

	ref_tracker_dir_exit(&dev->refcnt_tracker);
#ifdef CONFIG_PCPU_DEV_REFCNT
	free_percpu(dev->pcpu_refcnt);
	dev->pcpu_refcnt = NULL;
#endif
	free_percpu(dev->core_stats);
	dev->core_stats = NULL;
	free_percpu(dev->xdp_bulkq);
	dev->xdp_bulkq = NULL;

	netdev_free_phy_link_topology(dev);

	mutex_destroy(&dev->lock);

	/*  Compatibility with error handling in drivers */
	if (dev->reg_state == NETREG_UNINITIALIZED ||
	    dev->reg_state == NETREG_DUMMY) {
		kvfree(dev);
		return;
	}

	BUG_ON(dev->reg_state != NETREG_UNREGISTERED);
	WRITE_ONCE(dev->reg_state, NETREG_RELEASED);

	/* will free via device release */
	put_device(&dev->dev);
}
EXPORT_SYMBOL(free_netdev);

/**
 * alloc_netdev_dummy - Allocate and initialize a dummy net device.
 * @sizeof_priv: size of private data to allocate space for
 *
 * Return: the allocated net_device on success, NULL otherwise
 */
struct net_device *alloc_netdev_dummy(int sizeof_priv)
{
	return alloc_netdev(sizeof_priv, "dummy#", NET_NAME_UNKNOWN,
			    init_dummy_netdev);
}
EXPORT_SYMBOL_GPL(alloc_netdev_dummy);

/**
 *	synchronize_net -  Synchronize with packet receive processing
 *
 *	Wait for packets currently being received to be done.
 *	Does not block later packets from starting.
 */
void synchronize_net(void)
{
	might_sleep();
	if (from_cleanup_net() || rtnl_is_locked())
		synchronize_rcu_expedited();
	else
		synchronize_rcu();
}
EXPORT_SYMBOL(synchronize_net);

static void netdev_rss_contexts_free(struct net_device *dev)
{
	struct ethtool_rxfh_context *ctx;
	unsigned long context;

	mutex_lock(&dev->ethtool->rss_lock);
	xa_for_each(&dev->ethtool->rss_ctx, context, ctx) {
		xa_erase(&dev->ethtool->rss_ctx, context);
		dev->ethtool_ops->remove_rxfh_context(dev, ctx, context, NULL);
		kfree(ctx);
	}
	xa_destroy(&dev->ethtool->rss_ctx);
	mutex_unlock(&dev->ethtool->rss_lock);
}

/**
 *	unregister_netdevice_queue - remove device from the kernel
 *	@dev: device
 *	@head: list
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables.
 *	If head not NULL, device is queued to be unregistered later.
 *
 *	Callers must hold the rtnl semaphore.  You may want
 *	unregister_netdev() instead of this.
 */

void unregister_netdevice_queue(struct net_device *dev, struct list_head *head)
{
	ASSERT_RTNL();

	if (head) {
		list_move_tail(&dev->unreg_list, head);
	} else {
		LIST_HEAD(single);

		list_add(&dev->unreg_list, &single);
		unregister_netdevice_many(&single);
	}
}
EXPORT_SYMBOL(unregister_netdevice_queue);

static void dev_memory_provider_uninstall(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		struct netdev_rx_queue *rxq = &dev->_rx[i];
		struct pp_memory_provider_params *p = &rxq->mp_params;

		if (p->mp_ops && p->mp_ops->uninstall)
			p->mp_ops->uninstall(rxq->mp_params.mp_priv, rxq);
	}
}

void unregister_netdevice_many_notify(struct list_head *head,
				      u32 portid, const struct nlmsghdr *nlh)
{
	struct net_device *dev, *tmp;
	LIST_HEAD(close_head);
	int cnt = 0;

	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	if (list_empty(head))
		return;

	list_for_each_entry_safe(dev, tmp, head, unreg_list) {
		/* Some devices call without registering
		 * for initialization unwind. Remove those
		 * devices and proceed with the remaining.
		 */
		if (dev->reg_state == NETREG_UNINITIALIZED) {
			pr_debug("unregister_netdevice: device %s/%p never was registered\n",
				 dev->name, dev);

			WARN_ON(1);
			list_del(&dev->unreg_list);
			continue;
		}
		dev->dismantle = true;
		BUG_ON(dev->reg_state != NETREG_REGISTERED);
	}

	/* If device is running, close it first. Start with ops locked... */
	list_for_each_entry(dev, head, unreg_list) {
		if (netdev_need_ops_lock(dev)) {
			list_add_tail(&dev->close_list, &close_head);
			netdev_lock(dev);
		}
	}
	netif_close_many(&close_head, true);
	/* ... now unlock them and go over the rest. */
	list_for_each_entry(dev, head, unreg_list) {
		if (netdev_need_ops_lock(dev))
			netdev_unlock(dev);
		else
			list_add_tail(&dev->close_list, &close_head);
	}
	netif_close_many(&close_head, true);

	list_for_each_entry(dev, head, unreg_list) {
		/* And unlink it from device chain. */
		unlist_netdevice(dev);
		netdev_lock(dev);
		WRITE_ONCE(dev->reg_state, NETREG_UNREGISTERING);
		netdev_unlock(dev);
	}
	flush_all_backlogs();

	synchronize_net();

	list_for_each_entry(dev, head, unreg_list) {
		struct sk_buff *skb = NULL;

		/* Shutdown queueing discipline. */
		netdev_lock_ops(dev);
		dev_shutdown(dev);
		dev_tcx_uninstall(dev);
		dev_xdp_uninstall(dev);
		dev_memory_provider_uninstall(dev);
		netdev_unlock_ops(dev);
		bpf_dev_bound_netdev_unregister(dev);

		netdev_offload_xstats_disable_all(dev);

		/* Notify protocols, that we are about to destroy
		 * this device. They should clean all the things.
		 */
		call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

		if (!(dev->rtnl_link_ops && dev->rtnl_link_initializing))
			skb = rtmsg_ifinfo_build_skb(RTM_DELLINK, dev, ~0U, 0,
						     GFP_KERNEL, NULL, 0,
						     portid, nlh);

		/*
		 *	Flush the unicast and multicast chains
		 */
		dev_uc_flush(dev);
		dev_mc_flush(dev);

		netdev_name_node_alt_flush(dev);
		netdev_name_node_free(dev->name_node);

		netdev_rss_contexts_free(dev);

		call_netdevice_notifiers(NETDEV_PRE_UNINIT, dev);

		if (dev->netdev_ops->ndo_uninit)
			dev->netdev_ops->ndo_uninit(dev);

		mutex_destroy(&dev->ethtool->rss_lock);

		net_shaper_flush_netdev(dev);

		if (skb)
			rtmsg_ifinfo_send(skb, dev, GFP_KERNEL, portid, nlh);

		/* Notifier chain MUST detach us all upper devices. */
		WARN_ON(netdev_has_any_upper_dev(dev));
		WARN_ON(netdev_has_any_lower_dev(dev));

		/* Remove entries from kobject tree */
		netdev_unregister_kobject(dev);
#ifdef CONFIG_XPS
		/* Remove XPS queueing entries */
		netif_reset_xps_queues_gt(dev, 0);
#endif
	}

	synchronize_net();

	list_for_each_entry(dev, head, unreg_list) {
		netdev_put(dev, &dev->dev_registered_tracker);
		net_set_todo(dev);
		cnt++;
	}
	atomic_add(cnt, &dev_unreg_count);

	list_del(head);
}

/**
 *	unregister_netdevice_many - unregister many devices
 *	@head: list of devices
 *
 *  Note: As most callers use a stack allocated list_head,
 *  we force a list_del() to make sure stack won't be corrupted later.
 */
void unregister_netdevice_many(struct list_head *head)
{
	unregister_netdevice_many_notify(head, 0, NULL);
}
EXPORT_SYMBOL(unregister_netdevice_many);

/**
 *	unregister_netdev - remove device from the kernel
 *	@dev: device
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables.
 *
 *	This is just a wrapper for unregister_netdevice that takes
 *	the rtnl semaphore.  In general you want to use this and not
 *	unregister_netdevice.
 */
void unregister_netdev(struct net_device *dev)
{
	rtnl_net_dev_lock(dev);
	unregister_netdevice(dev);
	rtnl_net_dev_unlock(dev);
}
EXPORT_SYMBOL(unregister_netdev);

int __dev_change_net_namespace(struct net_device *dev, struct net *net,
			       const char *pat, int new_ifindex,
			       struct netlink_ext_ack *extack)
{
	struct netdev_name_node *name_node;
	struct net *net_old = dev_net(dev);
	char new_name[IFNAMSIZ] = {};
	int err, new_nsid;

	ASSERT_RTNL();

	/* Don't allow namespace local devices to be moved. */
	err = -EINVAL;
	if (dev->netns_immutable) {
		NL_SET_ERR_MSG(extack, "The interface netns is immutable");
		goto out;
	}

	/* Ensure the device has been registered */
	if (dev->reg_state != NETREG_REGISTERED) {
		NL_SET_ERR_MSG(extack, "The interface isn't registered");
		goto out;
	}

	/* Get out if there is nothing todo */
	err = 0;
	if (net_eq(net_old, net))
		goto out;

	/* Pick the destination device name, and ensure
	 * we can use it in the destination network namespace.
	 */
	err = -EEXIST;
	if (netdev_name_in_use(net, dev->name)) {
		/* We get here if we can't use the current device name */
		if (!pat) {
			NL_SET_ERR_MSG(extack,
				       "An interface with the same name exists in the target netns");
			goto out;
		}
		err = dev_prep_valid_name(net, dev, pat, new_name, EEXIST);
		if (err < 0) {
			NL_SET_ERR_MSG_FMT(extack,
					   "Unable to use '%s' for the new interface name in the target netns",
					   pat);
			goto out;
		}
	}
	/* Check that none of the altnames conflicts. */
	err = -EEXIST;
	netdev_for_each_altname(dev, name_node) {
		if (netdev_name_in_use(net, name_node->name)) {
			NL_SET_ERR_MSG_FMT(extack,
					   "An interface with the altname %s exists in the target netns",
					   name_node->name);
			goto out;
		}
	}

	/* Check that new_ifindex isn't used yet. */
	if (new_ifindex) {
		err = dev_index_reserve(net, new_ifindex);
		if (err < 0) {
			NL_SET_ERR_MSG_FMT(extack,
					   "The ifindex %d is not available in the target netns",
					   new_ifindex);
			goto out;
		}
	} else {
		/* If there is an ifindex conflict assign a new one */
		err = dev_index_reserve(net, dev->ifindex);
		if (err == -EBUSY)
			err = dev_index_reserve(net, 0);
		if (err < 0) {
			NL_SET_ERR_MSG(extack,
				       "Unable to allocate a new ifindex in the target netns");
			goto out;
		}
		new_ifindex = err;
	}

	/*
	 * And now a mini version of register_netdevice unregister_netdevice.
	 */

	netdev_lock_ops(dev);
	/* If device is running close it first. */
	netif_close(dev);
	/* And unlink it from device chain */
	unlist_netdevice(dev);

	if (!netdev_need_ops_lock(dev))
		netdev_lock(dev);
	dev->moving_ns = true;
	netdev_unlock(dev);

	synchronize_net();

	/* Shutdown queueing discipline. */
	netdev_lock_ops(dev);
	dev_shutdown(dev);
	netdev_unlock_ops(dev);

	/* Notify protocols, that we are about to destroy
	 * this device. They should clean all the things.
	 *
	 * Note that dev->reg_state stays at NETREG_REGISTERED.
	 * This is wanted because this way 8021q and macvlan know
	 * the device is just moving and can keep their slaves up.
	 */
	call_netdevice_notifiers(NETDEV_UNREGISTER, dev);
	rcu_barrier();

	new_nsid = peernet2id_alloc(dev_net(dev), net, GFP_KERNEL);

	rtmsg_ifinfo_newnet(RTM_DELLINK, dev, ~0U, GFP_KERNEL, &new_nsid,
			    new_ifindex);

	/*
	 *	Flush the unicast and multicast chains
	 */
	dev_uc_flush(dev);
	dev_mc_flush(dev);

	/* Send a netdev-removed uevent to the old namespace */
	kobject_uevent(&dev->dev.kobj, KOBJ_REMOVE);
	netdev_adjacent_del_links(dev);

	/* Move per-net netdevice notifiers that are following the netdevice */
	move_netdevice_notifiers_dev_net(dev, net);

	/* Actually switch the network namespace */
	netdev_lock(dev);
	dev_net_set(dev, net);
	netdev_unlock(dev);
	dev->ifindex = new_ifindex;

	if (new_name[0]) {
		/* Rename the netdev to prepared name */
		write_seqlock_bh(&netdev_rename_lock);
		strscpy(dev->name, new_name, IFNAMSIZ);
		write_sequnlock_bh(&netdev_rename_lock);
	}

	/* Fixup kobjects */
	dev_set_uevent_suppress(&dev->dev, 1);
	err = device_rename(&dev->dev, dev->name);
	dev_set_uevent_suppress(&dev->dev, 0);
	WARN_ON(err);

	/* Send a netdev-add uevent to the new namespace */
	kobject_uevent(&dev->dev.kobj, KOBJ_ADD);
	netdev_adjacent_add_links(dev);

	/* Adapt owner in case owning user namespace of target network
	 * namespace is different from the original one.
	 */
	err = netdev_change_owner(dev, net_old, net);
	WARN_ON(err);

	netdev_lock(dev);
	dev->moving_ns = false;
	if (!netdev_need_ops_lock(dev))
		netdev_unlock(dev);

	/* Add the device back in the hashes */
	list_netdevice(dev);
	/* Notify protocols, that a new device appeared. */
	call_netdevice_notifiers(NETDEV_REGISTER, dev);
	netdev_unlock_ops(dev);

	/*
	 *	Prevent userspace races by waiting until the network
	 *	device is fully setup before sending notifications.
	 */
	rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U, GFP_KERNEL, 0, NULL);

	synchronize_net();
	err = 0;
out:
	return err;
}

static int dev_cpu_dead(unsigned int oldcpu)
{
	struct sk_buff **list_skb;
	struct sk_buff *skb;
	unsigned int cpu;
	struct softnet_data *sd, *oldsd, *remsd = NULL;

	local_irq_disable();
	cpu = smp_processor_id();
	sd = &per_cpu(softnet_data, cpu);
	oldsd = &per_cpu(softnet_data, oldcpu);

	/* Find end of our completion_queue. */
	list_skb = &sd->completion_queue;
	while (*list_skb)
		list_skb = &(*list_skb)->next;
	/* Append completion queue from offline CPU. */
	*list_skb = oldsd->completion_queue;
	oldsd->completion_queue = NULL;

	/* Append output queue from offline CPU. */
	if (oldsd->output_queue) {
		*sd->output_queue_tailp = oldsd->output_queue;
		sd->output_queue_tailp = oldsd->output_queue_tailp;
		oldsd->output_queue = NULL;
		oldsd->output_queue_tailp = &oldsd->output_queue;
	}
	/* Append NAPI poll list from offline CPU, with one exception :
	 * process_backlog() must be called by cpu owning percpu backlog.
	 * We properly handle process_queue & input_pkt_queue later.
	 */
	while (!list_empty(&oldsd->poll_list)) {
		struct napi_struct *napi = list_first_entry(&oldsd->poll_list,
							    struct napi_struct,
							    poll_list);

		list_del_init(&napi->poll_list);
		if (napi->poll == process_backlog)
			napi->state &= NAPIF_STATE_THREADED;
		else
			____napi_schedule(sd, napi);
	}

	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_enable();

	if (!use_backlog_threads()) {
#ifdef CONFIG_RPS
		remsd = oldsd->rps_ipi_list;
		oldsd->rps_ipi_list = NULL;
#endif
		/* send out pending IPI's on offline CPU */
		net_rps_send_ipi(remsd);
	}

	/* Process offline CPU's input_pkt_queue */
	while ((skb = __skb_dequeue(&oldsd->process_queue))) {
		netif_rx(skb);
		rps_input_queue_head_incr(oldsd);
	}
	while ((skb = skb_dequeue(&oldsd->input_pkt_queue))) {
		netif_rx(skb);
		rps_input_queue_head_incr(oldsd);
	}

	return 0;
}

/**
 *	netdev_increment_features - increment feature set by one
 *	@all: current feature set
 *	@one: new feature set
 *	@mask: mask feature set
 *
 *	Computes a new feature set after adding a device with feature set
 *	@one to the master device with current feature set @all.  Will not
 *	enable anything that is off in @mask. Returns the new feature set.
 */
netdev_features_t netdev_increment_features(netdev_features_t all,
	netdev_features_t one, netdev_features_t mask)
{
	if (mask & NETIF_F_HW_CSUM)
		mask |= NETIF_F_CSUM_MASK;
	mask |= NETIF_F_VLAN_CHALLENGED;

	all |= one & (NETIF_F_ONE_FOR_ALL | NETIF_F_CSUM_MASK) & mask;
	all &= one | ~NETIF_F_ALL_FOR_ALL;

	/* If one device supports hw checksumming, set for all. */
	if (all & NETIF_F_HW_CSUM)
		all &= ~(NETIF_F_CSUM_MASK & ~NETIF_F_HW_CSUM);

	return all;
}
EXPORT_SYMBOL(netdev_increment_features);

static struct hlist_head * __net_init netdev_create_hash(void)
{
	int i;
	struct hlist_head *hash;

	hash = kmalloc_array(NETDEV_HASHENTRIES, sizeof(*hash), GFP_KERNEL);
	if (hash != NULL)
		for (i = 0; i < NETDEV_HASHENTRIES; i++)
			INIT_HLIST_HEAD(&hash[i]);

	return hash;
}

/* Initialize per network namespace state */
static int __net_init netdev_init(struct net *net)
{
	BUILD_BUG_ON(GRO_HASH_BUCKETS >
		     BITS_PER_BYTE * sizeof_field(struct gro_node, bitmask));

	INIT_LIST_HEAD(&net->dev_base_head);

	net->dev_name_head = netdev_create_hash();
	if (net->dev_name_head == NULL)
		goto err_name;

	net->dev_index_head = netdev_create_hash();
	if (net->dev_index_head == NULL)
		goto err_idx;

	xa_init_flags(&net->dev_by_index, XA_FLAGS_ALLOC1);

	RAW_INIT_NOTIFIER_HEAD(&net->netdev_chain);

	return 0;

err_idx:
	kfree(net->dev_name_head);
err_name:
	return -ENOMEM;
}

/**
 *	netdev_drivername - network driver for the device
 *	@dev: network device
 *
 *	Determine network driver for device.
 */
const char *netdev_drivername(const struct net_device *dev)
{
	const struct device_driver *driver;
	const struct device *parent;
	const char *empty = "";

	parent = dev->dev.parent;
	if (!parent)
		return empty;

	driver = parent->driver;
	if (driver && driver->name)
		return driver->name;
	return empty;
}

static void __netdev_printk(const char *level, const struct net_device *dev,
			    struct va_format *vaf)
{
	if (dev && dev->dev.parent) {
		dev_printk_emit(level[1] - '0',
				dev->dev.parent,
				"%s %s %s%s: %pV",
				dev_driver_string(dev->dev.parent),
				dev_name(dev->dev.parent),
				netdev_name(dev), netdev_reg_state(dev),
				vaf);
	} else if (dev) {
		printk("%s%s%s: %pV",
		       level, netdev_name(dev), netdev_reg_state(dev), vaf);
	} else {
		printk("%s(NULL net_device): %pV", level, vaf);
	}
}

void netdev_printk(const char *level, const struct net_device *dev,
		   const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	__netdev_printk(level, dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(netdev_printk);

#define define_netdev_printk_level(func, level)			\
void func(const struct net_device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf;					\
	va_list args;						\
								\
	va_start(args, fmt);					\
								\
	vaf.fmt = fmt;						\
	vaf.va = &args;						\
								\
	__netdev_printk(level, dev, &vaf);			\
								\
	va_end(args);						\
}								\
EXPORT_SYMBOL(func);

define_netdev_printk_level(netdev_emerg, KERN_EMERG);
define_netdev_printk_level(netdev_alert, KERN_ALERT);
define_netdev_printk_level(netdev_crit, KERN_CRIT);
define_netdev_printk_level(netdev_err, KERN_ERR);
define_netdev_printk_level(netdev_warn, KERN_WARNING);
define_netdev_printk_level(netdev_notice, KERN_NOTICE);
define_netdev_printk_level(netdev_info, KERN_INFO);

static void __net_exit netdev_exit(struct net *net)
{
	kfree(net->dev_name_head);
	kfree(net->dev_index_head);
	xa_destroy(&net->dev_by_index);
	if (net != &init_net)
		WARN_ON_ONCE(!list_empty(&net->dev_base_head));
}

static struct pernet_operations __net_initdata netdev_net_ops = {
	.init = netdev_init,
	.exit = netdev_exit,
};

static void __net_exit default_device_exit_net(struct net *net)
{
	struct netdev_name_node *name_node, *tmp;
	struct net_device *dev, *aux;
	/*
	 * Push all migratable network devices back to the
	 * initial network namespace
	 */
	ASSERT_RTNL();
	for_each_netdev_safe(net, dev, aux) {
		int err;
		char fb_name[IFNAMSIZ];

		/* Ignore unmoveable devices (i.e. loopback) */
		if (dev->netns_immutable)
			continue;

		/* Leave virtual devices for the generic cleanup */
		if (dev->rtnl_link_ops && !dev->rtnl_link_ops->netns_refund)
			continue;

		/* Push remaining network devices to init_net */
		snprintf(fb_name, IFNAMSIZ, "dev%d", dev->ifindex);
		if (netdev_name_in_use(&init_net, fb_name))
			snprintf(fb_name, IFNAMSIZ, "dev%%d");

		netdev_for_each_altname_safe(dev, name_node, tmp)
			if (netdev_name_in_use(&init_net, name_node->name))
				__netdev_name_node_alt_destroy(name_node);

		err = dev_change_net_namespace(dev, &init_net, fb_name);
		if (err) {
			pr_emerg("%s: failed to move %s to init_net: %d\n",
				 __func__, dev->name, err);
			BUG();
		}
	}
}

static void __net_exit default_device_exit_batch(struct list_head *net_list)
{
	/* At exit all network devices most be removed from a network
	 * namespace.  Do this in the reverse order of registration.
	 * Do this across as many network namespaces as possible to
	 * improve batching efficiency.
	 */
	struct net_device *dev;
	struct net *net;
	LIST_HEAD(dev_kill_list);

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list) {
		default_device_exit_net(net);
		cond_resched();
	}

	list_for_each_entry(net, net_list, exit_list) {
		for_each_netdev_reverse(net, dev) {
			if (dev->rtnl_link_ops && dev->rtnl_link_ops->dellink)
				dev->rtnl_link_ops->dellink(dev, &dev_kill_list);
			else
				unregister_netdevice_queue(dev, &dev_kill_list);
		}
	}
	unregister_netdevice_many(&dev_kill_list);
	rtnl_unlock();
}

static struct pernet_operations __net_initdata default_device_ops = {
	.exit_batch = default_device_exit_batch,
};

static void __init net_dev_struct_check(void)
{
	/* TX read-mostly hotpath */
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, priv_flags_fast);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, netdev_ops);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, header_ops);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, _tx);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, real_num_tx_queues);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, gso_max_size);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, gso_ipv4_max_size);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, gso_max_segs);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, gso_partial_features);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, num_tc);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, mtu);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, needed_headroom);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, tc_to_txq);
#ifdef CONFIG_XPS
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, xps_maps);
#endif
#ifdef CONFIG_NETFILTER_EGRESS
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, nf_hooks_egress);
#endif
#ifdef CONFIG_NET_XGRESS
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_tx, tcx_egress);
#endif
	CACHELINE_ASSERT_GROUP_SIZE(struct net_device, net_device_read_tx, 160);

	/* TXRX read-mostly hotpath */
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_txrx, lstats);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_txrx, state);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_txrx, flags);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_txrx, hard_header_len);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_txrx, features);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_txrx, ip6_ptr);
	CACHELINE_ASSERT_GROUP_SIZE(struct net_device, net_device_read_txrx, 46);

	/* RX read-mostly hotpath */
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, ptype_specific);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, ifindex);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, real_num_rx_queues);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, _rx);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, gro_max_size);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, gro_ipv4_max_size);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, rx_handler);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, rx_handler_data);
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, nd_net);
#ifdef CONFIG_NETPOLL
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, npinfo);
#endif
#ifdef CONFIG_NET_XGRESS
	CACHELINE_ASSERT_GROUP_MEMBER(struct net_device, net_device_read_rx, tcx_ingress);
#endif
	CACHELINE_ASSERT_GROUP_SIZE(struct net_device, net_device_read_rx, 92);
}

/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not
 *	present) and leaves us with a valid list of present and active devices.
 *
 */

/* We allocate 256 pages for each CPU if PAGE_SHIFT is 12 */
#define SYSTEM_PERCPU_PAGE_POOL_SIZE	((1 << 20) / PAGE_SIZE)

static int net_page_pool_create(int cpuid)
{
#if IS_ENABLED(CONFIG_PAGE_POOL)
	struct page_pool_params page_pool_params = {
		.pool_size = SYSTEM_PERCPU_PAGE_POOL_SIZE,
		.flags = PP_FLAG_SYSTEM_POOL,
		.nid = cpu_to_mem(cpuid),
	};
	struct page_pool *pp_ptr;
	int err;

	pp_ptr = page_pool_create_percpu(&page_pool_params, cpuid);
	if (IS_ERR(pp_ptr))
		return -ENOMEM;

	err = xdp_reg_page_pool(pp_ptr);
	if (err) {
		page_pool_destroy(pp_ptr);
		return err;
	}

	per_cpu(system_page_pool.pool, cpuid) = pp_ptr;
#endif
	return 0;
}

static int backlog_napi_should_run(unsigned int cpu)
{
	struct softnet_data *sd = per_cpu_ptr(&softnet_data, cpu);
	struct napi_struct *napi = &sd->backlog;

	return test_bit(NAPI_STATE_SCHED_THREADED, &napi->state);
}

static void run_backlog_napi(unsigned int cpu)
{
	struct softnet_data *sd = per_cpu_ptr(&softnet_data, cpu);

	napi_threaded_poll_loop(&sd->backlog);
}

static void backlog_napi_setup(unsigned int cpu)
{
	struct softnet_data *sd = per_cpu_ptr(&softnet_data, cpu);
	struct napi_struct *napi = &sd->backlog;

	napi->thread = this_cpu_read(backlog_napi);
	set_bit(NAPI_STATE_THREADED, &napi->state);
}

static struct smp_hotplug_thread backlog_threads = {
	.store			= &backlog_napi,
	.thread_should_run	= backlog_napi_should_run,
	.thread_fn		= run_backlog_napi,
	.thread_comm		= "backlog_napi/%u",
	.setup			= backlog_napi_setup,
};

/*
 *       This is called single threaded during boot, so no need
 *       to take the rtnl semaphore.
 */
static int __init net_dev_init(void)
{
	int i, rc = -ENOMEM;

	BUG_ON(!dev_boot_phase);

	net_dev_struct_check();

	if (dev_proc_init())
		goto out;

	if (netdev_kobject_init())
		goto out;

	for (i = 0; i < PTYPE_HASH_SIZE; i++)
		INIT_LIST_HEAD(&ptype_base[i]);

	if (register_pernet_subsys(&netdev_net_ops))
		goto out;

	/*
	 *	Initialise the packet receive queues.
	 */

	flush_backlogs_fallback = flush_backlogs_alloc();
	if (!flush_backlogs_fallback)
		goto out;

	for_each_possible_cpu(i) {
		struct softnet_data *sd = &per_cpu(softnet_data, i);

		skb_queue_head_init(&sd->input_pkt_queue);
		skb_queue_head_init(&sd->process_queue);
#ifdef CONFIG_XFRM_OFFLOAD
		skb_queue_head_init(&sd->xfrm_backlog);
#endif
		INIT_LIST_HEAD(&sd->poll_list);
		sd->output_queue_tailp = &sd->output_queue;
#ifdef CONFIG_RPS
		INIT_CSD(&sd->csd, rps_trigger_softirq, sd);
		sd->cpu = i;
#endif
		INIT_CSD(&sd->defer_csd, trigger_rx_softirq, sd);
		spin_lock_init(&sd->defer_lock);

		gro_init(&sd->backlog.gro);
		sd->backlog.poll = process_backlog;
		sd->backlog.weight = weight_p;
		INIT_LIST_HEAD(&sd->backlog.poll_list);

		if (net_page_pool_create(i))
			goto out;
	}
	if (use_backlog_threads())
		smpboot_register_percpu_thread(&backlog_threads);

	dev_boot_phase = 0;

	/* The loopback device is special if any other network devices
	 * is present in a network namespace the loopback device must
	 * be present. Since we now dynamically allocate and free the
	 * loopback device ensure this invariant is maintained by
	 * keeping the loopback device as the first device on the
	 * list of network devices.  Ensuring the loopback devices
	 * is the first device that appears and the last network device
	 * that disappears.
	 */
	if (register_pernet_device(&loopback_net_ops))
		goto out;

	if (register_pernet_device(&default_device_ops))
		goto out;

	open_softirq(NET_TX_SOFTIRQ, net_tx_action);
	open_softirq(NET_RX_SOFTIRQ, net_rx_action);

	rc = cpuhp_setup_state_nocalls(CPUHP_NET_DEV_DEAD, "net/dev:dead",
				       NULL, dev_cpu_dead);
	WARN_ON(rc < 0);
	rc = 0;

	/* avoid static key IPIs to isolated CPUs */
	if (housekeeping_enabled(HK_TYPE_MISC))
		net_enable_timestamp();
out:
	if (rc < 0) {
		for_each_possible_cpu(i) {
			struct page_pool *pp_ptr;

			pp_ptr = per_cpu(system_page_pool.pool, i);
			if (!pp_ptr)
				continue;

			xdp_unreg_page_pool(pp_ptr);
			page_pool_destroy(pp_ptr);
			per_cpu(system_page_pool.pool, i) = NULL;
		}
	}

	return rc;
}

subsys_initcall(net_dev_init);
