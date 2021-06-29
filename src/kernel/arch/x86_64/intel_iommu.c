#include <arch/x86_64-acpi.h>
#include <debug.h>
#include <init.h>
#include <machine/pc-pcie.h>
#include <memory.h>
#include <page.h>
#include <pmap.h>
#include <slots.h>
#include <system.h>

/* Support for the intel IOMMU is a work in-progress. Our eventual goal is to support more
 * autonomous hardware. For now, however, we're getting basic drivers working. */

struct __packed device_scope {
	uint8_t type;
	uint8_t length;
	uint16_t resv;
	uint8_t enumer_id;
	uint8_t start_bus_nr;
	uint16_t path[];
};

struct __packed dmar_remap {
	uint16_t type;
	uint16_t length;
	uint8_t flags;
	uint8_t resv;
	uint16_t segnr;
	uint64_t reg_base_addr;
	struct device_scope scopes[];
};

struct __packed dmar_desc {
	struct sdt_header header;
	uint8_t host_addr_width;
	uint8_t flags;
	uint8_t resv[10];
	struct dmar_remap remaps[];
};

static struct dmar_desc *dmar;
size_t remap_entries = 0;

struct iommu {
	uint64_t base;
	uint16_t pcie_seg;
	uint16_t id;
	uintptr_t root_table;
	void *root_table_virt;
	uint64_t cap;
	bool init;
	void **table_pages;
	void *qi_addr;
	size_t qi_len, qi_tail, qi_stride;
	struct spinlock qi_lock;
};

#define MAX_IOMMUS 16
static struct iommu iommus[MAX_IOMMUS] = {};

#define IOMMU_REG_VERS 0
#define IOMMU_REG_CAP 8
#define IOMMU_REG_EXCAP 0x10
#define IOMMU_REG_GCMD 0x18
#define IOMMU_REG_GSTS 0x1c
#define IOMMU_REG_RTAR 0x20
#define IOMMU_REG_CCMD 0x28
#define IOMMU_REG_FSR 0x34
#define IOMMU_REG_FEC 0x38
#define IOMMU_REG_FED 0x3c
#define IOMMU_REG_FEA 0x40
#define IOMMU_REG_FEUA 0x44
#define IOMMU_REG_AFL 0x58
#define IOMMU_REG_IQH 0x80
#define IOMMU_REG_IQT 0x88
#define IOMMU_REG_IQA 0x90
#define IOMMU_REG_ICS 0x9c
#define IOMMU_REG_ICEC 0xa0
#define IOMMU_REG_ICED 0xa4
#define IOMMU_REG_ICEA 0xa8
#define IOMMU_REG_ICEUA 0xac
#define IOMMU_REG_IQER 0xb0
#define IOMMU_REG_IRTADDR 0xb8
#define IOMMU_REG_PRQH 0xc0
#define IOMMU_REG_PRQT 0xc8
#define IOMMU_REG_PRQA 0xd0
#define IOMMU_REG_PRSR 0xdc
#define IOMMU_REG_PREC 0xe0
#define IOMMU_REG_PRED 0xe4
#define IOMMU_REG_PREA 0xe8
#define IOMMU_REG_PREUA 0xec

#define IOMMU_CAP_NFR(x) ((((x) >> 40) & 0xff) + 1)
#define IOMMU_CAP_SLLPS(x) (((x) >> 34) & 0xf)
#define IOMMU_CAP_FRO(x) ((((x) >> 24) & 0x3ff) * 16)
#define IOMMU_CAP_CM (1 << 7)
#define IOMMU_CAP_ND(x) (((x)&7) * 2 + 4)

#define IOMMU_EXCAP_DT (1 << 2)
#define IOMMU_EXCAP_QI (1 << 1)

#define IOMMU_GCMD_QIE (1ul << 26)
#define IOMMU_GCMD_TE (1ul << 31)
#define IOMMU_GCMD_SRTP (1ul << 30)

