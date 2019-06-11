#include <stdio.h>
#include <twz/_err.h>
#include <twz/obj.h>
#include <twz/sys.h>
#include <twz/view.h>
int twz_object_create(int flags, objid_t kuid, objid_t src, objid_t *id)
{
	return sys_ocreate(flags, kuid, src, id);
}

int twz_object_open(struct object *obj, objid_t id, int flags)
{
	static int i = 0;
	int x = i++;

	twz_view_set(NULL, 0x100 + x, id, FE_READ | FE_WRITE); // TODO

	obj->base = (void *)(OBJ_MAXSIZE * (x + 0x100));
	return 0;
}

void *twz_object_getext(struct object *obj, uint64_t tag)
{
	struct metainfo *mi = twz_object_meta(obj);
	struct metaext *e = &mi->exts[0];

	while((char *)e < (char *)mi + mi->milen) {
		if(e->tag == tag) {
			return twz_ptr_lea(obj, e->ptr);
		}
		e++;
	}
	return NULL;
}

#include <twz/debug.h>
int twz_object_addext(struct object *obj, uint64_t tag, void *ptr)
{
	struct metainfo *mi = twz_object_meta(obj);
	struct metaext *e = &mi->exts[0];

	while((char *)e < (char *)mi + mi->milen) {
		if(e->tag == 0) {
			e->ptr = twz_ptr_local(ptr);
			e->tag = tag;
			return 0;
		}
		e++;
	}
	return -ENOSPC;
}

ssize_t twz_object_addfot(struct object *obj, objid_t id, uint64_t flags)
{
	struct metainfo *mi = twz_object_meta(obj);
	struct fotentry *fe = (void *)((char *)mi + mi->milen);
	/* TODO: large FOTs */
	for(size_t e = 1; e < 64; e++) {
		/* TODO: reuse better */
		if(fe[e].id == id && fe[e].flags == flags)
			return e;
		if(fe[e].id == 0) {
			fe[e].id = id;
			fe[e].flags = flags;
			return e;
		}
	}
	return -ENOSPC;
}

int __twz_ptr_store(struct object *obj, const void *p, uint32_t flags, const void **res)
{
	objid_t target;
	int r = twz_vaddr_to_obj(p, &target, NULL);
	if(r)
		return r;

	ssize_t fe = twz_object_addfot(obj, target, flags);
	if(fe < 0)
		return fe;

	*res = twz_ptr_rebase(fe, p);

	return 0;
}
