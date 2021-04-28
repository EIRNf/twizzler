#include <kalloc.h>
#include <lib/iter.h>
#include <lib/list.h>
#include <lib/rb.h>
#include <nvdimm.h>
#include <object.h>
#include <page.h>
#include <pager.h>
#include <processor.h>
#include <slab.h>
#include <tmpmap.h>

#include <twz/sys/syscall.h>

static struct slabcache sc_objpage;

void obj_system_init_objpage(void)
{
	slabcache_init(&sc_objpage, "sc_objpage", sizeof(struct objpage), NULL, NULL, NULL);
}

static int __objpage_compar_key(struct objpage *a, size_t n)
{
	if(a->idx > n)
		return 1;
	else if(a->idx < n)
		return -1;
	return 0;
}

static int __objpage_compar(struct objpage *a, struct objpage *b)
{
	return __objpage_compar_key(a, b->idx);
}

static int __objpage_idxmap_compar_key(struct objpage *a, size_t n)
{
	if(a->srcidx > n)
		return 1;
	else if(a->srcidx < n)
		return -1;
	return 0;
}

static int __objpage_idxmap_compar(struct objpage *a, struct objpage *b)
{
	return __objpage_idxmap_compar_key(a, b->srcidx);
}

static struct objpage *objpage_alloc(struct object *obj)
{
	struct objpage *op = slabcache_alloc(&sc_objpage);
	op->flags = 0;
	op->srcidx = 0;
	op->page = NULL;
	op->lock = SPINLOCK_INIT;
	krc_init(&op->refs);
	op->obj = obj; /* weak */
	return op;
}

static void objpage_delete(struct objpage *op)
{
	if(op->page) {
		if(op->flags & OBJPAGE_COW) {
			if(op->page->cowcount-- <= 1) {
				if(op->page->type == PAGE_TYPE_VOLATILE) {
					page_dealloc(op->page, 0);
				} else if(op->page->type == PAGE_TYPE_PERSIST) {
					//		nv_region_free_page(op->obj, op->idx, op->page->addr);
					/* TODO: free the page struct */
				}
			}
		} else {
			if(op->page->type == PAGE_TYPE_VOLATILE) {
				page_dealloc(op->page, 0);
			} else if(op->page->type == PAGE_TYPE_PERSIST) {
				//	nv_region_free_page(op->obj, op->idx, op->page->addr);
				/* TODO: free the page struct */
			}
		}
		op->page = NULL;
	}
	slabcache_free(&sc_objpage, op);
}

void objpage_release(struct objpage *op, int flags)
{
	struct object *obj = op->obj;
	if(!(flags & OBJPAGE_RELEASE_OBJLOCKED)) {
		spinlock_acquire_save(&obj->lock);
	}
	spinlock_acquire_save(&op->lock);
	if(krc_put(&op->refs)) {
		objpage_delete(op);
	} else {
		spinlock_release_restore(&op->lock);
	}
	if(!(flags & OBJPAGE_RELEASE_OBJLOCKED)) {
		spinlock_release_restore(&obj->lock);
	}
}

#define OBJPAGE_CLONE_REMAP 1

static struct objpage *objpage_clone(struct object *newobj, struct objpage *op, int flags)
{
	struct objpage *new_op = objpage_alloc(newobj);
	spinlock_acquire_save(&op->lock);
	if(op->page) {
		new_op->page = op->page;
		if(flags & OBJPAGE_CLONE_REMAP) {
			arch_object_page_remap_cow(op);
		}
		op->page->cowcount++;
	}
	op->flags |= OBJPAGE_COW;
	op->flags &= ~OBJPAGE_MAPPED;
	spinlock_release_restore(&op->lock);
	new_op->idx = op->idx;
	new_op->flags = OBJPAGE_COW;

	return new_op;
}

void obj_clone_cow(struct object *src, struct object *nobj)
{
	arch_object_remap_cow(src);
	for(struct rbnode *node = rb_first(&src->pagecache_root); node; node = rb_next(node)) {
		struct objpage *pg = rb_entry(node, struct objpage, node);

		struct objpage *new_op;
		if(pg->page) {
			new_op = objpage_clone(nobj, pg, 0);
		} else {
			new_op = objpage_alloc(nobj);
			new_op->srcidx = pg->idx;
			rb_insert(
			  &nobj->idx_map, new_op, struct objpage, idx_map_node, __objpage_idxmap_compar);
		}
		new_op->idx = pg->idx;
		rb_insert(&nobj->pagecache_root, new_op, struct objpage, node, __objpage_compar);
	}
}

