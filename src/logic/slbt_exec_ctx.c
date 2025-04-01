/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2025  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_mkdir_impl.h"
#include "slibtool_linkcmd_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_ar_impl.h"

#define SLBT_ECTX_LIB_EXTRAS	26
#define SLBT_ECTX_SPARE_PTRS	16

static int slbt_ectx_free_exec_ctx_impl(
	struct slbt_exec_ctx_impl *	ictx,
	int				status);

static size_t slbt_parse_comma_separated_flags(
	const char *	str,
	int *		argc)
{
	const char * ch;

	for (ch=str; *ch; ch++)
		if (*ch == ',')
			(*argc)++;

	return ch - str;
}


static char * slbt_source_file(char ** argv)
{
	char **	parg;
	char *	ch;

	for (parg=argv; *parg; parg++)
		if ((ch = strrchr(*parg,'.')))
			if ((!(strcmp(++ch,"s")))
					|| (!(strcmp(ch,"S")))
					|| (!(strcmp(ch,"asm")))
					|| (!(strcmp(ch,"c")))
					|| (!(strcmp(ch,"cc")))
					|| (!(strcmp(ch,"cpp")))
					|| (!(strcmp(ch,"cxx"))))
				return *parg;
	return 0;
}


static int slbt_exec_ctx_tool_argc(char ** argv)
{
	char ** parg;

	if (!(parg = argv))
		return 0;

	for (; *parg; )
		parg++;

	return parg - argv + 1;
}


static struct slbt_exec_ctx_impl * slbt_exec_ctx_alloc(
	const struct slbt_driver_ctx *	dctx)
{
	struct slbt_driver_ctx_impl *   ctx;
	struct slbt_exec_ctx_impl *	ictx;
	size_t				size;
	size_t                          slen;
	size_t                          exts;
	int				argc;
	char *				args;
	char *				shadow;
	char *				csrc;
	char **				parg;
	struct slbt_archive_ctx **      dlactxv;
	size_t                          ndlopen;

	/* internal driver context for host-specific tool arguments */
	ctx = slbt_get_driver_ictx(dctx);

	/* initial buffer size (cargv, -Wc), .libs/.exe.wrapper */
	argc = 0;
	csrc = 0;
	size = 0;
	exts = 20;

	for (parg=dctx->cctx->cargv; *parg; parg++, argc++) {
		if (!(strncmp("-Wc,",*parg,4))) {
			size += slbt_parse_comma_separated_flags(
				&(*parg)[4],&argc) + 1;
		} else {
			size += strlen(*parg) + 1;
		}
	}

	/* argument transformation: assume for each argv member */
	if (dctx->cctx->shrext) {
		exts += argc * (strlen(dctx->cctx->shrext));
	}

	if (dctx->cctx->settings.dsosuffix) {
		exts += argc * (strlen(dctx->cctx->settings.dsosuffix));
	}

	size += argc * exts;

	/* buffer size (csrc, ldirname, lbasename, lobjname, aobjname, etc.) */
	if (dctx->cctx->libname) {
		slen  = strlen(dctx->cctx->libname);
		size += (strlen(".slibtool.expsyms.extension") + slen + exts + 1) * SLBT_ECTX_LIB_EXTRAS;

	} else if (dctx->cctx->output) {
		slen  = strlen(dctx->cctx->output);
		size += (strlen(".slibtool.expsyms.extension") + slen + exts + 1) * SLBT_ECTX_LIB_EXTRAS;

	} else if ((csrc = slbt_source_file(dctx->cctx->cargv))) {
		slen  = strlen(csrc);
		size += (strlen(".slibtool.expsyms.extension") + slen + exts + 1) * SLBT_ECTX_LIB_EXTRAS;
	} else {
		size += exts * SLBT_ECTX_LIB_EXTRAS;
	}

	/* -lsltdl (--prefer-sltdl impl.) */
	size += 8;

	/* string buffers: args, shadow */
	if (!(args = malloc(size)))
		return 0;

	if (!(shadow = malloc(size))) {
		free(args);
		return 0;
	}

	/* tool-specific argv: to simplify matters, be additive */
	argc += slbt_exec_ctx_tool_argc(ctx->host.ar_argv);
	argc += slbt_exec_ctx_tool_argc(ctx->host.as_argv);
	argc += slbt_exec_ctx_tool_argc(ctx->host.nm_argv);
	argc += slbt_exec_ctx_tool_argc(ctx->host.ranlib_argv);
	argc += slbt_exec_ctx_tool_argc(ctx->host.windres_argv);
	argc += slbt_exec_ctx_tool_argc(ctx->host.dlltool_argv);
	argc += slbt_exec_ctx_tool_argc(ctx->host.mdso_argv);

	/* argv transformation: .libs/libfoo.so --> -L.libs -lfoo */
	argc *= 2;

	/* argv ad-hoc extensions */
	argc += SLBT_ECTX_SPARE_PTRS;

	/* ctx alloc and vector alloc: argv, xargv, and altv, where we  */
	/* assume -Wl,--whole-archive arg -Wl,--no-whole-archive;      */
	/* and also dlargv for compiling dlunit.dlopen.c              */
	if (!(ictx = calloc(1,sizeof(*ictx)))) {
		free(args);
		free(shadow);
		return 0;
	}

	if (!(ictx->vbuffer = calloc(6*(argc+1),sizeof(char *)))) {
		free(args);
		free(shadow);
		free(ictx);
	}

	if ((ndlopen = (slbt_get_driver_ictx(dctx))->ndlopen)) {
		if (!(dlactxv = calloc(ndlopen+1,sizeof(*dlactxv)))) {
			free(ictx->vbuffer);
			free(ictx);
			free(args);
			free(shadow);
		}

		ictx->dlactxv = dlactxv;
	}

	/* all ready */
	ictx->dctx = dctx;
	ictx->args = args;
	ictx->argc = argc;

	ictx->size   = size;
	ictx->exts   = exts;
	ictx->shadow = shadow;

	ictx->ctx.csrc  = csrc;
	ictx->fdwrapper = (-1);

	ictx->ctx.envp  = slbt_driver_envp(dctx);

	return ictx;
}


