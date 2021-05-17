#include <kalloc.h>
#include <nvdimm.h>
#include <object.h>
#include <page.h>
#include <processor.h>
#include <rand.h>
#include <slots.h>
#include <syscall.h>
#include <twz/meta.h>
#include <twz/sys/sctx.h>

static bool __do_invalidate(struct object *obj, struct kso_invl_args *invl)
{
	invl->result = KSO_INVL_RES_ERR;
	if(obj->kso_type != KSO_NONE && obj->kso_calls->invl) {
		return obj->kso_calls->invl(obj, invl);
	}
	return false;
}

long syscall_invalidate_kso(struct kso_invl_args *invl, size_t count)
{
	size_t suc = 0;
	if(!verify_user_pointer(invl, count * sizeof(*invl)))
		return -EINVAL;
	for(size_t i = 0; i < count; i++) {
		struct kso_invl_args ko;
		memcpy(&ko, &invl[i], sizeof(ko));
		if(ko.flags & KSOI_VALID) {
			struct object *o = NULL;
			if(ko.flags & KSOI_CURRENT) {
				switch(ko.id) {
					case KSO_CURRENT_VIEW:
						o = current_thread->ctx->viewobj;
						krc_get(&o->refs);
						break;
					default:
						ko.result = -EINVAL;
						break;
				}
			} else {
				o = obj_lookup(ko.id, 0);
			}
			if(o && __do_invalidate(o, &ko)) {
				suc++;
			}
			if(o) {
				obj_put(o);
			}
		}
	}
	return suc;
}

#include <lib/iter.h>
#include <lib/list.h>

long syscall_ostat(uint64_t idlo, uint64_t idhi, int stat_type, uint64_t arg, void *p)
{
	objid_t id = MKID(idhi, idlo);

#if 0
	struct object *obj = obj_lookup(id, OBJ_LOOKUP_HIDDEN);
	if(!obj) {
		return -ENOENT;
	}
	int ret = 0;
	switch(stat_type) {
		case OS_TYPE_OBJ: {
			struct kernel_ostat *os = p;
			if(!verify_user_pointer(os, sizeof(*os))) {
				ret = -EINVAL;
				break;
			}
			os->flags = 0;
			os->flags |= (obj->flags & OF_PINNED) ? OS_FLAGS_PIN : 0;
			os->flags |= (obj->flags & OF_KERNEL) ? OS_FLAGS_KERNEL : 0;
			os->flags |= (obj->flags & OF_PERSIST) ? OS_FLAGS_PERSIST : 0;
			os->flags |= (obj->flags & OF_HIDDEN) ? OS_FLAGS_HIDDEN : 0;
			os->flags |= (obj->flags & OF_PAGER) ? OS_FLAGS_PAGER : 0;
			os->flags |= (obj->flags & OF_ALLOC) ? OS_FLAGS_ALLOC : 0;
			if(obj->sourced_from) {
				os->flags = OS_FLAGS_SOURCED;
			}

			os->cache_mode = obj->cache_mode;
			os->kso_type = obj->kso_type;
			spinlock_acquire_save(&obj->sleepers_lock);
			size_t c = 0;
			foreach(e, list, &obj->sleepers) {
				c++;
			}
			spinlock_release_restore(&obj->sleepers_lock);
			os->nr_sleepers = c;
			c = 0;
			spinlock_acquire_save(&obj->lock);
			foreach(e, list, &obj->derivations) {
				c++;
			}
			spinlock_release_restore(&obj->lock);
			os->nr_derivations = c;
			if(obj->preg) {
				os->nvreg = obj->preg->mono_id;
			} else {
				os->nvreg = 0;
			}
		} break;
		case OS_TYPE_PAGE: {
			struct kernel_ostat_page *os = p;
			if(!verify_user_pointer(os, sizeof(*os))) {
				ret = -EINVAL;
				break;
			}

			struct objpage *opage = NULL;
			enum obj_get_page_result gpr =
			  obj_get_page(obj, (arg * mm_page_size(0)) % OBJ_MAXSIZE, &opage, OBJ_GET_PAGE_TEST);
			assert(gpr == GETPAGE_OK || gpr == GETPAGE_NOENT);
			if(gpr == GETPAGE_OK) {
				os->flags = OS_PAGE_EXIST;
				os->flags |= (opage->flags & OBJPAGE_MAPPED) ? OS_PAGE_MAPPED : 0;
				os->flags |= (opage->flags & OBJPAGE_COW) ? OS_PAGE_COW : 0;
				os->pgnr = opage->idx;
				if(opage->page) {
					os->level = opage->page->level;
					os->cowcount = opage->page->cowcount;
				}
				objpage_release(opage, 0);
			} else if(gpr == GETPAGE_NOENT) {
				os->flags = 0;
			}
		} break;
	}
	obj_put(obj);
#endif
	panic("A");

	return 0;
}

