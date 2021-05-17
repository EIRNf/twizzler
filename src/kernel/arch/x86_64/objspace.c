#include <arch/x86_64-vmx.h>
#include <memory.h>
#include <object.h>
#include <objspace.h>
#include <page.h>
#include <processor.h>

uintptr_t arch_mm_objspace_max_address(void)
{
	return 2ul << arch_processor_physical_width();
}

size_t arch_mm_objspace_region_size(void)
{
	return 2 * 1024 * 1024;
}

extern struct object_space _bootstrap_object_space;

void arch_objspace_map(struct object_space *space,
  uintptr_t virt,
  struct page *pages,
  size_t count,
  uint64_t mapflags)
{
	if(!space)
		space = &_bootstrap_object_space;
	uint64_t flags = EPT_IGNORE_PAT;
	flags |= (mapflags & MAP_WRITE) ? EPT_WRITE : 0;
	flags |= (mapflags & MAP_READ) ? EPT_READ : 0;
	flags |= (mapflags & MAP_EXEC) ? EPT_EXEC : 0;

	struct arch_object_space *arch = &space->arch;
	for(size_t i = 0; i < count; i++, virt += mm_page_size(0)) {
		uintptr_t addr;
		uint64_t cf = EPT_MEMTYPE_WB;
		if(pages) {
			addr = pages[i].addr;
			switch(PAGE_CACHE_TYPE(&pages[i])) {
				case PAGE_CACHE_WB:
				default:
					break;
				case PAGE_CACHE_UC:
					cf = EPT_MEMTYPE_UC;
					break;
				case PAGE_CACHE_WT:
					cf = EPT_MEMTYPE_WT;
					break;
				case PAGE_CACHE_WC:
					cf = EPT_MEMTYPE_WC;
					break;
			}
		} else if(mapflags & MAP_ZERO) {
			addr = mm_page_alloc_addr(PAGE_ZERO);
		} else if(!(mapflags & MAP_TABLE_PREALLOC)) {
			panic("invalid page mapping strategy");
		}
		if(mapflags & MAP_TABLE_PREALLOC)
			table_premap(&arch->root, virt, 0, EPT_WRITE | EPT_READ | EPT_EXEC, true);
		else {
			// printk("map %lx -> %lx\n", virt, addr);
			table_map(
			  &arch->root, virt, addr, 0, flags | cf, EPT_WRITE | EPT_READ | EPT_EXEC, true);
		}
	}
}

void arch_objspace_unmap(uintptr_t addr, size_t nrpages, int flags)
{
	struct object_space *space = &_bootstrap_object_space;
	struct arch_object_space *arch = &space->arch;
	for(size_t i = 0; i < nrpages; i++) {
		table_unmap(&arch->root, addr + i * mm_page_size(0), flags);
	}
}

void arch_objspace_region_map_page(struct objspace_region *region,
  size_t idx,
  struct page *page,
  uint64_t flags)
{
	assert(idx < 512);
	struct rwlock_result res = rwlock_wlock(&region->arch.table.lock, 0);
	table_realize(&region->arch.table, true);

	printk("TODO: cache type\n");
	uint64_t mapflags = EPT_MEMTYPE_WB | EPT_IGNORE_PAT;
	mapflags |= (flags & MAP_READ) ? EPT_READ : 0;
	mapflags |= (flags & MAP_WRITE) ? EPT_WRITE : 0;
	mapflags |= (flags & MAP_EXEC) ? EPT_EXEC : 0;

	if(flags & PAGE_MAP_COW)
		mapflags &= EPT_WRITE;

	region->arch.table.table[idx] = mapflags | page->addr;
	rwlock_wunlock(&res);
}

uintptr_t arch_mm_objspace_get_phys(uintptr_t oaddr)
{
	struct object_space *space = &_bootstrap_object_space;
	uint64_t entry;
	int level;
	struct arch_object_space *arch = &space->arch;
	if(!table_readmap(&arch->root, oaddr, &entry, &level)) {
		return (uintptr_t)-1;
	}
	return entry & EPT_PAGE_MASK;
}

void arch_mm_objspace_invalidate(uintptr_t start, size_t len, int flags)
{
	x86_64_invvpid(start, len);
}

void arch_object_space_init(struct object_space *space)
{
	table_realize(&space->arch.root, true);
	for(int i = 0; i < 512; i++) {
		space->arch.root.children[i] = _bootstrap_object_space.arch.root.children[i];
		space->arch.root.table[i] = _bootstrap_object_space.arch.root.table[i];
	}
}

void arch_object_space_fini(struct object_space *space)
{
	panic("A");
	/*
	for(int i = 0; i < 512; i++) {
	    if(space->arch.root.children[i] && !_bootstrap_object_space.arch.root.children[i]) {
	        table_free_downward(space->arch.root.children[i]);
	        space->arch.root.children[i] = NULL;
	    }
	    space->arch.root.table[i] = 0;
	}*/
}
