/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2025  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_visibility_impl.h"

slbt_hidden int slbt_record_error(
	const struct slbt_driver_ctx *	dctx,
	int				esyscode,
	int				elibcode,
	const char *			efunction,
	int				eline,
	unsigned			eflags,
	void *				eany)
{
	struct slbt_driver_ctx_impl *	ictx;
	struct slbt_error_info *	erri;

	ictx = slbt_get_driver_ictx(dctx);

	if (ictx->errinfp == ictx->erricap)
		return -1;

	*ictx->errinfp = &ictx->erribuf[ictx->errinfp - ictx->erriptr];
	erri = *ictx->errinfp;

	erri->edctx     = dctx;
	erri->esyscode  = esyscode;
	erri->elibcode  = elibcode;
	erri->efunction = efunction;
	erri->eline     = eline;
	erri->eflags    = eflags;
	erri->eany      = (eany && (esyscode == ENOENT)) ? strdup(eany) : eany;

	ictx->errinfp++;

	return -1;
}
