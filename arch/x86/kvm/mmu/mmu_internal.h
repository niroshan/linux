/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_MMU_INTERNAL_H
#define __KVM_X86_MMU_INTERNAL_H

#include <linux/types.h>
#include <linux/kvm_host.h>
#include <asm/kvm_host.h>

#include "mmu.h"

#ifdef CONFIG_KVM_PROVE_MMU
#define KVM_MMU_WARN_ON(x) WARN_ON_ONCE(x)
#else
#define KVM_MMU_WARN_ON(x) BUILD_BUG_ON_INVALID(x)
#endif

/* Page table builder macros common to shadow (host) PTEs and guest PTEs. */
#define __PT_BASE_ADDR_MASK GENMASK_ULL(51, 12)
#define __PT_LEVEL_SHIFT(level, bits_per_level)	\
	(PAGE_SHIFT + ((level) - 1) * (bits_per_level))
#define __PT_INDEX(address, level, bits_per_level) \
	(((address) >> __PT_LEVEL_SHIFT(level, bits_per_level)) & ((1 << (bits_per_level)) - 1))

#define __PT_LVL_ADDR_MASK(base_addr_mask, level, bits_per_level) \
	((base_addr_mask) & ~((1ULL << (PAGE_SHIFT + (((level) - 1) * (bits_per_level)))) - 1))

#define __PT_LVL_OFFSET_MASK(base_addr_mask, level, bits_per_level) \
	((base_addr_mask) & ((1ULL << (PAGE_SHIFT + (((level) - 1) * (bits_per_level)))) - 1))

#define __PT_ENT_PER_PAGE(bits_per_level)  (1 << (bits_per_level))

/*
 * Unlike regular MMU roots, PAE "roots", a.k.a. PDPTEs/PDPTRs, have a PRESENT
 * bit, and thus are guaranteed to be non-zero when valid.  And, when a guest
 * PDPTR is !PRESENT, its corresponding PAE root cannot be set to INVALID_PAGE,
 * as the CPU would treat that as PRESENT PDPTR with reserved bits set.  Use
 * '0' instead of INVALID_PAGE to indicate an invalid PAE root.
 */
#define INVALID_PAE_ROOT	0
#define IS_VALID_PAE_ROOT(x)	(!!(x))

static inline hpa_t kvm_mmu_get_dummy_root(void)
{
	return my_zero_pfn(0) << PAGE_SHIFT;
}

static inline bool kvm_mmu_is_dummy_root(hpa_t shadow_page)
{
	return is_zero_pfn(shadow_page >> PAGE_SHIFT);
}

typedef u64 __rcu *tdp_ptep_t;

struct kvm_mmu_page {
	/*
	 * Note, "link" through "spt" fit in a single 64 byte cache line on
	 * 64-bit kernels, keep it that way unless there's a reason not to.
	 */
	struct list_head link;
	struct hlist_node hash_link;

	bool tdp_mmu_page;
	bool unsync;
	union {
		u8 mmu_valid_gen;

		/* Only accessed under slots_lock.  */
		bool tdp_mmu_scheduled_root_to_zap;
	};

	 /*
	  * The shadow page can't be replaced by an equivalent huge page
	  * because it is being used to map an executable page in the guest
	  * and the NX huge page mitigation is enabled.
	  */
	bool nx_huge_page_disallowed;

	/*
	 * The following two entries are used to key the shadow page in the
	 * hash table.
	 */
	union kvm_mmu_page_role role;
	gfn_t gfn;

	u64 *spt;

	/*
	 * Stores the result of the guest translation being shadowed by each
	 * SPTE.  KVM shadows two types of guest translations: nGPA -> GPA
	 * (shadow EPT/NPT) and GVA -> GPA (traditional shadow paging). In both
	 * cases the result of the translation is a GPA and a set of access
	 * constraints.
	 *
	 * The GFN is stored in the upper bits (PAGE_SHIFT) and the shadowed
	 * access permissions are stored in the lower bits. Note, for
	 * convenience and uniformity across guests, the access permissions are
	 * stored in KVM format (e.g.  ACC_EXEC_MASK) not the raw guest format.
	 */
	u64 *shadowed_translation;