int obj_copy_pages(struct object *dest, struct object *src, size_t doff, size_t soff, size_t len)
{
	if(dest->sourced_from && src) {
		return -EINVAL;
	}
	if(src) {
		spinlock_acquire_save(&src->lock);
		arch_object_remap_cow(src);
		spinlock_release_restore(&src->lock);
	}
	spinlock_acquire_save(&dest->lock);

	if(src) {
		krc_get(&src->refs);
		dest->sourced_from = src;
		dest->flags |= OF_PARTIAL;

		struct derivation_info *di = kalloc(sizeof(*di));
		di->id = dest->id;
		list_insert(&src->derivations, &di->entry);

#if 0
		struct rbnode *fn = rb_first(&src->pagecache_root);
		struct rbnode *ln = rb_last(&src->pagecache_root);
		if(fn) {
			struct objpage *pg = rb_entry(fn, struct objpage, node);
			first_idx = pg->idx;
		}
		if(ln) {
			struct objpage *pg = rb_entry(ln, struct objpage, node);
			last_idx = pg->idx;
		}
#endif
	}

	/* TODO (perf): do this lazily */
	for(size_t i = 0; i < len / mm_page_size(0); i++) {
		size_t dst_idx = doff / mm_page_size(0) + i;

		struct rbnode *dnode =
		  rb_search(&dest->pagecache_root, dst_idx, struct objpage, node, __objpage_compar_key);
		if(dnode) {
			struct objpage *dp = rb_entry(dnode, struct objpage, node);
			rb_delete(dnode, &dp->obj->pagecache_root);
			if(dp->srcidx) {
				dp->srcidx = 0;
				rb_delete(&dp->idx_map_node, &dp->obj->idx_map);
			}
			objpage_release(dp, OBJPAGE_RELEASE_OBJLOCKED);
		}

		if(src) {
			spinlock_acquire_save(&src->lock);
			size_t src_idx = soff / mm_page_size(0) + i;
			struct rbnode *node =
			  rb_search(&src->pagecache_root, src_idx, struct objpage, node, __objpage_compar_key);
			struct objpage *new_op;
			struct objpage *pg = node ? rb_entry(node, struct objpage, node) : NULL;
			if(node && pg->page) {
				new_op = objpage_clone(dest, pg, 0);
			} else {
				new_op = objpage_alloc(dest);
				new_op->srcidx = src_idx;
				rb_insert(
				  &dest->idx_map, new_op, struct objpage, idx_map_node, __objpage_idxmap_compar);
			}
			new_op->idx = dst_idx;
			rb_insert(&dest->pagecache_root, new_op, struct objpage, node, __objpage_compar);
			spinlock_release_restore(&src->lock);
		}
	}

	spinlock_release_restore(&dest->lock);
	if(src) {
	}
	return 0;
}

static struct objpage *__obj_get_large_page(struct object *obj, size_t addr)
{
	size_t idx = addr / mm_page_size(1);
	struct objpage *op = NULL;
	struct rbnode *node =
	  rb_search(&obj->pagecache_level1_root, idx, struct objpage, node, __objpage_compar_key);
	if(node) {
		op = rb_entry(node, struct objpage, node);
		krc_get(&op->refs);
		return op;
	}
	return NULL;
}

static void __obj_get_page_alloc_page(struct objpage *op)
{
	struct page *pp;
	if(op->obj->flags & OF_PERSIST) {
		assert(op->obj->preg);
		pp = nv_region_pagein(op->obj, op->idx);
	} else {
		pp = page_alloc(PAGE_TYPE_VOLATILE, PAGE_ZERO, 0);
	}
	pp->cowcount = 1;
	op->flags = 0;
	op->page = pp;
	op->page->flags |= flag_if_notzero(op->obj->cache_mode & OC_CM_UC, PAGE_CACHE_UC);
	op->page->flags |= flag_if_notzero(op->obj->cache_mode & OC_CM_WB, PAGE_CACHE_WB);
	op->page->flags |= flag_if_notzero(op->obj->cache_mode & OC_CM_WT, PAGE_CACHE_WT);
	op->page->flags |= flag_if_notzero(op->obj->cache_mode & OC_CM_WC, PAGE_CACHE_WC);
}