#define IOMMU_RTAR_TTM_LEGACY 0
#define IOMMU_RTAR_TTM_SCALABLE 1

#define IOMMU_CCMD_ICC (1ul << 63)
#define IOMMU_CCMD_SRC(x) ((x) << 16)
#define IOMMU_CCMD_DID(x) ((x))

#define IOMMU_FSR_FRI (((x) >> 8) & 0xff)
#define IOMMU_FSR_PPF (1 << 1)
#define IOMMU_FSR_PFO (1 << 0)
#define IOMMU_FEC_IM (1 << 31)
#define IOMMU_FEC_IP (1 << 30)

#define IOMMU_FRRH_FAULT (1ul << 63)
#define IOMMU_FRRH_TYPE1 (1ul << 62)
#define IOMMU_FRRH_FR(x) (((x) >> 32 & 0xFF))
#define IOMMU_FRRH_EXE (1ul << 30)
#define IOMMU_FRRH_SID(x) ((x)&0xFFFF)
#define IOMMU_FRRH_TYPE2 (1ul << 28)

#define IOMMU_CTXE_PRESENT 1
#define IOMMU_CTXE_AW48 2
#define IOMMU_CTXE_AW39 1

#define IOMMU_RTE_PRESENT 1
#define IOMMU_RTE_MASK 0xFFFFFFFFFFFFF000

struct iommu_rte {
	uint64_t lo;
	uint64_t hi;
};

struct iommu_ctxe {
	uint64_t lo;
	uint64_t hi;
};

static uint32_t iommu_read32(struct iommu *im, int reg)
{
	return *(volatile uint32_t *)(im->base + reg);
}

static void iommu_write32(struct iommu *im, int reg, uint32_t val)
{
	*(volatile uint32_t *)(im->base + reg) = val;
	asm volatile("mfence" ::: "memory");
}

static uint64_t iommu_read64(struct iommu *im, int reg)
{
	return *(volatile uint64_t *)(im->base + reg);
}

static void iommu_write64(struct iommu *im, int reg, uint64_t val)
{
	*(volatile uint64_t *)(im->base + reg) = val;
	asm volatile("mfence" ::: "memory");
}

static void iommu_status_wait(struct iommu *im, uint32_t ws, bool set)
{
	if(set) {
		while(!(iommu_read32(im, IOMMU_REG_GSTS) & ws))
			asm("pause");
	} else {
		while((iommu_read32(im, IOMMU_REG_GSTS) & ws))
			asm("pause");
	}
}

static void iommu_set_context_entry(struct iommu *im,
  uint8_t bus,
  uint8_t dfn,
  uintptr_t ptroot,
  uint16_t did)
{
	struct iommu_rte *rt = im->root_table_virt;
	if(!(rt[bus].lo & IOMMU_RTE_PRESENT)) {
		im->table_pages[bus] = kheap_allocate_pages(0x1000, 0);
		rt[bus].lo = mm_vtop(im->table_pages[bus]) | IOMMU_RTE_PRESENT;
		asm volatile("clflush %0" ::"m"(rt[bus]));
	}
	struct iommu_ctxe *ct = im->table_pages[bus];
	// struct iommu_ctxe *ct = mm_ptov(rt[bus].lo & IOMMU_RTE_MASK);
	if(!(ct[dfn].lo & IOMMU_CTXE_PRESENT)) {
		ct[dfn].lo = ptroot | IOMMU_CTXE_PRESENT;
		ct[dfn].hi = IOMMU_CTXE_AW48 | (did << 8);
		asm volatile("clflush %0" ::"m"(ct[dfn]));
	}
}

#include <arch/x86_64-vmx.h>
#include <object.h>
#include <processor.h>

static uintptr_t ept_phys;
static void *ept_virt;
static void *pdpts[512];

