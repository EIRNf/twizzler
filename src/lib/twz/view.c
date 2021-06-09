#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <twz/obj.h>
#include <twz/objid.h>
#include <twz/sys/slots.h>
#include <twz/sys/sys.h>
#include <twz/sys/thread.h>
#include <twz/sys/view.h>

#include <twz/_err.h>
#include <twz/_types.h>

#include <twz/debug.h>

#include <twz.h>

void twz_view_object_init(twzobj *obj)
{
	twz_object_init_ptr(obj, SLOT_TO_VADDR(TWZSLOT_CVIEW));
}

void twz_view_set(twzobj *obj, size_t slot, objid_t target, uint32_t flags)
{
	if(slot > TWZSLOT_MAX_SLOT) {
		libtwz_panic("slot number too large: %ld", slot);
	}
	struct viewentry *ves = obj ? ((struct twzview_repr *)twz_object_base(obj))->ves
	                            : ((struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW))->ves;

	uint32_t old = atomic_fetch_and(&ves[slot].flags, ~VE_VALID);
	ves[slot].id = target;
	ves[slot].res0 = 0;
	ves[slot].res1 = 0;
	if(flags & VE_WRITE) {
		flags &= ~VE_EXEC;
	}
	atomic_store(&ves[slot].flags, flags | VE_VALID);

#if 0
	twzobj vo;
	twz_view_object_init(&vo);
	debug_printf("mapping in %p " IDFMT " --> %p (%x) (old = %x) :: %p %x in " IDFMT "\n",
	  obj,
	  IDPR(target),
	  SLOT_TO_VADDR(slot),
	  flags,
	  old,
	  &ves[slot],
	  ves[slot].flags,
	  IDPR(twz_object_guid(&vo)));
#endif
	if((old & VE_VALID)) {
		debug_printf("invalidating %p\n", obj);
		struct sys_invalidate_op op = {
			.offset = (long)SLOT_TO_VADDR(slot),
			.length = OBJ_MAXSIZE,
			.flags = KSOI_VALID | KSOI_CURRENT,
			.id = KSO_CURRENT_VIEW,
		};
		if(obj) {
			op.id = twz_object_guid(obj);
			op.flags &= ~KSOI_CURRENT;
		}
		if(sys_invalidate(&op, 1) < 0) {
			libtwz_panic("failed to invalidate view entry");
		}
	}
}

void twz_view_fixedset(twzobj *obj, size_t slot, objid_t target, uint32_t flags)
{
	if(slot > TWZSLOT_MAX_SLOT) {
		libtwz_panic("slot number too large (fixed): %ld", slot);
	}
	flags &= ~VE_FIXED;
	struct viewentry *ves = obj ? ((struct twzthread_ctrl_repr *)twz_object_base(obj))->fixed_points
	                            : (struct viewentry *)twz_thread_ctrl_repr_base()->fixed_points;
	uint32_t old = atomic_fetch_and(&ves[slot].flags, ~VE_VALID);
	ves[slot].id = target;
	ves[slot].res0 = 0;
	ves[slot].res1 = 0;
	if(flags & VE_WRITE) {
		flags &= ~VE_EXEC;
	}
	atomic_store(&ves[slot].flags, flags | VE_VALID);

	if(old & VE_VALID) {
		struct sys_invalidate_op op = {
			.offset = (long)SLOT_TO_VADDR(slot),
			.length = 1,
			.flags = KSOI_VALID | KSOI_CURRENT,
			.id = KSO_CURRENT_VIEW,
		};
		if(obj) {
			op.id = twz_object_guid(obj);
			op.flags &= ~KSOI_CURRENT;
		}
		if(sys_invalidate(&op, 1) < 0) {
			libtwz_panic("failed to invalidate view entry");
		}
	}
}

void twz_view_get(twzobj *obj, size_t slot, objid_t *target, uint32_t *flags)
{
	if(slot > TWZSLOT_MAX_SLOT) {
		libtwz_panic("slot number too large (get): %ld (%p)", slot, __builtin_return_address(1));
	}
	struct viewentry *ves;
	uint32_t extra_flags = 0;
	if(obj) {
		ves = &(((struct twzview_repr *)twz_object_base(obj))->ves[slot]);
	} else {
		struct twzthread_ctrl_repr *tr = twz_thread_ctrl_repr_base();
		ves = &tr->fixed_points[slot];
		extra_flags |= VE_FIXED;
		if(!(ves->flags & VE_VALID)) {
			ves = &(((struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW))->ves[slot]);
			extra_flags = 0;
		}
	}
	if(flags)
		*flags = atomic_load(&ves->flags) | extra_flags;
	if(target)
		*target = ves->id;
}

int twz_vaddr_to_obj(const void *v, objid_t *id, uint32_t *fl)
{
	uint32_t tf;
	twz_view_get(NULL, VADDR_TO_SLOT(v), id, &tf);
	if(!(tf & VE_VALID))
		return -ENOENT;
	if(fl)
		*fl = tf;
	return 0;
}

static struct __viewrepr_bucket *__lookup_bucket(struct twzview_repr *v, objid_t id, uint32_t flags)
{
	int32_t idx = id % (TWZSLOT_MAX_SLOT + 1);
	struct __viewrepr_bucket *b = v->buckets;
	do {
		// if(b[idx].id == 0)
		//	return NULL;
		if(b[idx].id == id && b[idx].flags == flags) {
			return &b[idx];
		}
		idx = b[idx].chain - 1;
		b = v->chain;
	} while(idx != -1);
	return NULL;
}

