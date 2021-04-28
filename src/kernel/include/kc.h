#pragma once

#include <twz/objid.h>

extern objid_t kc_init_id, kc_bsv_id, kc_inithr_id, kc_name_id;

void kc_parse(char *data, size_t len);
bool objid_parse(const char *name, objid_t *id);

extern char *kc_data;
extern size_t kc_len;