static void do_iommu_object_map_slot(struct object *obj, uint64_t flags)
{
	/* TODO: map w/ permissions */
	(void)flags;
	uintptr_t virt;
	if(obj->flags & OF_KERNEL) {
		virt = obj->kslot->num * OBJ_MAXSIZE;
	} else {
		assert(obj->slot != NULL);
		virt = obj->slot->num * OBJ_MAXSIZE;
	}
	int pml4_idx = PML4_IDX(virt);
	int pdpt_idx = PDPT_IDX(virt);

	// panic("IOMMU");

	uintptr_t *pml4 = ept_virt;
	if(pml4[pml4_idx] == 0) {
		pdpts[pml4_idx] = kheap_allocate_pages(0x1000, 0);
		pml4[pml4_idx] = mm_vtop(pdpts[pml4_idx]) | EPT_READ | EPT_WRITE | EPT_EXEC;
		asm volatile("clflush %0" ::"m"(pml4[pml4_idx]));
	}

	uintptr_t *pdpt = pdpts[pml4_idx];
	pdpt[pdpt_idx] = obj->arch.pt_root | 7;
	asm volatile("clflush %0" ::"m"(pdpt[pdpt_idx]));
}

#include <device.h>
void iommu_object_map_slot(struct device *dev, struct object *obj)
{
	if(obj)
		do_iommu_object_map_slot(obj, 0);
	if(dev) {
		uint16_t seg = (dev->uid >> 16) & 0xffff;
		uint16_t sid = dev->uid & 0xffff;
		for(size_t i = 0; i < MAX_IOMMUS; i++) {
			if(iommus[i].base && iommus[i].pcie_seg == seg) {
				iommu_set_context_entry(&iommus[i], sid >> 8, sid & 0xff, ept_phys, 1);
			}
		}
	}
}

/* TODO: generalize */
static void __iommu_fault_handler(int v __unused, struct interrupt_handler *h __unused)
{
	for(int i = 0; i < MAX_IOMMUS; i++) {
		struct iommu *im = &iommus[i];
		if(!im->init)
			continue;
		uint32_t fsr = iommu_read32(im, IOMMU_REG_FSR);
		iommu_write32(im, IOMMU_REG_FSR, fsr);
		if(fsr & IOMMU_FSR_PPF) {
			/* TODO: should use the FRI field */
			for(unsigned fr = 0; fr < IOMMU_CAP_NFR(im->cap); fr++) {
				uint64_t flo = iommu_read64(im, IOMMU_CAP_FRO(im->cap) + (fr * 16));
				uint64_t fhi = iommu_read64(im, IOMMU_CAP_FRO(im->cap) + (fr * 16) + 8);
				if(!(fhi & IOMMU_FRRH_FAULT))
					continue;
				iommu_write64(im, IOMMU_CAP_FRO(im->cap) + (fr * 16) + 8, IOMMU_FRRH_FAULT);
				uint16_t sid = IOMMU_FRRH_SID(fhi);
				printk(":: detected fault from device %x:%x:%x.%x access to %lx\n",
				  im->pcie_seg,
				  sid >> 8,
				  (sid >> 3) & 0xf,
				  (sid & 7),
				  flo);
				size_t slot_num = flo / mm_page_size(MAX_PGLEVEL);
				size_t idx = (flo % mm_page_size(MAX_PGLEVEL)) / mm_page_size(0);
				struct slot *slot;
				struct object *o = obj_lookup_slot(flo, &slot);
				if(o) {
					do_iommu_object_map_slot(o, 0);
					iommu_set_context_entry(im, sid >> 8, sid & 0xff, ept_phys, 1);
					struct objpage *p;
					panic("A");
					// obj_get_page(o, idx, &p, OBJ_GET_PAGE_ALLOC);

					printk("[iommu] mapping page %ld\n", p->idx);
					if(!(p->flags & OBJPAGE_MAPPED)) {
						arch_object_map_page(o, p->idx, p->page, 0);
						p->flags |= OBJPAGE_MAPPED;
					}
					arch_object_map_flush(o, p->idx * mm_page_size(0) /* TODO: this is brittle */);

					panic("A");
					// objpage_release(p, 0);
					obj_put(o);
				} else {
					printk("[iommu] fault to slot %ld; unknown object\n", slot_num);
				}
				pcie_iommu_fault(im->pcie_seg, sid, flo, !!o);
				slot_release(slot);
			}
		}
		if(fsr & IOMMU_FSR_PFO) {
			printk("[iommu] warning - primary fault overflow\n");
		}
	}
}

