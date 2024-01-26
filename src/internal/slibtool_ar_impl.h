#ifndef SLIBTOOL_AR_IMPL_H
#define SLIBTOOL_AR_IMPL_H

#include "argv/argv.h"
#include <slibtool/slibtool.h>
#include <slibtool/slibtool_arbits.h>

extern const struct argv_option slbt_ar_options[];

struct ar_armaps_impl {
	struct ar_raw_armap_bsd_32      armap_bsd_32;
	struct ar_raw_armap_bsd_64      armap_bsd_64;
	struct ar_raw_armap_sysv_32     armap_sysv_32;
	struct ar_raw_armap_sysv_64     armap_sysv_64;
	struct ar_meta_armap_common_32  armap_common_32;
	struct ar_meta_armap_common_64  armap_common_64;
	uint64_t                        armap_nsyms;
};

struct slbt_archive_meta_impl {
	void *                          hdrinfov;
	char *                          namestrs;
	const char *                    symstrs;
	const char **                   symstrv;
	off_t *                         offsetv;
	struct ar_meta_member_info **   memberv;
	struct ar_meta_member_info *    members;
	struct ar_armaps_impl           armaps;
	struct slbt_archive_meta        armeta;
};

static inline struct slbt_archive_meta_impl * slbt_archive_meta_ictx(const struct slbt_archive_meta * meta)
{
	uintptr_t addr;

	if (meta) {
		addr = (uintptr_t)meta - offsetof(struct slbt_archive_meta_impl,armeta);
		return (struct slbt_archive_meta_impl *)addr;
	}

	return 0;
}

#endif