	/* Currently serving as active root */
	union {
		int root_count;
		refcount_t tdp_mmu_root_count;
	};

	bool has_mapped_host_mmio;

	union {
		/* These two members aren't used for TDP MMU */
		struct {
			unsigned int unsync_children;
			/*
			 * Number of writes since the last time traversal
			 * visited this page.
			 */
			atomic_t write_flooding_count;
		};
		/*
		 * Page table page of external PT.
		 * Passed to TDX module, not accessed by KVM.
		 */
		void *external_spt;
	};
	union {
		struct kvm_rmap_head parent_ptes; /* rmap pointers to parent sptes */
		tdp_ptep_t ptep;
	};
	DECLARE_BITMAP(unsync_child_bitmap, 512);

	/*
	 * Tracks shadow pages that, if zapped, would allow KVM to create an NX
	 * huge page.  A shadow page will have nx_huge_page_disallowed set but
	 * not be on the list if a huge page is disallowed for other reasons,
	 * e.g. because KVM is shadowing a PTE at the same gfn, the memslot
	 * isn't properly aligned, etc...
	 */
	struct list_head possible_nx_huge_page_link;
#ifdef CONFIG_X86_32
	/*
	 * Used out of the mmu-lock to avoid reading spte values while an
	 * update is in progress; see the comments in __get_spte_lockless().
	 */
	int clear_spte_count;
#endif

#ifdef CONFIG_X86_64
	/* Used for freeing the page asynchronously if it is a TDP MMU page. */
	struct rcu_head rcu_head;
#endif
};

extern struct kmem_cache *mmu_page_header_cache;

static inline int kvm_mmu_role_as_id(union kvm_mmu_page_role role)
{
	return role.smm ? 1 : 0;
}

static inline int kvm_mmu_page_as_id(struct kvm_mmu_page *sp)
{
	return kvm_mmu_role_as_id(sp->role);
}

static inline bool is_mirror_sp(const struct kvm_mmu_page *sp)
{
	return sp->role.is_mirror;
}

static inline void kvm_mmu_alloc_external_spt(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	/*
	 * external_spt is allocated for TDX module to hold private EPT mappings,
	 * TDX module will initialize the page by itself.
	 * Therefore, KVM does not need to initialize or access external_spt.
	 * KVM only interacts with sp->spt for private EPT operations.
	 */
	sp->external_spt = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_external_spt_cache);
}

static inline gfn_t kvm_gfn_root_bits(const struct kvm *kvm, const struct kvm_mmu_page *root)
{
	/*
	 * Since mirror SPs are used only for TDX, which maps private memory
	 * at its "natural" GFN, no mask needs to be applied to them - and, dually,
	 * we expect that the bits is only used for the shared PT.
	 */
	if (is_mirror_sp(root))
		return 0;
	return kvm_gfn_direct_bits(kvm);
}

static inline bool kvm_mmu_page_ad_need_write_protect(struct kvm *kvm,
						      struct kvm_mmu_page *sp)
{
	/*
	 * When using the EPT page-modification log, the GPAs in the CPU dirty
	 * log would come from L2 rather than L1.  Therefore, we need to rely
	 * on write protection to record dirty pages, which bypasses PML, since
	 * writes now result in a vmexit.  Note, the check on CPU dirty logging
	 * being enabled is mandatory as the bits used to denote WP-only SPTEs
	 * are reserved for PAE paging (32-bit KVM).
	 */
	return kvm->arch.cpu_dirty_log_size && sp->role.guest_mode;
}

static inline gfn_t gfn_round_for_level(gfn_t gfn, int level)
{
	return gfn & -KVM_PAGES_PER_HPAGE(level);
}

int mmu_try_to_unsync_pages(struct kvm *kvm, const struct kvm_memory_slot *slot,
			    gfn_t gfn, bool synchronizing, bool prefetch);