static void __iommu_inv_handler(int v __unused, struct interrupt_handler *h __unused)
{
	printk("!!! IOMMU INV\n");
}

static struct interrupt_alloc_req _iommu_int_iaq[2] = {
	[0] = {
		.pri = IVP_NORMAL,
		.handler.fn = __iommu_fault_handler,
		.flags = INTERRUPT_ALLOC_REQ_VALID,
	},
	[1] = {
		.pri = IVP_NORMAL,
		.handler.fn = __iommu_inv_handler,
		.flags = INTERRUPT_ALLOC_REQ_VALID,
	}
};

union qi_entry {
	struct {
		uint64_t qw0;
		uint64_t qw1;
	};
};

struct iommu_inval {
	union qi_entry entry;
	volatile uint32_t status;
};

static void iommu_submit_invalidation(struct iommu *im, struct iommu_inval *inv)
{
	spinlock_acquire_save(&im->qi_lock);

	while(((im->qi_tail + im->qi_stride) % im->qi_len) == iommu_read64(im, IOMMU_REG_IQH)) {
		spinlock_release_restore(&im->qi_lock);
		arch_processor_relax();
		spinlock_acquire_save(&im->qi_lock);
	}

	union qi_entry *entry = (void *)((char *)im->qi_addr + im->qi_tail);
	*entry = inv->entry;

	im->qi_tail = (im->qi_tail + im->qi_stride) % im->qi_len;
	iommu_write64(im, IOMMU_REG_IQT, im->qi_tail);

	spinlock_release_restore(&im->qi_lock);
}

#define IOMMU_INVL_IOTLB_GRAN_GLOBAL (1 << 4)
#define IOMMU_INVL_IOTLB_GRAN_DSEL (2 << 4)
#define IOMMU_INVL_IOTLB_GRAN_PSEL (3 << 4)

static void iommu_build_iotlb_invl(struct iommu_inval *inv,
  uintptr_t addr,
  bool hint,
  uint8_t addr_mask,
  uint16_t did,
  uint8_t flags)
{
	inv->entry.qw1 = addr | (hint ? (1 << 6) : 0) | addr_mask;
	inv->entry.qw0 = ((uint32_t)did << 16) | flags | 2;
}

#define IOMMU_INVL_WAIT_STATWR (1 << 5)
#define IOMMU_INVL_WAIT_IF (1 << 4)
#define IOMMU_INVL_WAIT_FENCE (1 << 6)

static void iommu_build_wait_invl(struct iommu_inval *inv,
  uintptr_t addr,
  uint32_t status,
  uint8_t flags)
{
	inv->entry.qw1 = addr;
	inv->entry.qw0 = ((uint64_t)status << 32) | flags | 5;
	inv->status = 0;
}

void iommu_invalidate_tlb(void)
{
	for(size_t i = 0; i < MAX_IOMMUS; i++) {
		if(iommus[i].base) {
			struct iommu *im = &iommus[i];
			struct iommu_inval inv;
			iommu_build_iotlb_invl(&inv, 0, false, 0, 0, IOMMU_INVL_IOTLB_GRAN_GLOBAL);
			iommu_submit_invalidation(im, &inv);
			iommu_build_wait_invl(&inv, mm_vtop((void *)&inv.status), 1, IOMMU_INVL_WAIT_STATWR);
			iommu_submit_invalidation(im, &inv);
			while(inv.status != 1) {
				arch_processor_relax();
			}
		}
	}
}

