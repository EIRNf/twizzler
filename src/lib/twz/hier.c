/*
 * SPDX-FileCopyrightText: 2021 Daniel Bittman <danielbittman1@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <twz/debug.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/obj/hier.h>
#include <twz/sys/name.h>

static int _recur_twz_hier_resolve_name(twzobj *ns,
  const char *path,
  int flags,
  struct twz_name_ent *ent,
  size_t count);
/* TODO: thread-safe */

static struct twz_name_ent *__get_name_ent(twzobj *ns, const char *path, size_t plen)
{
	/* note that the dlen field includes the null terminator */
	struct twz_namespace_hdr *hdr = twz_object_base(ns);
	if(hdr->magic != NAMESPACE_MAGIC) {
		return NULL;
	}
	struct twz_name_ent *ent = hdr->ents;

	while(ent->dlen) {
		// debug_printf("trying out %s (for %s : %ld)\n", ent->name, path, plen);
		if((ent->flags & NAME_ENT_VALID) && ent->dlen >= plen + 1) {
			if(!memcmp(ent->name, path, plen) && ent->name[plen] == 0) {
				/* found! */
				return ent;
			}
		}
		size_t reclen = sizeof(*ent) + ent->dlen;
		reclen = (reclen + 15) & ~15;
		ent = (struct twz_name_ent *)((char *)ent + reclen);
	}
	return NULL;
}

ssize_t twz_hier_get_entry(twzobj *ns, size_t pos, struct twz_name_ent **ent)
{
	struct twz_namespace_hdr *hdr = twz_object_base(ns);
	if(hdr->magic != NAMESPACE_MAGIC)
		return -EINVAL;

	struct metainfo *mi = twz_object_meta(ns);
	if(pos >= mi->sz) {
		return 0;
	}

	struct twz_name_ent *pt = (void *)((char *)hdr->ents + pos);
	if(pt->dlen == 0)
		return 0;
	size_t reclen = sizeof(*pt) + pt->dlen;
	reclen = (reclen + 15) & ~15;

	*ent = pt;
	return reclen;
}

int twz_hier_namespace_new(twzobj *ns, twzobj *parent, const char *name, uint64_t flags)
{
	int r;
	if((r = twz_object_new(ns, NULL, NULL, OBJ_PERSISTENT, flags | TWZ_OC_TIED_NONE))) {
		return r;
	}
	struct twz_namespace_hdr *hdr = twz_object_base(ns);

	hdr->magic = NAMESPACE_MAGIC;
	bool ok = true;
	ok = ok && (twz_hier_assign_name(ns, ".", NAME_ENT_NAMESPACE, twz_object_guid(ns)) == 0);
	if(parent) {
		ok =
		  ok && (twz_hier_assign_name(ns, "..", NAME_ENT_NAMESPACE, twz_object_guid(parent)) == 0);
		ok =
		  ok && (twz_hier_assign_name(parent, name, NAME_ENT_NAMESPACE, twz_object_guid(ns)) == 0);
	} else {
		ok = ok && (twz_hier_assign_name(ns, "..", NAME_ENT_NAMESPACE, twz_object_guid(ns)) == 0);
	}

	if(!ok) {
		if(twz_object_delete(ns, 0)) {
			libtwz_panic("failed to delete namespace object during cleanup");
		}
	}

	return ok ? 0 : -EINVAL;
}

int twz_hier_remove_name(twzobj *ns, const char *name)
{
	struct twz_namespace_hdr *hdr = twz_object_base(ns);
	if(hdr->magic != NAMESPACE_MAGIC) {
		return -EINVAL;
	}
	struct twz_name_ent *ent = hdr->ents;

	bool found = false;
	size_t len = strlen(name) + 1;
	while(ent->dlen) {
		if((ent->flags & NAME_ENT_VALID) && ent->dlen >= len) {
			if(!memcmp(ent->name, name, len) && ent->name[len] == 0) {
				ent->flags &= ~NAME_ENT_VALID;
				found = true;
			}
		}

		size_t reclen = sizeof(*ent) + ent->dlen;
		reclen = (reclen + 15) & ~15;
		ent = (struct twz_name_ent *)((char *)ent + reclen);
	}
	return found ? 0 : -ENOENT;
}

int twz_hier_assign_name(twzobj *ns, const char *name, int type, objid_t id)
{
	struct twz_namespace_hdr *hdr = twz_object_base(ns);
	if(hdr->magic != NAMESPACE_MAGIC) {
		return -EINVAL;
	}
	struct twz_name_ent *ent = hdr->ents;

	size_t len = strlen(name) + 1;
	// fprintf(stderr, ":: !! creating name %s %d\n", name, len);
	while(ent->dlen) {
		//	fprintf(stderr, ":: considering entry %s, %d %x\n", ent->name, ent->dlen, ent->flags);
		if(!(ent->flags & NAME_ENT_VALID) && ent->dlen >= len) {
			//		fprintf(stderr, ":: ok! Adding\n");
			ent->flags |= NAME_ENT_VALID;
			ent->type = type;
			ent->resv0 = 0;
			ent->resv1 = 0;
			ent->id = id;
			strcpy(ent->name, name);
			return 0;
		}
		size_t reclen = sizeof(*ent) + ent->dlen;
		reclen = (reclen + 15) & ~15;
		ent = (struct twz_name_ent *)((char *)ent + reclen);
	}
	// fprintf(stderr, ":: creating new entry\n");
	ent->dlen = len + 1;
	ent->flags = NAME_ENT_VALID;
	ent->type = type;
	ent->resv0 = 0;
	ent->resv1 = 0;
	ent->id = id;
	strcpy(ent->name, name);

	size_t reclen = sizeof(*ent) + ent->dlen;
	reclen = (reclen + 15) & ~15;
	twz_object_setsz(ns, TWZ_OSSM_RELATIVE, reclen);
	return 0;
}