static void __obj_get_page_handle_derived(struct objpage *op)
{
	/* okay, we're adding a page to an object. We need to check:
	 *   for each object with pages derived from this object, would we have cared about this page
	 * when deriving? This means,
	 *   - if the object is "whole-derived", check the page with the same idx as op.
	 *   - if the object is partially derived, look up the page in the src_idx -> dest_idx map for
	 *     derived object, and check that page.
	 */

	assert(op->page);
	struct object *obj = op->obj;
	foreach(e, list, &obj->derivations) {
		struct derivation_info *derived = list_entry(e, struct derivation_info, entry);

		struct object *derived_obj = obj_lookup(derived->id, 0);
		if(!derived_obj)
			continue;

		spinlock_acquire_save(&derived_obj->lock);
		struct rbnode *node = rb_search(&derived_obj->idx_map,
		  op->idx,
		  struct objpage,
		  idx_map_node,
		  __objpage_idxmap_compar_key);
		if(node) {
			rb_delete(node, &derived_obj->idx_map);
			struct objpage *dop = rb_entry(node, struct objpage, idx_map_node);
			dop->srcidx = 0;
			if(dop->page == NULL) {
				spinlock_acquire_save(&op->lock);
				op->flags |= OBJPAGE_COW;
				op->flags &= ~OBJPAGE_MAPPED;
				assert(op->page->cowcount > 0);
				op->page->cowcount++;
				dop->flags = OBJPAGE_COW;
				dop->page = op->page;
				spinlock_release_restore(&op->lock);
				__obj_get_page_handle_derived(dop);
			}
		}
		spinlock_release_restore(&derived_obj->lock);

		/* TODO: this can lead to deadlock */
		obj_put(derived_obj);
	}
}

static void __obj_get_page_alloc(struct object *obj, size_t idx, struct objpage **result)
{
	struct objpage *page = objpage_alloc(obj);
	page->idx = idx;
	__obj_get_page_alloc_page(page);
	krc_get(&page->refs);
	rb_insert(&obj->pagecache_root, page, struct objpage, node, __objpage_compar);
	*result = page;
}

static enum obj_get_page_result __obj_get_page(struct object *obj,
  size_t addr,
  struct objpage **result,
  int flags)
{
	*result = NULL;
	spinlock_acquire_save(&obj->lock);
	struct objpage *op = __obj_get_large_page(obj, addr);
	if(op) {
		*result = op;
		spinlock_release_restore(&obj->lock);
		return GETPAGE_OK;
	}
	size_t idx = addr / mm_page_size(0);
	struct rbnode *node;
	node = rb_search(&obj->pagecache_root, idx, struct objpage, node, __objpage_compar_key);
	if(node) {
		op = rb_entry(node, struct objpage, node);
		krc_get(&op->refs);
		*result = op;

		enum obj_get_page_result res = GETPAGE_OK;
		if(op->page == NULL) {
			if(op->obj->flags & OF_PAGER) {
				if(!(flags & OBJ_GET_PAGE_PAGEROK)) {
					panic("tried to get a page from a paged object without PAGEROK");
				}
				spinlock_release_restore(&obj->lock);
				res = kernel_queue_pager_request_page(obj, idx) ? GETPAGE_NOENT : GETPAGE_NOENT;
			} else if(obj->sourced_from) {
				struct objpage *sop;
				assert(op->srcidx);
				size_t srcidx = op->srcidx;
				spinlock_release_restore(&obj->lock);

				/* TODO (minor): we can probably pass flags & ~OBJ_GET_PAGE_ALLOC  and handle that.
				 */
				res = obj_get_page(obj->sourced_from, srcidx * mm_page_size(0), &sop, flags);
				if(res == GETPAGE_OK) {
					struct objpage *oldres = *result;
					if(op->page == NULL) {
						struct rbnode *n = rb_search(&obj->idx_map,
						  op->srcidx,
						  struct objpage,
						  idx_map_node,
						  __objpage_idxmap_compar_key);

						panic("oh shit here comes dat bug :: " IDFMT " %lx :: %p\n",
						  IDPR(obj->id),
						  idx,
						  n);
					}
					assert(op->page);
					res = obj_get_page(obj, addr, result, flags);
					if(oldres) {
						objpage_release(oldres, 0);
					}
				}
				if(sop) {
					objpage_release(sop, 0);
				}
			} else if(flags & OBJ_GET_PAGE_ALLOC) {
				__obj_get_page_alloc_page(op);
				__obj_get_page_handle_derived(op);
				spinlock_release_restore(&obj->lock);
			} else {
				spinlock_release_restore(&obj->lock);
			}
		} else {
			spinlock_release_restore(&obj->lock);
		}

		return res;
	}

	if(obj->flags & OF_PAGER) {
		if(flags & OBJ_GET_PAGE_TEST) {
			spinlock_release_restore(&obj->lock);
			return GETPAGE_NOENT;
		}
		if(!(flags & OBJ_GET_PAGE_PAGEROK)) {
			panic(
			  "tried to get a page from a paged object (" IDFMT ") without PAGEROK", IDPR(obj->id));
		}

		spinlock_release_restore(&obj->lock);
		if(kernel_queue_pager_request_page(obj, idx)) {
			return GETPAGE_NOENT;
		}
		return GETPAGE_PAGER;
	}

	if(!(flags & OBJ_GET_PAGE_ALLOC)) {
		spinlock_release_restore(&obj->lock);
		return GETPAGE_NOENT;
	}

	__obj_get_page_alloc(obj, idx, result);
	__obj_get_page_handle_derived(*result);
	spinlock_release_restore(&obj->lock);
	return GETPAGE_OK;
}