int  slbt_ectx_get_exec_ctx(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx **		ectx)
{
	struct slbt_exec_ctx_impl *	ictx;
	struct slbt_driver_ctx_impl *   idctx;
	struct slbt_error_info**        errinfp;
	char **				parg;
	char **				src;
	char **				dst;
	char *				ch;
	char *				mark;
	const char *			dmark;
	const char *                    dsoext;
	char *				slash;
	char *                          arname;
	struct slbt_archive_ctx **      dlactxv;
	struct argv_entry *             dlentry;
	struct argv_entry **            dlopenv;
	bool                            fpreopen;
	const char *			arprefix;
	const char *			dsoprefix;
	const char *			impprefix;
	const char *			ref;
	int				i;

	/* internal driver context */
	idctx = slbt_get_driver_ictx(dctx);

	/* alloc */
	if (!(ictx = slbt_exec_ctx_alloc(dctx)))
		return SLBT_NESTED_ERROR(dctx);

	/* init with guard for later .lo check */
	ch                = ictx->args + strlen(".lo");
	ictx->ctx.argv    = ictx->vbuffer;
	ictx->ctx.xargv   = &ictx->ctx.argv [ictx->argc + 1];
	ictx->ctx.altv    = &ictx->ctx.xargv[ictx->argc + 1];
	ictx->dlargv      = &ictx->ctx.altv [ictx->argc + 1];

	/* <compiler> */
	ictx->ctx.compiler = dctx->cctx->cargv[0];
	ictx->ctx.cargv    = ictx->ctx.argv;

	/* ldirname, lbasename */
	ref = (dctx->cctx->output)
		? dctx->cctx->output
		: ictx->ctx.csrc;

	if (ref && !ictx->ctx.csrc && (mark = strrchr(ref,'/'))) {
		if (!(strncmp(ref,"../",3)))
			dmark = 0;
		else if (!(strncmp(ref,"./",2)))
			dmark = &ref[1];
		else
			dmark = strchr(ref,'/');

		for (; dmark; ) {
			if (!(strncmp(dmark,"/./",3))) {
				dmark = strchr(&dmark[2],'/');
			} else if (!(strncmp(dmark,"/../",4))) {
				ictx->ctx.ldirdepth = -1;
				dmark = 0;
			} else {
				for (; *dmark=='/'; )
					dmark++;

				ictx->ctx.ldirdepth++;
				dmark = strchr(dmark,'/');
			}
		}

		ictx->ctx.ldirname = ch;
		strcpy(ch,ref);
		ch += mark - ref;
		ch += sprintf(ch,"%s","/.libs/");
		ch++;

		ictx->ctx.lbasename = ch;
		ch += sprintf(ch,"%s",++mark);
		ch++;
	} else if (ref) {
		ictx->ctx.ldirname = ch;
		ch += sprintf(ch,"%s",".libs/");
		ch++;

		ictx->ctx.lbasename = ch;
		mark = strrchr(ref,'/');
		ch += sprintf(ch,"%s",mark ? ++mark : ref);
		ch++;
	}

	/* lbasename suffix */
	if (ref && (dctx->cctx->mode == SLBT_MODE_COMPILE)) {
		if ((ch[-4] == '.') && (ch[-3] == 'l') && (ch[-2] == 'o')) {
			ch[-3] = 'o';
			ch[-2] = 0;
			ch--;
		} else if (ictx->ctx.csrc) {
			if ((mark = strrchr(ictx->ctx.lbasename,'.'))) {
				ch    = mark;
				*++ch = 'o';
				*++ch = 0;
				ch++;
			}
		}
	}

	/* cargv, -Wc */
	for (i=0, parg=dctx->cctx->cargv; *parg; parg++, ch++) {
		if (!(strncmp("-Wc,",*parg,4))) {
			strcpy(ch,&(*parg)[4]);
			ictx->ctx.argv[i++] = ch;

			for (; *ch; ch++)
				if (*ch == ',') {
					*ch++ = 0;
					ictx->ctx.argv[i++] = ch;
				}
		} else {
			ictx->ctx.argv[i++] = ch;
			ch += sprintf(ch,"%s",*parg);
			ch += ictx->exts;
		}
	}

	/* placeholders for -DPIC, -fPIC, -c, -o, <output> */
	ictx->ctx.dpic = &ictx->ctx.argv[i++];
	ictx->ctx.fpic = &ictx->ctx.argv[i++];
	ictx->ctx.cass = &ictx->ctx.argv[i++];

	ictx->ctx.noundef = &ictx->ctx.argv[i++];
	ictx->ctx.soname  = &ictx->ctx.argv[i++];
	ictx->ctx.lsoname = &ictx->ctx.argv[i++];
	ictx->ctx.symdefs = &ictx->ctx.argv[i++];
	ictx->ctx.symfile = &ictx->ctx.argv[i++];
	ictx->ctx.explarg = &ictx->ctx.argv[i++];
	ictx->ctx.expsyms = &ictx->ctx.argv[i++];

	ictx->ctx.lout[0] = &ictx->ctx.argv[i++];
	ictx->ctx.lout[1] = &ictx->ctx.argv[i++];

	ictx->ctx.rpath[0] = &ictx->ctx.argv[i++];
	ictx->ctx.rpath[1] = &ictx->ctx.argv[i++];

	ictx->ctx.sentinel= &ictx->ctx.argv[i++];

	slbt_reset_placeholders(&ictx->ctx);

	/* dsoprefix */
	if (dctx->cctx->settings.dsoprefix) {
		ictx->dsoprefix = ch;
		strcpy(ch,dctx->cctx->settings.dsoprefix);
		ch += strlen(ch) + 1;
	}

	/* output file name */
	if (ref && ((dctx->cctx->mode == SLBT_MODE_COMPILE))) {
		*ictx->ctx.lout[0] = "-o";
		*ictx->ctx.lout[1] = ch;
		ictx->ctx.lobjname = ch;

		ch += sprintf(ch,"%s%s",
			ictx->ctx.ldirname,
			ictx->ctx.lbasename)
			+ 1;

		ictx->ctx.aobjname = ch;

		ch += sprintf(ch,"%s",ictx->ctx.ldirname);
		ch -= strlen(".libs/");
		ch += sprintf(ch,"%s",
			ictx->ctx.lbasename)
			+ 1;

		ictx->ctx.ltobjname = ch;
		strcpy(ch,ictx->ctx.aobjname);

		if ((mark = strrchr(ch,'.')))
			ch = mark + sprintf(mark,"%s",".lo")
				+ 1;
	}

	/* linking: arfilename, lafilename, laifilename, dsobasename, dsofilename, mapfilename */
	if (dctx->cctx->mode == SLBT_MODE_LINK && dctx->cctx->libname) {
		if (!(dsoext = dctx->cctx->shrext))
			dsoext = dctx->cctx->settings.dsosuffix;

		/* arprefix, dsoprefix */
		if (dctx->cctx->drvflags & SLBT_DRIVER_MODULE) {
			ictx->ctx.sonameprefix = "";
			arprefix  = "";
			dsoprefix = "";
			impprefix = "";
		} else {
			ictx->ctx.sonameprefix = ictx->dsoprefix;
			arprefix  = dctx->cctx->settings.arprefix;
			dsoprefix = dctx->cctx->settings.dsoprefix;
			impprefix = dctx->cctx->settings.impprefix;
		}

		/* arfilename */
		ictx->ctx.arfilename = ch;
		ch += sprintf(ch,"%s%s%s%s",
				ictx->ctx.ldirname,
				arprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.arsuffix);
		ch++;



		/* lafilename */
		ictx->ctx.lafilename = ch;
		ch += sprintf(ch,"%s%s%s.la",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname);
		ch++;


		/* laifilename */
		ictx->ctx.laifilename = ch;
		ch += sprintf(ch,"%s%s%s.lai",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname);
		ch++;


		/* dsobasename */
		ictx->ctx.dsobasename = ch;
		ch += sprintf(ch,"%s%s%s",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname);
		ch++;

		/* dsofilename */
		ictx->ctx.dsofilename = ch;
		ch += sprintf(ch,"%s%s%s%s",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dsoext);
		ch++;

		/* mapfilename */
		ictx->ctx.mapfilename = ch;
		ch += sprintf(ch,"%s%s%s%s%s%s",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dctx->cctx->release ? "-" : "",
				dctx->cctx->release ? dctx->cctx->release : "",
				dctx->cctx->settings.mapsuffix);
		ch++;

		/* deffilename */
		ictx->ctx.deffilename = ch;
		ch += sprintf(ch,"%s%s%s%s%s%s.def",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dctx->cctx->release ? "-" : "",
				dctx->cctx->release ? dctx->cctx->release : "",
				dsoext);
		ch++;

		/* rpathfilename */
		ictx->ctx.rpathfilename = ch;
		ch += sprintf(ch,"%s%s%s%s.slibtool.rpath",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dsoext);
		ch++;

		/* default implib file name */
		ictx->ctx.dimpfilename = ch;
		ch += sprintf(ch,"%s%s%s%s",
				ictx->ctx.ldirname,
				impprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.impsuffix);
		ch++;


		/* primary implib file name */
		ictx->ctx.pimpfilename = ch;
		ch += sprintf(ch,"%s%s%s%s%s.%d%s",
				ictx->ctx.ldirname,
				impprefix,
				dctx->cctx->libname,
				dctx->cctx->release ? "-" : "",
				dctx->cctx->release ? dctx->cctx->release : "",
				dctx->cctx->verinfo.major,
				dctx->cctx->settings.impsuffix);
		ch++;

		/* versioned implib file name */
		ictx->ctx.vimpfilename = ch;
		ch += sprintf(ch,"%s%s%s%s%s.%d.%d.%d%s",
				ictx->ctx.ldirname,
				impprefix,
				dctx->cctx->libname,
				dctx->cctx->release ? "-" : "",
				dctx->cctx->release ? dctx->cctx->release : "",
				dctx->cctx->verinfo.major,
				dctx->cctx->verinfo.minor,
				dctx->cctx->verinfo.revision,
				dctx->cctx->settings.impsuffix);
		ch++;

		/* relfilename */
		if (dctx->cctx->release) {
			ictx->ctx.relfilename = ch;
			ch += dctx->cctx->verinfo.verinfo
				? sprintf(ch,"%s%s%s-%s%s.%d.%d.%d%s",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->verinfo.minor,
					dctx->cctx->verinfo.revision,
					dctx->cctx->settings.osdfussix)
				: sprintf(ch,"%s%s%s-%s%s",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dsoext);
			ch++;
		}

		/* dsorellnkname */
		if (dctx->cctx->release) {
			ictx->ctx.dsorellnkname = ch;
			ch += sprintf(ch,"%s%s%s-%s%s",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dsoext);
			ch++;
		}

		/* dlopensrc, dlopenobj */
		if (idctx->dlopenv) {
			ictx->ctx.dlopensrc = ch;
			ch += sprintf(ch,"%s%s%s.dlopen.c",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname);

			ch++;

			ictx->ctx.dlopenobj = ch;
			ch += sprintf(ch,"%s%s%s.dlopen.o",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname);

			ch++;

			ictx->ctx.dlpreopen = ch;
			ch += sprintf(ch,"%s%s%s.dlpreopen.a",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname);

			ch++;

			ictx->ctx.dlunit = ch;
			ch += sprintf(ch,"%s%s",
					dsoprefix,
					dctx->cctx->libname);

			ch++;
		}

	/* linking: exefilename */
	} else if (dctx->cctx->mode == SLBT_MODE_LINK) {
		ictx->ctx.exefilename = ch;

		if ((slash = strrchr(dctx->cctx->output,'/'))) {
			strcpy(ch,dctx->cctx->output);
			mark = ch + (slash - dctx->cctx->output);
			sprintf(++mark,".libs/%s",++slash);
			ch += strlen(ch) + 1;
		} else
			ch += sprintf(ch,".libs/%s",dctx->cctx->output) + 1;
	}

	/* dlopensrc, dlopenobj: executable program */
	if (idctx->dlopenv && !dctx->cctx->libname) {
		ictx->ctx.dlopensrc = ch;
		ch += sprintf(ch,"%s.dlopen.c",
				ictx->ctx.exefilename);

		ch++;

		ictx->ctx.dlopenobj = ch;
		ch += sprintf(ch,"%s.dlopen.o",
				ictx->ctx.exefilename);

		ch++;

		ictx->ctx.dlpreopen = ch;
		ch += sprintf(ch,"%s.dlpreopen.a",
				ictx->ctx.exefilename);

		ch++;

		ictx->ctx.dlunit = ch;
		ch += sprintf(ch,"%s",
				"@PROGRAM@");

		ch++;

		ictx->ctx.mapfilename = ch;
		ch += sprintf(ch,"%s%s",
				ictx->ctx.exefilename,
				dctx->cctx->settings.mapsuffix);
		ch++;

	}

	/* -lsltdl (--prefer-sltdl impl.) */
	ictx->lsltdl = ch;
	strcpy(ch,"-lsltdl");
	ch += 8;

	/* dlopen, dlpreopen */
	if ((dlopenv = idctx->dlopenv), (dlactxv = ictx->dlactxv)) {
		if (slbt_ar_get_varchive_ctx(dctx,dlactxv) < 0)
			return slbt_ectx_free_exec_ctx_impl(
				ictx,
				SLBT_NESTED_ERROR(dctx));

		if (slbt_ar_update_syminfo(*dlactxv) < 0)
			return slbt_ectx_free_exec_ctx_impl(
				ictx,
				SLBT_NESTED_ERROR(dctx));

		dlactxv++;

		for (; *dlopenv; ) {
			dlentry  = *dlopenv;
			fpreopen = (dlentry->tag == TAG_DLPREOPEN);

			arname = ictx->sbuf;
			strcpy(arname,dlentry->arg);

			slbt_adjust_wrapper_argument(
				arname,true,
				".expsyms.a");

			errinfp = idctx->errinfp;

			if (slbt_ar_get_archive_ctx(dctx,arname,dlactxv) < 0) {
				if ((*errinfp)->esyscode != ENOENT)
					return slbt_ectx_free_exec_ctx_impl(
						ictx,
						SLBT_NESTED_ERROR(dctx));

				strcpy(arname,dlentry->arg);

				slbt_adjust_wrapper_argument(
					arname,true,
					dctx->cctx->settings.arsuffix);

				if (slbt_ar_get_archive_ctx(dctx,arname,dlactxv) < 0)
					return slbt_ectx_free_exec_ctx_impl(
						ictx,
						SLBT_NESTED_ERROR(dctx));

				idctx->errinfp = errinfp;

				for (; *errinfp; )
					*errinfp++ = 0;

				fpreopen = true;
			}

			if (fpreopen) {
				if (slbt_ar_update_syminfo(*dlactxv) < 0)
					return slbt_ectx_free_exec_ctx_impl(
						ictx,
						SLBT_NESTED_ERROR(dctx));

				dlactxv++;
			} else {
				slbt_ar_free_archive_ctx(*dlactxv);
				*dlactxv = 0;
			}

			dlopenv++;
		}

		if (slbt_mkdir(dctx,ictx->ctx.ldirname) < 0)
			return slbt_ectx_free_exec_ctx_impl(
				ictx,
				SLBT_SYSTEM_ERROR(
					dctx,
					ictx->ctx.ldirname));

		if (slbt_ar_create_dlsyms(
					ictx->dlactxv,
					ictx->ctx.dlunit,
					ictx->ctx.dlopensrc,
					0644) < 0)
				return slbt_ectx_free_exec_ctx_impl(
					ictx,
					SLBT_NESTED_ERROR(dctx));

		if (slbt_ar_merge_archives(ictx->dlactxv,&ictx->dlpreopen) < 0)
				return slbt_ectx_free_exec_ctx_impl(
					ictx,
					SLBT_NESTED_ERROR(dctx));

		if (slbt_ar_store_archive(ictx->dlpreopen,ictx->ctx.dlpreopen,0644) < 0)
				return slbt_ectx_free_exec_ctx_impl(
					ictx,
					SLBT_NESTED_ERROR(dctx));
	}

	/* vector of exported symbols (raw input via -export-symbols) */
	if (dctx->cctx->expsyms)
		if (slbt_lib_get_symlist_ctx(
				dctx,dctx->cctx->expsyms,
				&ictx->sctx) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* argument strings shadow copy */
	memcpy(ictx->shadow,ictx->args,ictx->size);

	/* compile mode: argument vector shadow copy */
	if (dctx->cctx->mode == SLBT_MODE_COMPILE)
		for (src=ictx->ctx.argv, dst=ictx->ctx.xargv; *src; src++, dst++)
			*dst = *src;

	/* save the full vector's lout, mout */
	ictx->lout[0] = ictx->ctx.lout[0];
	ictx->lout[1] = ictx->ctx.lout[1];

	ictx->mout[0] = ictx->ctx.mout[0];
	ictx->mout[1] = ictx->ctx.mout[1];

	/* all done */
	*ectx = &ictx->ctx;
	return 0;
}


static int slbt_ectx_free_exec_ctx_impl(
	struct slbt_exec_ctx_impl *	ictx,
	int				status)
{
	struct slbt_archive_ctx ** dlactxv;

	if (ictx->sctx)
		slbt_lib_free_symlist_ctx(ictx->sctx);

	if (ictx->fdwrapper >= 0)
		close(ictx->fdwrapper);

	if (ictx->dlactxv) {
		for (dlactxv=ictx->dlactxv; *dlactxv; dlactxv++)
			slbt_ar_free_archive_ctx(*dlactxv);

		free(ictx->dlactxv);
	}

	free(ictx->args);
	free(ictx->shadow);
	free(ictx->vbuffer);
	free (ictx);

	return status;
}


void slbt_ectx_free_exec_ctx(struct slbt_exec_ctx * ctx)
{
	struct slbt_exec_ctx_impl *	ictx;
	uintptr_t			addr;

	if (ctx) {
		addr = (uintptr_t)ctx - offsetof(struct slbt_exec_ctx_impl,ctx);
		ictx = (struct slbt_exec_ctx_impl *)addr;
		slbt_ectx_free_exec_ctx_impl(ictx,0);
	}
}


void slbt_ectx_reset_arguments(struct slbt_exec_ctx * ectx)
{
	struct slbt_exec_ctx_impl *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ectx - offsetof(struct slbt_exec_ctx_impl,ctx);
	ictx = (struct slbt_exec_ctx_impl *)addr;
	memcpy(ictx->args,ictx->shadow,ictx->size);
}


void slbt_ectx_reset_argvector(struct slbt_exec_ctx * ectx)
{
	struct slbt_exec_ctx_impl *	ictx;
	uintptr_t			addr;
	char ** 			src;
	char ** 			dst;

	addr = (uintptr_t)ectx - offsetof(struct slbt_exec_ctx_impl,ctx);
	ictx = (struct slbt_exec_ctx_impl *)addr;

	for (src=ectx->xargv, dst=ectx->argv; *src; src++, dst++)
		*dst = *src;

	*dst = 0;

	ectx->lout[0] = ictx->lout[0];
	ectx->lout[1] = ictx->lout[1];

	ectx->mout[0] = ictx->mout[0];
	ectx->mout[1] = ictx->mout[1];
}


slbt_hidden void slbt_reset_placeholders(struct slbt_exec_ctx * ectx)
{
	struct slbt_exec_ctx_impl * ictx;

	ictx = slbt_get_exec_ictx(ectx);

	if (ictx->lout[0]) {
		ectx->lout[0] = ictx->lout[0];
		ectx->lout[1] = ictx->lout[1];
	}

	*ectx->dpic = "-USLIBTOOL_PLACEHOLDER_DPIC";
	*ectx->fpic = "-USLIBTOOL_PLACEHOLDER_FPIC";
	*ectx->cass = "-USLIBTOOL_PLACEHOLDER_COMPILE_ASSEMBLE";

	*ectx->noundef = "-USLIBTOOL_PLACEHOLDER_NO_UNDEFINED";
	*ectx->soname  = "-USLIBTOOL_PLACEHOLDER_SONAME";
	*ectx->lsoname = "-USLIBTOOL_PLACEHOLDER_LSONAME";
	*ectx->symdefs = "-USLIBTOOL_PLACEHOLDER_SYMDEF_SWITCH";
	*ectx->symfile = "-USLIBTOOL_PLACEHOLDER_SYMDEF_FILE";
	*ectx->explarg = "-USLIBTOOL_PLACEHOLDER_EXPSYMS_SWITCH";
	*ectx->expsyms = "-USLIBTOOL_PLACEHOLDER_EXPSYMS_FILE";

	*ectx->lout[0] = "-USLIBTOOL_PLACEHOLDER_OUTPUT_SWITCH";
	*ectx->lout[1] = "-USLIBTOOL_PLACEHOLDER_OUTPUT_FILE";

	*ectx->rpath[0] = "-USLIBTOOL_PLACEHOLDER_RPATH_SWITCH";
	*ectx->rpath[1] = "-USLIBTOOL_PLACEHOLDER_RPATH_DIR";

	ectx->mout[0]  = 0;
	ectx->mout[1]  = 0;

	*ectx->sentinel= 0;
}

slbt_hidden void slbt_disable_placeholders(struct slbt_exec_ctx * ectx)
{
	*ectx->dpic = 0;
	*ectx->fpic = 0;
	*ectx->cass = 0;

	*ectx->noundef = 0;
	*ectx->soname  = 0;
	*ectx->lsoname = 0;
	*ectx->symdefs = 0;
	*ectx->symfile = 0;
	*ectx->explarg = 0;
	*ectx->expsyms = 0;

	*ectx->lout[0] = 0;
	*ectx->lout[1] = 0;

	*ectx->rpath[0] = 0;
	*ectx->rpath[1] = 0;

	*ectx->sentinel= 0;
}
