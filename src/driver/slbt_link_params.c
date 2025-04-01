/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2025  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include <slibtool/slibtool.h>
#include "slibtool_version.h"
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_visibility_impl.h"


slbt_hidden int slbt_init_link_params(struct slbt_driver_ctx_impl * ctx)
{
	const char * program;
	const char * libname;
	const char * prefix;
	const char * base;
	char *       dot;
	bool         fmodule;
	int          fderr;

	fderr   = ctx->fdctx.fderr;
	program = slbt_program_name(ctx->cctx.targv[0]);
	libname = 0;
	prefix  = 0;
	fmodule = false;

	/* output */
	if (!(ctx->cctx.output)) {
		if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			slbt_dprintf(fderr,
				"%s: error: output file must be "
				"specified in link mode.\n",
				program);
		return -1;
	}

	/* executable? */
	if (!(dot = strrchr(ctx->cctx.output,'.')))
		if (!(ctx->cctx.drvflags & SLBT_DRIVER_MODULE))
			return 0;

	/* todo: archive? library? wrapper? inlined function, avoid repetition */
	if ((base = strrchr(ctx->cctx.output,'/')))
		base++;
	else
		base = ctx->cctx.output;

	/* archive? */
	if (dot && !strcmp(dot,ctx->cctx.settings.arsuffix)) {
		prefix = ctx->cctx.settings.arprefix;

		if (!strncmp(prefix,base,strlen(prefix)))
			libname = base;
		else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				slbt_dprintf(fderr,
					"%s: error: output file prefix does "
					"not match its (archive) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	}

	/* library? */
	else if (dot && !strcmp(dot,ctx->cctx.settings.dsosuffix)) {
		prefix = ctx->cctx.settings.dsoprefix;

		if (!strncmp(prefix,base,strlen(prefix))) {
			libname = base;

		} else if (ctx->cctx.drvflags & SLBT_DRIVER_MODULE) {
			libname = base;
			fmodule = true;

		} else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				slbt_dprintf(fderr,
					"%s: error: output file prefix does "
					"not match its (shared library) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	}

	/* wrapper? */
	else if (dot && !strcmp(dot,".la")) {
		prefix = ctx->cctx.settings.dsoprefix;

		if (!strncmp(prefix,base,strlen(prefix))) {
			libname = base;
			fmodule = !!(ctx->cctx.drvflags & SLBT_DRIVER_MODULE);
		} else if (ctx->cctx.drvflags & SLBT_DRIVER_MODULE) {
			libname = base;
			fmodule = true;
		} else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				slbt_dprintf(fderr,
					"%s: error: output file prefix does "
					"not match its (libtool wrapper) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	} else
		return 0;

	/* libname alloc */
	if (!fmodule)
		libname += strlen(prefix);

	if (!(ctx->libname = strdup(libname)))
		return -1;

	if ((dot  = strrchr(ctx->libname,'.')))
		*dot = 0;

	ctx->cctx.libname = ctx->libname;

	return 0;
}
