/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2025  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"

int slbt_output_machine(const struct slbt_driver_ctx * dctx)
{
	const struct slbt_common_ctx *	cctx;
	int				fdout;

	cctx     = dctx->cctx;
	fdout    = slbt_driver_fdout(dctx);

	if (slbt_dprintf(fdout,"%s\n",cctx->host.host) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	return 0;
}