void kvm_mmu_gfn_disallow_lpage(const struct kvm_memory_slot *slot, gfn_t gfn);
void kvm_mmu_gfn_allow_lpage(const struct kvm_memory_slot *slot, gfn_t gfn);
bool kvm_mmu_slot_gfn_write_protect(struct kvm *kvm,
				    struct kvm_memory_slot *slot, u64 gfn,
				    int min_level);

/* Flush the given page (huge or not) of guest memory. */
static inline void kvm_flush_remote_tlbs_gfn(struct kvm *kvm, gfn_t gfn, int level)
{
	kvm_flush_remote_tlbs_range(kvm, gfn_round_for_level(gfn, level),
				    KVM_PAGES_PER_HPAGE(level));
}

unsigned int pte_list_count(struct kvm_rmap_head *rmap_head);

extern int nx_huge_pages;
static inline bool is_nx_huge_page_enabled(struct kvm *kvm)
{
	return READ_ONCE(nx_huge_pages) && !kvm->arch.disable_nx_huge_pages;
}

struct kvm_page_fault {
	/* arguments to kvm_mmu_do_page_fault.  */
	const gpa_t addr;
	const u64 error_code;
	const bool prefetch;

	/* Derived from error_code.  */
	const bool exec;
	const bool write;
	const bool present;
	const bool rsvd;
	const bool user;

	/* Derived from mmu and global state.  */
	const bool is_tdp;
	const bool is_private;
	const bool nx_huge_page_workaround_enabled;

	/*
	 * Whether a >4KB mapping can be created or is forbidden due to NX
	 * hugepages.
	 */
	bool huge_page_disallowed;

	/*
	 * Maximum page size that can be created for this fault; input to
	 * FNAME(fetch), direct_map() and kvm_tdp_mmu_map().
	 */
	u8 max_level;

	/*
	 * Page size that can be created based on the max_level and the
	 * page size used by the host mapping.
	 */
	u8 req_level;

	/*
	 * Page size that will be created based on the req_level and
	 * huge_page_disallowed.
	 */
	u8 goal_level;

	/*
	 * Shifted addr, or result of guest page table walk if addr is a gva. In
	 * the case of VM where memslot's can be mapped at multiple GPA aliases
	 * (i.e. TDX), the gfn field does not contain the bit that selects between
	 * the aliases (i.e. the shared bit for TDX).
	 */
	gfn_t gfn;

	/* The memslot containing gfn. May be NULL. */
	struct kvm_memory_slot *slot;

	/* Outputs of kvm_mmu_faultin_pfn().  */
	unsigned long mmu_seq;
	kvm_pfn_t pfn;
	struct page *refcounted_page;
	bool map_writable;

	/*
	 * Indicates the guest is trying to write a gfn that contains one or
	 * more of the PTEs used to translate the write itself, i.e. the access
	 * is changing its own translation in the guest page tables.
	 */
	bool write_fault_to_shadow_pgtable;
};

int kvm_tdp_page_fault(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault);

/*
 * Return values of handle_mmio_page_fault(), mmu.page_fault(), fast_page_fault(),
 * and of course kvm_mmu_do_page_fault().
 *
 * RET_PF_CONTINUE: So far, so good, keep handling the page fault.
 * RET_PF_RETRY: let CPU fault again on the address.
 * RET_PF_EMULATE: mmio page fault, emulate the instruction directly.
 * RET_PF_WRITE_PROTECTED: the gfn is write-protected, either unprotected the
 *                         gfn and retry, or emulate the instruction directly.
 * RET_PF_INVALID: the spte is invalid, let the real page fault path update it.
 * RET_PF_FIXED: The faulting entry has been fixed.
 * RET_PF_SPURIOUS: The faulting entry was already fixed, e.g. by another vCPU.
 *
 * Any names added to this enum should be exported to userspace for use in
 * tracepoints via TRACE_DEFINE_ENUM() in mmutrace.h
 *
 * Note, all values must be greater than or equal to zero so as not to encroach
 * on -errno return values.
 */