long syscall_ocopy(objid_t *destid,
  objid_t *srcid,
  size_t doff,
  size_t soff,
  size_t len,
  int flags __unused)
{
	/* TODO: check permissions */
	// printk("OCOPY: --> %lx %lx %lx\n", doff, soff, len);
	if(doff & (mm_page_size(0) - 1))
		return -EINVAL;
	if(soff & (mm_page_size(0) - 1))
		return -EINVAL;
	if(len & (mm_page_size(0) - 1))
		return -EINVAL;

	if(!verify_user_pointer(destid, sizeof(*destid)))
		return -EINVAL;
	if(!verify_user_pointer(srcid, sizeof(*srcid)))
		return -EINVAL;

	struct object *src = *srcid ? obj_lookup(*srcid, 0) : NULL;
	struct object *dest = obj_lookup(*destid, 0);
	if(!dest) {
		if(src)
			obj_put(src);
		if(dest)
			obj_put(dest);
		return -ENOENT;
	}

	// int r = obj_copy_pages(dest, src, doff, soff, len);
	int r = -ENOSYS;
	panic("A");

	obj_put(dest);
	if(src)
		obj_put(src);

	return r;
}

long syscall_otie(uint64_t pidlo, uint64_t pidhi, uint64_t cidlo, uint64_t cidhi, int flags)
{
	objid_t pid = MKID(pidhi, pidlo);
	objid_t cid = MKID(cidhi, cidlo);

	if(pid == cid)
		return -EPERM;

	int ret = 0;
	struct object *parent = obj_lookup(pid, 0);
	struct object *child = obj_lookup(cid, 0);

	if(!parent || !child)
		goto done;

	if(flags & OTIE_UNTIE) {
#if CONFIG_DEBUG_OBJECT_LIFE
		printk("UNTIE: " IDFMT " -> " IDFMT "\n", IDPR(cid), IDPR(pid));
#endif
		ret = obj_untie(parent, child);
	} else {
#if CONFIG_DEBUG_OBJECT_LIFE
		if(parent->kso_type != KSO_NONE)
			printk("TIE: " IDFMT " -> " IDFMT " (%d)\n", IDPR(cid), IDPR(pid), parent->kso_type);
		else
			printk("TIE: " IDFMT " -> " IDFMT "\n", IDPR(cid), IDPR(pid));
#endif
		obj_tie(parent, child);
	}

done:
	if(parent)
		obj_put(parent);
	if(child)
		obj_put(child);

	return ret;
}

long syscall_vmap(const void *restrict p, int cmd, long arg)
{
	panic("A");
#if 0
	switch(cmd) {
		case TWZ_SYS_VMAP_WIRE:
			(void)arg;
			return vm_context_wire(p);
			break;
		default:
			return -EINVAL;
	}
	return 0;
#endif
}

long syscall_kaction(size_t count, struct sys_kaction_args *args)
{
	size_t t = 0;
	if(count > 4096 || !args)
		return -EINVAL;

	if(!verify_user_pointer(args, sizeof(*args) * count))
		return -EINVAL;
	for(size_t i = 0; i < count; i++) {
		if(args[i].flags & KACTION_VALID) {
			struct object *obj = obj_lookup(args->id, 0);
			if(!obj) {
				args[i].result = -ENOENT;
				continue;
			}
			if(obj->kaction) {
				args[i].result = obj->kaction(obj, args[i].cmd, args[i].arg);
				t++;
			}
			obj_put(obj);
		}
	}
	return t;
}

