/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <time.h>
#include <locale.h>
#include <inttypes.h>
#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_ar_impl.h"

#define SLBT_PRETTY_FLAGS       (SLBT_PRETTY_YAML      \
	                         | SLBT_PRETTY_POSIX    \
	                         | SLBT_PRETTY_HEXDATA)

static int slbt_ar_output_symbols_posix(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * mctx,
	const struct slbt_fd_ctx *      fdctx)
{
	int             fdout;
	const char **   symv;

	fdout = fdctx->fdout;

	for (symv=mctx->symstrv; *symv; symv++)
		if (slbt_dprintf(fdout,"%s\n",*symv) < 0)
			return SLBT_SYSTEM_ERROR(dctx,0);

	return 0;
}

static int slbt_ar_output_symbols_yaml(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * mctx,
	const struct slbt_fd_ctx *      fdctx)
{
	(void)dctx;
	(void)mctx;
	(void)fdctx;

	return 0;
}

int slbt_ar_output_symbols(const struct slbt_archive_meta * meta)
{
	struct slbt_archive_meta_impl * mctx;
	const struct slbt_driver_ctx *  dctx;
	struct slbt_fd_ctx              fdctx;

	mctx = slbt_archive_meta_ictx(meta);
	dctx = (slbt_archive_meta_ictx(meta))->dctx;

	if (slbt_get_driver_fdctx(dctx,&fdctx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	if (!meta->a_memberv)
		return 0;

	switch (dctx->cctx->fmtflags & SLBT_PRETTY_FLAGS) {
		case SLBT_PRETTY_YAML:
			return slbt_ar_output_symbols_yaml(
				dctx,mctx,&fdctx);

		case SLBT_PRETTY_POSIX:
			return slbt_ar_output_symbols_posix(
				dctx,mctx,&fdctx);

		default:
			return slbt_ar_output_symbols_yaml(
				dctx,mctx,&fdctx);
	}
}