enum {
	RET_PF_CONTINUE = 0,
	RET_PF_RETRY,
	RET_PF_EMULATE,
	RET_PF_WRITE_PROTECTED,
	RET_PF_INVALID,
	RET_PF_FIXED,
	RET_PF_SPURIOUS,
};

/*
 * Define RET_PF_CONTINUE as 0 to allow for
 * - efficient machine code when checking for CONTINUE, e.g.
 *   "TEST %rax, %rax, JNZ", as all "stop!" values are non-zero,
 * - kvm_mmu_do_page_fault() to return other RET_PF_* as a positive value.
 */
static_assert(RET_PF_CONTINUE == 0);

static inline void kvm_mmu_prepare_memory_fault_exit(struct kvm_vcpu *vcpu,
						     struct kvm_page_fault *fault)
{
	kvm_prepare_memory_fault_exit(vcpu, fault->gfn << PAGE_SHIFT,
				      PAGE_SIZE, fault->write, fault->exec,
				      fault->is_private);
}

static inline int kvm_mmu_do_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
					u64 err, bool prefetch,
					int *emulation_type, u8 *level)
{
	struct kvm_page_fault fault = {
		.addr = cr2_or_gpa,
		.error_code = err,
		.exec = err & PFERR_FETCH_MASK,
		.write = err & PFERR_WRITE_MASK,
		.present = err & PFERR_PRESENT_MASK,
		.rsvd = err & PFERR_RSVD_MASK,
		.user = err & PFERR_USER_MASK,
		.prefetch = prefetch,
		.is_tdp = likely(vcpu->arch.mmu->page_fault == kvm_tdp_page_fault),
		.nx_huge_page_workaround_enabled =
			is_nx_huge_page_enabled(vcpu->kvm),

		.max_level = KVM_MAX_HUGEPAGE_LEVEL,
		.req_level = PG_LEVEL_4K,
		.goal_level = PG_LEVEL_4K,
		.is_private = err & PFERR_PRIVATE_ACCESS,

		.pfn = KVM_PFN_ERR_FAULT,
	};
	int r;

	if (vcpu->arch.mmu->root_role.direct) {
		/*
		 * Things like memslots don't understand the concept of a shared
		 * bit. Strip it so that the GFN can be used like normal, and the
		 * fault.addr can be used when the shared bit is needed.
		 */
		fault.gfn = gpa_to_gfn(fault.addr) & ~kvm_gfn_direct_bits(vcpu->kvm);
		fault.slot = kvm_vcpu_gfn_to_memslot(vcpu, fault.gfn);
	}

	/*
	 * With retpoline being active an indirect call is rather expensive,
	 * so do a direct call in the most common case.
	 */
	if (IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) && fault.is_tdp)
		r = kvm_tdp_page_fault(vcpu, &fault);
	else
		r = vcpu->arch.mmu->page_fault(vcpu, &fault);

	/*
	 * Not sure what's happening, but punt to userspace and hope that
	 * they can fix it by changing memory to shared, or they can
	 * provide a better error.
	 */
	if (r == RET_PF_EMULATE && fault.is_private) {
		pr_warn_ratelimited("kvm: unexpected emulation request on private memory\n");
		kvm_mmu_prepare_memory_fault_exit(vcpu, &fault);
		return -EFAULT;
	}

	if (fault.write_fault_to_shadow_pgtable && emulation_type)
		*emulation_type |= EMULTYPE_WRITE_PF_TO_SP;
	if (level)
		*level = fault.goal_level;

	return r;
}

int kvm_mmu_max_mapping_level(struct kvm *kvm,
			      const struct kvm_memory_slot *slot, gfn_t gfn);
void kvm_mmu_hugepage_adjust(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault);
void disallowed_hugepage_adjust(struct kvm_page_fault *fault, u64 spte, int cur_level);

void track_possible_nx_huge_page(struct kvm *kvm, struct kvm_mmu_page *sp);
void untrack_possible_nx_huge_page(struct kvm *kvm, struct kvm_mmu_page *sp);

#endif /* __KVM_X86_MMU_INTERNAL_H */