long syscall_attach(uint64_t palo, uint64_t pahi, uint64_t chlo, uint64_t chhi, uint64_t ft)
{
	objid_t paid = MKID(pahi, palo), chid = MKID(chhi, chlo);
	uint16_t type = (ft & 0xffff);
	uint32_t flags = (ft >> 32) & 0xffffffff;
	struct object *parent;
	if(paid == 0) {
		krc_get(&current_thread->reprobj->refs);
		parent = current_thread->reprobj;
	} else {
		parent = obj_lookup(paid, 0);
	}
	struct object *child = obj_lookup(chid, 0);

	if(!parent || !child) {
		if(child)
			obj_put(child);
		if(parent)
			obj_put(parent);
		return -ENOENT;
	}

	int e;
	if(paid && (e = obj_check_permission(parent, SCP_USE | SCP_WRITE)) != 0) {
		obj_put(child);
		obj_put(parent);
		return e;
	}

	if((e = obj_check_permission(child, SCP_USE)) != 0) {
		obj_put(child);
		obj_put(parent);
		return e;
	}

	spinlock_acquire_save(&child->lock);
	/* don't need lock on parent since kso_type is atomic, and once set cannot be unset */
	if(parent->kso_type == KSO_NONE || (child->kso_type != KSO_NONE && child->kso_type != type)) {
		spinlock_release_restore(&child->lock);
		obj_put(child);
		obj_put(parent);
		return -EINVAL;
	}
	if(child->kso_type == KSO_NONE) {
		obj_kso_init(child, type);
	}
	spinlock_release_restore(&child->lock);

	int ret = -EINVAL;
	if(child->kso_calls && child->kso_calls->attach) {
		ret = child->kso_calls->attach(parent, child, flags) ? 0 : -EINVAL;
	}

	obj_put(child);
	obj_put(parent);
	return ret;
}

long syscall_detach(uint64_t palo, uint64_t pahi, uint64_t chlo, uint64_t chhi, uint64_t ft)
{
	uint16_t type = ft & 0xffff;
	uint16_t sysc = (ft >> 16) & 0xffff;
	uint32_t flags = (ft >> 32) & 0xffffffff;

	if(type >= KSO_MAX)
		return -EINVAL;

	objid_t paid = MKID(pahi, palo), chid = MKID(chhi, chlo);
	struct object *parent;
	if(paid == 0) {
		krc_get(&current_thread->reprobj->refs);
		parent = current_thread->reprobj;
	} else {
		parent = obj_lookup(paid, 0);
	}
	struct object *child = chid == 0 ? NULL : obj_lookup(chid, 0);

	if(!parent) {
		if(child)
			obj_put(child);
		return -ENOENT;
	}

	int e;
	if(paid && (e = obj_check_permission(parent, SCP_USE | SCP_WRITE)) != 0) {
		if(child)
			obj_put(child);
		obj_put(parent);
		return e;
	}

	if(child && (e = obj_check_permission(child, SCP_USE)) != 0) {
		if(child)
			obj_put(child);
		obj_put(parent);
		return e;
	}

	if(parent->kso_type == KSO_NONE || (child && child->kso_type == KSO_NONE)
	   || (child && child->kso_type != type && type != KSO_NONE)) {
		if(child)
			obj_put(child);
		obj_put(parent);
		return -EINVAL;
	}

	int ret = -EINVAL;
	if(sysc == SYS_NULL)
		sysc = SYS_DETACH;
	if(child && child->kso_calls && child->kso_calls->detach) {
		ret = child->kso_calls->detach(parent, child, sysc, flags) ? 0 : -EINVAL;
	} else if(!child) {
		/*
		struct kso_calls *kc = kso_lookup_calls(type);
		bool (*c)(struct object *, struct object *, int, int) = kc ? kc->detach : NULL;
		if(c) {
		    ret = c(parent, child, sysc, flags) ? 0 : -EINVAL;
		}*/
		panic("A");
	}

	if(child)
		obj_put(child);
	obj_put(parent);
	return ret;
}