int twz_hier_readlink(twzobj *ns, const char *path, char *buf, size_t bufsz)
{
	while(*path == '/')
		path++;
	if(!*path) {
		/* TODO: ? */
		return -EINVAL;
	}

	/* consume the next part of the path, and recurse if necessary. */
	char *ndl = strchr(path, '/');
	size_t elen = ndl ? (size_t)(ndl - path) : strlen(path);
	struct twz_name_ent *ne = __get_name_ent(ns, path, elen);
	if(!ne) {
		return -ENOENT;
	}
	struct twz_name_ent target;
	if(ne->type == NAME_ENT_SYMLINK && ndl) {
		int r;
		const char *symtarget = ne->name + strlen(ne->name) + 1;
		if((r = _recur_twz_hier_resolve_name(ns, symtarget, 0, &target, 0))) {
			return r;
		}
		ne = &target;
	}
	if(ndl) {
		/* not the last element of the path. Note: if the path has the form '/usr/bin/' then
		 * we actually _are_ the last element, but have no way of knowing yet. That's okay, though,
		 * we'll handle that. Firstly, a path lookup of a _file_ will fail if we append a /, so we
		 * can check that now */
		if(ne->type != NAME_ENT_NAMESPACE) {
			return -ENOTDIR;
		}
		twzobj next;
		twz_object_init_guid(&next, ne->id, FE_READ); /* TODO: release? */
		return twz_hier_readlink(&next, ndl + 1, buf, bufsz);
	}
	if(ne->type != NAME_ENT_SYMLINK) {
		return -EINVAL;
	}
	const char *symtarget = ne->name + strlen(ne->name) + 1;
	size_t len = bufsz;
	if(strlen(symtarget) < len) {
		len = strlen(symtarget);
	}
	memcpy(buf, symtarget, len);
	return len;
}

/* resolve a path starting from namespace ns. Note that this does _not_ operate
 * like a unix path resolver in some ways:
 *   - a first element '/' character does not differ from a path without one. That is,
 *     /usr and usr are both treated the same way here. In this sense, 'ns' is more like the
 *     root of the unix path system.
 */
static int __twz_hier_resolve_name(twzobj *ns,
  const char *path,
  int flags,
  struct twz_name_ent *ent,
  size_t count)
{
	while(*path == '/')
		path++;
	if(!*path) {
		/* no path to traverse; return 0, with an ID of 0, and the caller can figure it out */
		ent->id = 0;
		ent->type = NAME_ENT_REGULAR;
		return 0;
	}

	/* consume the next part of the path, and recurse if necessary. */
	char *ndl = strchr(path, '/');
	size_t elen = ndl ? (size_t)(ndl - path) : strlen(path);
	struct twz_name_ent *ne = __get_name_ent(ns, path, elen);
	if(!ne) {
		return -ENOENT;
	}
	struct twz_name_ent target;
	// debug_printf("found: %s %d\n", ne->name, ne->type);
	if(ne->type == NAME_ENT_SYMLINK && (!(flags & TWZ_HIER_SYM) || ndl)) {
		int r;
		const char *symtarget = ne->name + strlen(ne->name) + 1;
		// debug_printf("llok symta %s\n", symtarget);
		//	fprintf(stderr, "  lookup sym %s\n", symtarget);
		//		fprintf(stderr, "looking up symlink from %s -> %s\n", ne->name, symtarget);
		if(*symtarget == '/') {
			ns = twz_name_get_root();
		}
		if((r = _recur_twz_hier_resolve_name(ns, symtarget, flags, &target, count + 1))) {
			return r;
		}
		ne = &target;
	}
	if(ndl) {
		/* not the last element of the path. Note: if the path has the form '/usr/bin/' then
		 * we actually _are_ the last element, but have no way of knowing yet. That's okay, though,
		 * we'll handle that. Firstly, a path lookup of a _file_ will fail if we append a /, so we
		 * can check that now */
		if(ne->type != NAME_ENT_NAMESPACE) {
			return -ENOTDIR;
		}
		twzobj next;
		twz_object_init_guid(&next, ne->id, FE_READ); /* TODO: release? */
		int r = _recur_twz_hier_resolve_name(&next, ndl + 1, flags, ent, count);
		if(ent->id || r || ent->type == NAME_ENT_SYMLINK) {
			return r;
		}
	}
	*ent = *ne;
	ent->dlen = 0;
	return 0;
}

static int _recur_twz_hier_resolve_name(twzobj *ns,
  const char *path,
  int flags,
  struct twz_name_ent *ent,
  size_t count)
{
	if(count >= 32) {
		return -ELOOP;
	}
	int r = __twz_hier_resolve_name(ns, path, flags, ent, count);
	// debug_printf(":::::::::: :: %p %s : %d %d\n", ns, path, r, ent->type);
	// fprintf(stderr, ":: %d\n", ent->type);
	if(r == 0 && ent->id == 0 && ent->type != NAME_ENT_SYMLINK) {
		//	fprintf(stderr, "FALLBACK\n");
		return __twz_hier_resolve_name(ns, "/.", flags, ent, count);
	}
	return r;
}

int twz_hier_resolve_name(twzobj *ns, const char *path, int flags, struct twz_name_ent *ent)
{
	return _recur_twz_hier_resolve_name(ns, path, flags, ent, 0);
}