static int iommu_init(struct iommu *im)
{
	/* TODO: verify caps and ecaps */
	// uint32_t vs = iommu_read32(im, IOMMU_REG_VERS);
	uint64_t cap = iommu_read64(im, IOMMU_REG_CAP);
	uint64_t ecap = iommu_read64(im, IOMMU_REG_EXCAP);
	im->cap = cap;

	ept_virt = kheap_allocate_pages(0x1000, 0);
	ept_phys = mm_vtop(ept_virt);

	// printk(":: %lx %lx\n", cap, ecap);
	/*	printk("nfr=%lx, sllps=%lx, fro=%lx, nd=%ld\n",
	      IOMMU_CAP_NFR(cap),
	      IOMMU_CAP_SLLPS(cap),
	      IOMMU_CAP_FRO(cap),
	      IOMMU_CAP_ND(cap));
	      */
	if(IOMMU_CAP_ND(cap) < 16) {
		printk("[iommu] iommu %d does not support large enough domain ID\n", im->id);
		// return -1;
	}
	if(IOMMU_CAP_SLLPS(cap) != 3) {
		printk("[iommu] iommu %d does not support huge pages at sl translation\n", im->id);
		return -1;
	}

	if(!(ecap & IOMMU_EXCAP_QI)) {
		printk("[iommu] iommu %d does not support queued invalidations\n", im->id);
		/* TODO: should we support this? */
		return -1;
	}

	/* first disable the hardware during init */
	iommu_write32(im, IOMMU_REG_GCMD, 0);
	/* if it was enabled, wait for it to disable */
	iommu_status_wait(im, IOMMU_GCMD_TE, false);

	/* set the root table */
	im->root_table_virt = kheap_allocate_pages(0x1000, 0);
	im->root_table = mm_vtop(im->root_table_virt);
	iommu_write64(im, IOMMU_REG_RTAR, im->root_table | IOMMU_RTAR_TTM_LEGACY);

	iommu_write32(im, IOMMU_REG_GCMD, IOMMU_GCMD_SRTP);
	iommu_status_wait(im, IOMMU_GCMD_SRTP, true);

	/* allocate interrupt vectors for the iommu itself. It need to inform us of faults and of
	 * invalidation completions. It uses message-signaled interrupts. */
	iommu_write32(im, IOMMU_REG_FED, _iommu_int_iaq[0].vec);
	iommu_write32(im, IOMMU_REG_FEA, x86_64_msi_addr(0, X86_64_MSI_DM_PHYSICAL));
	iommu_write32(im, IOMMU_REG_FEUA, 0);
	/* unmask the FECTL interrupt */
	iommu_write32(im, IOMMU_REG_FEC, 0);

	iommu_write32(im, IOMMU_REG_ICED, _iommu_int_iaq[1].vec);
	iommu_write32(im, IOMMU_REG_ICEA, x86_64_msi_addr(0, X86_64_MSI_DM_PHYSICAL));
	iommu_write32(im, IOMMU_REG_ICEUA, 0);

	im->table_pages = kheap_allocate_pages(sizeof(void *) * 512, 0);
	im->init = true;
	// uint32_t cmd = IOMMU_GCMD_TE;
	// iommu_write32(im, IOMMU_REG_GCMD, cmd);
	// iommu_status_wait(im, IOMMU_GCMD_TE, true);

	/* enable queued invalidations */
	im->qi_len = 0x1000;
	im->qi_addr = kheap_allocate_pages(0x1000, 0);

	uint64_t val = mm_vtop(im->qi_addr);

	iommu_write64(im, IOMMU_REG_IQT, 0);
	iommu_write64(im, IOMMU_REG_IQA, val);

	iommu_write32(im, IOMMU_REG_ICEC, 0);

	iommu_write32(im, IOMMU_REG_GCMD, IOMMU_GCMD_QIE | IOMMU_GCMD_TE);
	iommu_status_wait(im, IOMMU_GCMD_QIE, true);
	im->qi_lock = SPINLOCK_INIT;
	im->qi_stride = 16;

#if 0
	struct iommu_inval iv;
	iommu_build_iotlb_invl(&iv, false, 0, 0, 0, IOMMU_INVL_IOTLB_GRAN_GLOBAL);
	iommu_submit_invalidation(im, &iv);

	iommu_build_wait_invl(&iv, mm_vtop(&iv.status), 1234, IOMMU_INVL_WAIT_STATWR);
	iommu_submit_invalidation(im, &iv);

	// asm volatile("sti");

	for(;;) {
		for(volatile long i = 0; i < 1000000000; i++)
			;
		printk("%d\n", iv.status);
	}

	for(;;)
		;
#endif
	return 0;
}