long syscall_ocreate(uint64_t kulo,
  uint64_t kuhi,
  uint64_t slo,
  uint64_t shi,
  uint64_t flags,
  objid_t *retid)
{
	objid_t kuid = MKID(kuhi, kulo);
	objid_t srcid = MKID(shi, slo);
	nonce_t nonce = 0;

	if(!verify_user_pointer(retid, sizeof(*retid)) && current_thread
	   && arch_thread_syscall_num() == SYS_OCREATE)
		return -EINVAL;
	int r;
	if(!(flags & TWZ_SYS_OC_ZERONONCE)) {
		r = rand_getbytes(&nonce, sizeof(nonce), 0);
		if(r < 0) {
			return r;
		}
	}
	int ksot = (flags >> 8) & 0xF;
	if(ksot >= KSO_MAX) {
		return -EINVAL;
	}
	struct object *o, *so = NULL;
	if(srcid && (flags & TWZ_SYS_OC_PERSIST_)) {
		return -ENOTSUP;
	}
	if(srcid) {
		panic("A");
#if 0
		so = obj_lookup(srcid, 0);
		if(!so) {
			return -ENOENT;
		}
		if((r = obj_check_permission(so, SCP_READ))) {
			obj_put(so);
			return r;
		}
		spinlock_acquire_save(&so->lock);
		o = obj_create_clone(0, so, ksot);
#endif
	} else {
		o = obj_create(0, ksot);
	}

#if 0
	if(flags & TWZ_SYS_OC_PERSIST_) {
		o->flags |= OF_PERSIST;
		o->preg = nv_region_select();
		if(!o->preg) {
			/* TODO: cleanup */
			return -ENOSPC;
		}
	}
#endif

	struct metainfo mi = {
		.magic = MI_MAGIC,
		.p_flags =
		  flags & (MIP_HASHDATA | MIP_DFL_READ | MIP_DFL_WRITE | MIP_DFL_EXEC | MIP_DFL_USE),
		.flags = flags,
		.milen = sizeof(mi) + 128,
		.kuid = kuid,
		.nonce = nonce,
		.fotentries = 0,
	};

	o->cached_pflags = mi.p_flags;
	o->flags |= OF_CPF_VALID;
	obj_write_data(o, OBJ_MAXSIZE - (OBJ_NULLPAGE_SIZE + OBJ_METAPAGE_SIZE), sizeof(mi), &mi);

	objid_t id = obj_compute_id(o);
	obj_assign_id(o, id);

#if 0
	if(o->flags & OF_PERSIST) {
		nv_region_persist_obj_meta(o);
	}
#endif

#if CONFIG_DEBUG_OBJECT_LIFE
	if(srcid)
		printk("CREATE OBJECT: " IDFMT " from srcobj " IDFMT "\n", IDPR(id), IDPR(srcid));
	else
		printk("CREATE OBJECT: " IDFMT "\n", IDPR(id));
#endif

	if(srcid) {
		panic("A");
		/*
		//struct derivation_info *di = kalloc(sizeof(*di));

		//	printk("alloced derivation: %p\n", di);
		//	spinlock_acquire_save(&so->lock);
		krc_get(&so->refs);
		o->sourced_from = so;
		di->id = id;
		list_insert(&so->derivations, &di->entry);
		spinlock_release_restore(&so->lock);

		obj_put(so);
		*/
	}

	obj_put(o);

	if(retid)
		*retid = id;
	return 0;
}