enum obj_get_page_result obj_get_page(struct object *obj,
  size_t addr,
  struct objpage **result,
  int flags)
{
	enum obj_get_page_result r = __obj_get_page(obj, addr, result, flags);
	return r;
}

void obj_cache_page(struct object *obj, size_t addr, struct page *p)
{
	if(addr & (mm_page_size(p->level) - 1))
		panic("cannot map page level %d to %lx\n", p->level, addr);
	size_t idx = addr / mm_page_size(p->level);
	struct rbroot *root = p->level ? &obj->pagecache_level1_root : &obj->pagecache_root;

	spinlock_acquire_save(&obj->lock);
	struct rbnode *node = rb_search(root, idx, struct objpage, node, __objpage_compar_key);
	struct objpage *page;
	/* TODO (major): deal with overwrites? */
	if(node == NULL) {
		page = objpage_alloc(obj);
		page->idx = idx;
	} else {
		page = rb_entry(node, struct objpage, node);
	}
	page->page = p;
	if(p->cowcount == 0) {
		p->cowcount = 1;
	}
	page->flags &= ~OBJPAGE_MAPPED;
	if(p->cowcount > 1) {
		page->flags |= OBJPAGE_COW;
	}
	if(node == NULL) {
		krc_get(&page->refs);
		rb_insert(root, page, struct objpage, node, __objpage_compar);
	}
	__obj_get_page_handle_derived(page);
	spinlock_release_restore(&obj->lock);
}

void objpage_do_cow_write(struct objpage *p)
{
	/* NOTE: we probably don't need to handle derivations here. Because,
	 * 1) if we get here then the page is fully allocated, which means that any derivations that
	 *    happen after this would derive immediately, and any that were in the past would have to
	 *    have been resolved when this page was assigned.
	 *
	 * 2) If this page isn't fully allocated, we
	 *    would never get here. */
	struct page *np = page_alloc(p->page->type, 0, p->page->level);
	assert(np->level == p->page->level);
	np->cowcount = 1;
	void *cs = mm_ptov_try(p->page->addr);
	void *cd = mm_ptov_try(np->addr);

	bool src_fast = !!cs;
	bool dest_fast = !!cd;

	if(!cs) {
		cs = tmpmap_map_page(p->page);
	}
	if(!cd) {
		cd = tmpmap_map_page(np);
	}

	memcpy(cd, cs, mm_page_size(p->page->level));

	if(!dest_fast) {
		tmpmap_unmap_page(cd);
	}
	if(!src_fast) {
		tmpmap_unmap_page(cs);
	}

	assert(np->cowcount == 1);

	p->page = np;
}