static struct __viewrepr_bucket *__insert_obj(struct twzview_repr *v,
  objid_t id,
  uint32_t flags,
  size_t slot)
{
	int32_t idx = id % (TWZSLOT_MAX_SLOT + 1);
	struct __viewrepr_bucket *b = v->buckets;
	struct __viewrepr_bucket *pb = NULL;
	do {
		if(b[idx].id == 0) {
			b[idx].id = id;
			b[idx].slot = slot;
			b[idx].flags = flags;
			b[idx].refs = 1;
			return &b[idx];
		}
		pb = &b[idx];
		idx = b[idx].chain - 1;
		b = v->chain;
	} while(idx != -1);
	for(size_t i = 0; i <= TWZSLOT_MAX_SLOT; i++) {
		if(b[i].id == 0 && b[i].chain == 0) {
			b[i].id = id;
			b[i].flags = flags;
			b[i].slot = slot;
			b[i].refs = 1;
			b[i].chain = 0;
			pb->chain = i + 1;
			return &b[i];
		}
	}
	return NULL;
}

static ssize_t __alloc_slot(struct twzview_repr *v)
{
	for(size_t i = TWZSLOT_ALLOC_START / 8; i <= TWZSLOT_ALLOC_MAX / 8; i++) {
		size_t idx = i;
		if(v->bitmap[idx] != 0xff) {
			for(int bit = 0; bit < 8; bit++) {
				if(!(v->bitmap[idx] & (1 << bit))) {
					v->bitmap[idx] |= (1 << bit);
					return idx * 8 + bit;
				}
			}
		}
	}
	return -ENOSPC;
}

static void __dealloc_slot(struct twzview_repr *v, size_t slot)
{
	v->bitmap[slot / 8] &= ~(1 << (slot % 8));
}

#include <stdio.h>
int twz_view_clone(twzobj *old,
  twzobj *nobj,
  int flags,
  bool (*fn)(twzobj *, size_t, objid_t, uint32_t, objid_t *, uint32_t *))
{
	struct twzview_repr *oldv = old ? (struct twzview_repr *)twz_object_base(old)
	                                : (struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW);
	struct twzview_repr *newv = (struct twzview_repr *)twz_object_base(nobj);

	mutex_acquire(&oldv->lock);

	if(VIEW_CLONE_BITMAP) {
		memcpy(newv->bitmap, oldv->bitmap, sizeof(newv->bitmap));
	}

	for(size_t i = 0; i < TWZSLOT_MAX_SLOT + 1; i++) {
		if(flags & VIEW_CLONE_ENTRIES) {
			struct viewentry *ove = &oldv->ves[i];
			struct viewentry *nve = &newv->ves[i];
			if(ove->flags & VE_VALID) {
				if(!fn(nobj, i, ove->id, ove->flags, &nve->id, (uint32_t *)&nve->flags)) {
					nve->id = 0;
					nve->flags = 0;
				}
			}
		}

		struct __viewrepr_bucket *nb = &newv->buckets[i];
		struct __viewrepr_bucket *ob = &oldv->buckets[i];
		if(ob->id == 0)
			continue;
		*nb = *ob;
		if(!fn(nobj, ob->slot, ob->id, ob->flags, &nb->id, &nb->flags)) {
			nb->refs = 0;
			nb->id = 0;
			nb->flags = 0;
		} else {
			newv->ves[ob->slot].id = nb->id;
			newv->ves[ob->slot].flags = nb->flags | VE_VALID;
		}
	}

	/*
	for(size_t i = 0; i < TWZSLOT_MAX_SLOT + 1; i++) {
	    struct __viewrepr_bucket *nb = &newv->buckets[i];
	    struct __viewrepr_bucket *ob = &oldv->buckets[i];
	    if(ob->id == 0)
	        continue;
	    *nb = *ob;
	    if(!fn(nobj, ob->slot, ob->id, ob->flags, &nb->id, &nb->flags)) {
	        nb->refs = 0;
	        nb->id = 0;
	        nb->flags = 0;
	    } else {
	        newv->ves[ob->slot].id = nb->id;
	        newv->ves[ob->slot].flags = nb->flags | VE_VALID;
	    }
	}*/

	mutex_release(&oldv->lock);

	return 0;
}

#include <assert.h>
void twz_view_release_slot(twzobj *obj, objid_t id, uint32_t flags, size_t slot)
{
	struct twzview_repr *v = obj ? (struct twzview_repr *)twz_object_base(obj)
	                             : (struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW);

	mutex_acquire(&v->lock);
	struct __viewrepr_bucket *b = __lookup_bucket(v, id, flags);
	assert(b);
	assert(b->slot == slot);

	// debug_printf("TWZ RELEASE SLOT: " IDFMT " refs=%d\n", IDPR(id), b->refs);
	int old = b->refs--;
	assert(old > 0);
	if(old == 1) {
		twz_view_set(obj, b->slot, 0, 0);
		__dealloc_slot(v, b->slot);
		b->slot = 0;
		b->id = 0;
		b->flags = 0;
	}

	mutex_release(&v->lock);
}

ssize_t twz_view_allocate_slot(twzobj *obj, objid_t id, uint32_t flags)
{
	struct twzview_repr *v = obj ? (struct twzview_repr *)twz_object_base(obj)
	                             : (struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW);

	mutex_acquire(&v->lock);

	struct __viewrepr_bucket *b = __lookup_bucket(v, id, flags);
	ssize_t slot;
	if(b) {
		slot = b->slot;
		assert(b->refs > 0);
		b->refs++;
	} else {
		slot = __alloc_slot(v);
		if(slot < 0) {
			mutex_release(&v->lock);
			return slot;
		}
		b = __insert_obj(v, id, flags, slot);
		twz_view_set(obj, slot, id, flags);
	}

	mutex_release(&v->lock);
	return slot;
}
