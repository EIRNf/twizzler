#include <arena.h>
#include <memory.h>
#include <object.h>
#include <pmap.h>
#include <slots.h>
#include <spinlock.h>
#include <tmpmap.h>
#include <twz/sys/dev/memory.h>

void tmpmap_collect_stats(struct memory_stats *stats)
{
}

__initializer static void tmpmap_init(void)
{
}

//#include <processor.h>
void tmpmap_unmap_page(void *addr)
{
#if 0
	uintptr_t virt = (uintptr_t)addr - (uintptr_t)SLOT_TO_VADDR(KVSLOT_TMP_MAP);
	int level = 0;
	if(virt >= OBJ_MAXSIZE / 2) {
		level = 1;
		virt -= OBJ_MAXSIZE / 2;
	}
	spinlock_acquire_save(&lock);
	uint8_t *bitmap = level ? l1bitmap : l0bitmap;
	size_t i = virt / mm_page_size(level);
	bitmap[i / 8] &= ~(1 << (i % 8));
	// struct objpage *op = obj_get_page(&tmpmap_object, virt, false);
	// assert(op);
	// op->flags &= ~OBJPAGE_MAPPED;
	/* TODO: un-cache page from object */
	spinlock_acquire_save(&tmpmap_object.lock);
	arch_object_unmap_page(&tmpmap_object, virt / mm_page_size(0));
	spinlock_release_restore(&tmpmap_object.lock);

	alloced -= mm_page_size(level);

	asm volatile("invlpg %0" ::"m"(addr) : "memory");
	// if(current_thread)
	//	processor_send_ipi(
	//	  PROCESSOR_IPI_DEST_OTHERS, PROCESSOR_IPI_SHOOTDOWN, NULL, PROCESSOR_IPI_NOWAIT);

	spinlock_release_restore(&lock);
#endif
	panic("TMPMAP");
}

void *tmpmap_map_page(struct page *page)
{
#if 0
	uintptr_t virt;
	spinlock_acquire_save(&lock);

	size_t max = (OBJ_MAXSIZE / mm_page_size(page->level)) / 2;
	uint8_t *bitmap = page->level ? l1bitmap : l0bitmap;
	size_t i;
	for(i = 0; i < max; i++) {
		if(!(bitmap[i / 8] & (1 << (i % 8)))) {
			bitmap[i / 8] |= (1 << (i % 8));
			break;
		}
	}
	alloced += mm_page_size(page->level);
	spinlock_release_restore(&lock);
	if(i == max) {
		/* TODO: do something smarter */
		panic("out of tmp maps");
	}
	virt = i * mm_page_size(page->level);
	if(page->level)
		virt += OBJ_MAXSIZE / 2;

	// assert(!op || !(op->flags & OBJPAGE_MAPPED));
	obj_cache_page(&tmpmap_object, virt, page);
	struct objpage *op;
	obj_get_page(&tmpmap_object, virt, &op, 0);
	spinlock_acquire_save(&tmpmap_object.lock);
	op->flags &= ~OBJPAGE_COW;
	arch_object_map_page(&tmpmap_object, op);
	objpage_release(op, OBJPAGE_RELEASE_OBJLOCKED);
	spinlock_release_restore(&tmpmap_object.lock);
	int x;
	asm volatile("invept %0, %%rax" ::"m"(x), "r"(0));
	asm volatile("invlpg (%0)" ::"r"(virt + (uintptr_t)SLOT_TO_VADDR(KVSLOT_TMP_MAP)) : "memory");
	/* TODO: better invalidation */
	// printk("tmpmap: %lx %lx\n", virt, SLOT_TO_VADDR(KVSLOT_TMP_MAP));
	return (void *)(virt + (uintptr_t)SLOT_TO_VADDR(KVSLOT_TMP_MAP));
#endif
	panic("TMPMAP");
}