static void dmar_late_init(void *__u __unused)
{
	interrupt_allocate_vectors(2, _iommu_int_iaq);
	printk("[iommu] allocated vectors (%d, %d) for iommu\n",
	  _iommu_int_iaq[0].vec,
	  _iommu_int_iaq[1].vec);
	for(size_t i = 0; i < MAX_IOMMUS; i++) {
		if(iommus[i].base) {
			iommu_init(&iommus[i]);
		}
	}
}
POST_INIT(dmar_late_init);

__orderedinitializer(__orderedafter(ACPI_INITIALIZER_ORDER)) static void dmar_init(void)
{
	if(!(dmar = acpi_find_table("DMAR"))) {
		return;
	}

	remap_entries = (dmar->header.length - sizeof(struct dmar_desc)) / sizeof(struct dmar_remap);
	printk("[iommu] found DMAR header with %ld remap entries (haw=%d; flags=%x)\n",
	  remap_entries,
	  dmar->host_addr_width,
	  dmar->flags);
	size_t ie = 0;
	struct dmar_remap *remap = &dmar->remaps[0];
	for(size_t i = 0; i < remap_entries; i++) {
		iommus[ie].id = ie;
		size_t scope_entries =
		  (remap->length - sizeof(struct dmar_remap)) / sizeof(struct device_scope);
		if(remap->type != 0)
			continue;
		printk(
		  "[iommu] %d remap: type=%d, length=%d, flags=%x, segnr=%d, base_addr=%lx, %ld device "
		  "scopes\n",
		  iommus[ie].id,
		  remap->type,
		  remap->length,
		  remap->flags,
		  remap->segnr,
		  remap->reg_base_addr,
		  scope_entries);
#if 0
		for(size_t j = 0; j < scope_entries; j++) {
			size_t path_len = (remap->scopes[j].length - sizeof(struct device_scope)) / 2;
			printk("[iommu]    dev_scope: type=%d, length=%d, enumer_id=%d, start_bus=%d, "
			       "path_len=%ld\n",
			  dmar->remaps[i].scopes[j].type,
			  dmar->remaps[i].scopes[j].length,
			  dmar->remaps[i].scopes[j].enumer_id,
			  dmar->remaps[i].scopes[j].start_bus_nr,
			  path_len);
			for(size_t k = 0; k < path_len; k++) {
				//		printk("[iommu]      path %ld: %x\n", k, dmar->remaps[i].scopes[j].path[k]);
			}
		}
#endif

		if(1 || remap->flags & 1 /* PCIe include all */) {
			iommus[ie].base =
			  (uint64_t)pmap_allocate(remap->reg_base_addr, 0x1000 /* TODO length */, PMAP_UC);
			//	iommus[ie].base = (uint64_t)mm_ptov(remap->reg_base_addr);
			iommus[ie].pcie_seg = remap->segnr;
			ie++;
		} else {
			printk("[iommu] warning - remap hardware without PCI-include-all bit unsupported\n");
		}
		remap = (void *)((char *)remap + remap->length);
	}
}