long syscall_odelete(uint64_t olo, uint64_t ohi, uint64_t flags)
{
	objid_t id = MKID(ohi, olo);
	struct object *obj = obj_lookup(id, 0);
	if(!obj) {
		return -ENOENT;
	}

#if CONFIG_DEBUG_OBJECT_LIFE
	printk("DELETE OBJECT " IDFMT ": %lx\n", IDPR(id), flags);
#endif

	spinlock_acquire_save(&obj->lock);
	if(flags & TWZ_SYS_OD_IMMEDIATE) {
		/* "immediate" delete: the object will be marked for deletion, new lookups will return
		 * failure. */
		obj->flags |= OF_HIDDEN;
	}

	obj->flags |= OF_DELETE;

	spinlock_release_restore(&obj->lock);
	obj_put(obj);
	return 0;
}

long syscall_opin(uint64_t lo, uint64_t hi, uint64_t *addr, int flags)
{
	panic("A");
#if 0
	objid_t id = MKID(hi, lo);
	struct object *o = obj_lookup(id, 0);
	if(!o)
		return -ENOENT;

	if(flags & OP_UNPIN) {
		o->flags &= ~OF_PINNED;
	} else {
		o->flags |= OF_PINNED;
		/* TODO: does this return a slot that needs to be released? */
		obj_alloc_slot(o);
		assert(o->slot != NULL);
		if(addr)
			*addr = o->slot->num * OBJ_MAXSIZE;
	}
	obj_put(o);
	return 0;
#endif
}

#include <device.h>
#include <page.h>
long syscall_octl(uint64_t lo, uint64_t hi, int op, long arg1, long arg2, long arg3)
{
#if 0
	objid_t id = MKID(hi, lo);
	struct object *o = obj_lookup(id, 0);
	if(!o)
		return -ENOENT;

	int r = 0;
	switch(op) {
		size_t pnb, pne;
		case OCO_CACHE_MODE:
			o->cache_mode = arg3;
			arg1 += OBJ_NULLPAGE_SIZE;
			pnb = arg1 / mm_page_size(0);
			pne = (arg1 + arg2) / mm_page_size(0);
			/* TODO: bounds check */
			for(size_t i = pnb; i < pne; i++) {
				struct objpage *pg;
				obj_get_page(o, i * mm_page_size(0), &pg, OBJ_GET_PAGE_ALLOC);
				/* TODO: locking */
				pg->page->flags |= flag_if_notzero(arg3 & OC_CM_UC, PAGE_CACHE_UC);
				pg->page->flags |= flag_if_notzero(arg3 & OC_CM_WB, PAGE_CACHE_WB);
				pg->page->flags |= flag_if_notzero(arg3 & OC_CM_WT, PAGE_CACHE_WT);
				pg->page->flags |= flag_if_notzero(arg3 & OC_CM_WC, PAGE_CACHE_WC);
				arch_object_map_page(o, pg->idx, pg->page, 0);
				objpage_release(pg, 0);
			}
			break;
		case OCO_MAP:
			arg1 += OBJ_NULLPAGE_SIZE;
			pnb = arg1 / mm_page_size(0);
			pne = (arg1 + arg2) / mm_page_size(0);

			/* TODO: bounds check */
			for(size_t i = pnb; i < pne; i++) {
				struct objpage *pg;
				obj_get_page(o, i * mm_page_size(0), &pg, OBJ_GET_PAGE_ALLOC);
				/* TODO: locking */
				arch_object_map_page(o, pg->idx, pg->page, 0);
				if(arg3)
					arch_object_map_flush(o, i * mm_page_size(0));
				// objpage_release(pg, 0);
			}
			if(arg3) {
				if(!verify_user_pointer((void *)arg3, sizeof(objid_t))) {
					obj_put(o);
					return -EINVAL;
				}
				objid_t doid = *(objid_t *)arg3;
				struct object *dobj = obj_lookup(doid, 0);
				if(!dobj) {
					obj_put(o);
					return -ENOENT;
				}
				if(dobj->kso_type != KSO_DEVICE) {
					obj_put(o);
					return -EINVAL;
				}
				/* TODO: actually lookup the device */
				iommu_object_map_slot(dobj->data, o);
				obj_put(dobj);
			}

			break;
		default:
			r = -EINVAL;
	}

	obj_put(o);
	return r;
#endif
	panic("A");
}
