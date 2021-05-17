#include <lib/list.h>
#include <memory.h>
#include <objspace.h>
#include <slab.h>

static _Atomic uintptr_t objspace_reservation = MEMORY_BOOTSTRAP_MAX;

static struct spinlock lock = SPINLOCK_INIT;
static DECLARE_LIST(region_list);

uintptr_t mm_objspace_reserve(size_t len)
{
	len = align_up(len, mm_objspace_region_size());
	uintptr_t ret = atomic_fetch_add(&objspace_reservation, len);
	return ret;
}

static struct objspace_region *new_objspace_region_struct(void)
{
	panic("A");
}

struct objspace_region *mm_objspace_allocate_region(void)
{
	spinlock_acquire_save(&lock);
	struct objspace_region *region;
	if(list_empty(&region_list)) {
		uintptr_t addr = atomic_fetch_add(&objspace_reservation, mm_objspace_region_size());
		if(addr >= arch_mm_objspace_max_address()) {
			spinlock_release_restore(&lock);
			return NULL;
		}
		region = new_objspace_region_struct();
		region->addr = addr;
	} else {
		region = list_entry(list_pop(&region_list), struct objspace_region, entry);
	}
	spinlock_release_restore(&lock);
	return region;
}

void mm_objspace_free_region(struct objspace_region *region)
{
	spinlock_acquire_save(&lock);
	list_insert(&region_list, &region->entry);
	spinlock_release_restore(&lock);
}

void mm_objspace_fill(uintptr_t addr, struct page *pages, size_t count, int flags)
{
	arch_objspace_map(NULL, addr, pages, count, flags);
}

void mm_objspace_unmap(uintptr_t addr, size_t nrpages, int flags)
{
	arch_objspace_unmap(addr, nrpages, flags);
}

struct omap *mm_objspace_get_object_map(struct object *obj, size_t page)
{
	panic("A");
}

struct omap *mm_objspace_lookup_omap_addr(uintptr_t addr)
{
	panic("A");
}

uintptr_t mm_objspace_get_phys(uintptr_t oaddr)
{
	return arch_mm_objspace_get_phys(oaddr);
}

static void object_space_init(void *data __unused, void *ptr)
{
	struct object_space *space = ptr;
	arch_object_space_init(space);
}

static void object_space_fini(void *data __unused, void *ptr)
{
	struct object_space *space = ptr;
	arch_object_space_fini(space);
}

static DECLARE_SLABCACHE(sc_objspace,
  sizeof(struct object_space),
  object_space_init,
  NULL,
  NULL,
  object_space_fini,
  NULL);

struct object_space *object_space_alloc(void)
{
	return slabcache_alloc(&sc_objspace);
}

void object_space_free(struct object_space *space)
{
	slabcache_free(&sc_objspace, space);
}
