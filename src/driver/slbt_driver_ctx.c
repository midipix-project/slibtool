/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define ARGV_DRIVER

#include <slibtool/slibtool.h>
#include "slibtool_version.h"
#include "slibtool_driver_impl.h"
#include "slibtool_objlist_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_lconf_impl.h"
#include "slibtool_mkvars_impl.h"
#include "slibtool_txtline_impl.h"
#include "slibtool_stoolie_impl.h"
#include "slibtool_ar_impl.h"
#include "argv/argv.h"

extern char ** environ;

/* annotation strings */
static const char cfgexplicit[] = "command-line argument";
static const char cfglconf[]    = "derived from <libtool>";
static const char cfgmkvars[]   = "derived from <makefile>";

/* package info */
static const struct slbt_source_version slbt_src_version = {
	SLBT_TAG_VER_MAJOR,
	SLBT_TAG_VER_MINOR,
	SLBT_TAG_VER_PATCH,
	SLIBTOOL_GIT_VERSION
};

/* default fd context */
static const struct slbt_fd_ctx slbt_default_fdctx = {
	.fdin  = STDIN_FILENO,
	.fdout = STDOUT_FILENO,
	.fderr = STDERR_FILENO,
	.fdcwd = AT_FDCWD,
	.fddst = AT_FDCWD,
	.fdlog = (-1),
};

static const char aclr_reset   [] = "\x1b[0m";
static const char aclr_bold    [] = "\x1b[1m";
static const char aclr_red     [] = "\x1b[31m";
static const char aclr_green   [] = "\x1b[32m";
static const char aclr_yellow  [] = "\x1b[33m";
static const char aclr_blue    [] = "\x1b[34m";
static const char aclr_magenta [] = "\x1b[35m";
static const char aclr_cyan    [] = "\x1b[36m";
static const char aclr_white   [] = "\x1b[37m";


static void slbt_output_raw_vector(int fderr, char ** argv, char ** envp, bool fcolor)
{
	char **		parg;
	char *		dot;
	const char *	color;
	const char *    prev;
	const char *    prog;

	(void)envp;

	if (fcolor)
		slbt_dprintf(fderr,"%s%s",aclr_bold,aclr_red);

	slbt_dprintf(fderr,"\n\n\n%s",argv[0]);

	for (prev=0,prog=0,parg=&argv[1]; *parg; parg++) {
		dot  = strrchr(*parg,'.');

		if (!fcolor) {
			color = "";

		} else if (!prog && (parg[0][0] != '-')) {
			color = aclr_magenta;
			prog  = *parg;
			prev  = 0;

		} else if (!strcmp(parg[0],"-o")) {
			color = aclr_white;
			prev  = color;

		} else if (!strcmp(parg[0],"-c")) {
			color = aclr_green;
			prev  = color;

		} else if (!strncmp(parg[0],"-Wl,",4)) {
			color = aclr_magenta;
			prev  = 0;

		} else if (prev == aclr_white) {
			color = prev;
			prev  = 0;

		} else if (dot && !strcmp(dot,".o")) {
			color = aclr_cyan;
			prev  = 0;

		} else if (dot && !strcmp(dot,".lo")) {
			color = aclr_cyan;
			prev  = 0;

		} else if (dot && !strcmp(dot,".la")) {
			color = aclr_yellow;
			prev  = 0;

		} else if (!strcmp(parg[0],"-dlopen")) {
			color = aclr_yellow;
			prev  = color;

		} else if (!strcmp(parg[0],"-dlpreopen")) {
			color = aclr_yellow;
			prev  = color;

		} else if (!strcmp(parg[0],"-objectlist")) {
			color = aclr_yellow;
			prev  = color;

		} else if (*parg[0] == '-') {
			color = aclr_blue;
			prev  = color;

		} else if (prev) {
			color = prev;
			prev  = 0;

		} else {
			color = aclr_yellow;
		}

		slbt_dprintf(fderr," %s%s",color,*parg);
	}

	slbt_dprintf(fderr,"%s\n\n",fcolor ? aclr_reset : "");
}

slbt_hidden const char * slbt_program_name(const char * path)
{
	return argv_program_name(path);
}


slbt_hidden int slbt_optv_init(
	const struct argv_option    options[],
	const struct argv_option ** optv)
{
	return argv_optv_init(options,optv);
}


slbt_hidden void slbt_argv_scan(
	char **				argv,
	const struct argv_option **	optv,
	struct argv_ctx *		ctx,
	struct argv_meta *		meta)
{
	argv_scan(argv,optv,ctx,meta);
}


slbt_hidden struct argv_meta * slbt_argv_get(
	char **                         argv,
	const struct argv_option **     optv,
	int                             flags,
	int                             fd)
{
	return argv_get(argv,optv,flags,fd);
}


slbt_hidden void slbt_argv_free(struct argv_meta * meta)
{
	argv_free(meta);
}


slbt_hidden void slbt_argv_usage(
	int		                fd,
	const char *	                header,
	const struct	argv_option **  optv,
	const char *	                mode)
{
	argv_usage(fd,header,optv,mode);
}


slbt_hidden void slbt_argv_usage_plain(
	int		                fd,
	const char *	                header,
	const struct	argv_option **  optv,
	const char *	                mode)
{
	argv_usage_plain(fd,header,optv,mode);
}


slbt_hidden uint64_t slbt_argv_flags(uint64_t flags)
{
	uint32_t ret = 0;

	if (flags & SLBT_DRIVER_VERBOSITY_NONE)
		ret |= ARGV_VERBOSITY_NONE;

	if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
		ret |= ARGV_VERBOSITY_ERRORS;

	if (flags & SLBT_DRIVER_VERBOSITY_STATUS)
		ret |= ARGV_VERBOSITY_STATUS;

	return ret;
}

static int slbt_free_argv_buffer(
	struct slbt_split_vector * sargv,
	struct slbt_obj_list *     objlistv)
{
	struct slbt_obj_list * objlistp;

	if (sargv->dargs)
		free(sargv->dargs);

	if (sargv->dargv)
		free(sargv->dargv);

	if (sargv->targv)
		free(sargv->targv);

	if (objlistv) {
		for (objlistp=objlistv; objlistp->name; objlistp++) {
			free(objlistp->objv);
			free(objlistp->addr);
		}

		free(objlistv);
	}

	return -1;
}

slbt_hidden int slbt_driver_usage(
	int				fdout,
	const char *			program,
	const char *			arg,
	const struct argv_option **	optv,
	struct argv_meta *		meta,
	struct slbt_split_vector *	sargv,
	struct slbt_obj_list *		objlistv,
	int				noclr)
{
	char header[512];

	snprintf(header,sizeof(header),
		"Usage: %s [options] <file>...\n" "Options:\n",
		program);

	switch (noclr) {
		case 0:
			argv_usage(fdout,header,optv,arg);
			break;

		default:
			argv_usage_plain(fdout,header,optv,arg);
			break;
	}

	argv_free(meta);
	slbt_free_argv_buffer(sargv,objlistv);

	return SLBT_USAGE;
}

static struct slbt_driver_ctx_impl * slbt_driver_ctx_alloc(
	const struct slbt_fd_ctx *	fdctx,
	const struct slbt_common_ctx *	cctx,
	struct slbt_split_vector *	sargv,
	struct slbt_obj_list *		objlistv,
	char **				envp,
	size_t				ndlopen)
{
	struct slbt_driver_ctx_alloc *	ictx;
	size_t				size;
	int				elements;

	size =  sizeof(struct slbt_driver_ctx_alloc);

	if (!(ictx = calloc(1,size))) {
		slbt_free_argv_buffer(sargv,objlistv);
		return 0;
	}

	if (ndlopen) {
		if (!(ictx->ctx.dlopenv = calloc(ndlopen+1,sizeof(*ictx->ctx.dlopenv)))) {
			free(ictx);
			slbt_free_argv_buffer(sargv,objlistv);
			return 0;
		}

		ictx->ctx.ndlopen = ndlopen;
	}

	ictx->ctx.dargs = sargv->dargs;
	ictx->ctx.dargv = sargv->dargv;
	ictx->ctx.targv = sargv->targv;
	ictx->ctx.cargv = sargv->cargv;
	ictx->ctx.envp  = envp;

	memcpy(&ictx->ctx.fdctx,fdctx,sizeof(*fdctx));
	memcpy(&ictx->ctx.cctx,cctx,sizeof(*cctx));

	elements = sizeof(ictx->ctx.erribuf) / sizeof(*ictx->ctx.erribuf);

	ictx->ctx.errinfp  = &ictx->ctx.erriptr[0];
	ictx->ctx.erricap  = &ictx->ctx.erriptr[--elements];

	ictx->ctx.objlistv = objlistv;

	ictx->ctx.ctx.errv = ictx->ctx.errinfp;

	return &ictx->ctx;
}

static int slbt_lib_get_driver_ctx_fail(
	struct slbt_driver_ctx * dctx,
	struct argv_meta *       meta)
{
	if (dctx) {
		slbt_output_error_vector(dctx);
		slbt_lib_free_driver_ctx(dctx);
	} else {
		argv_free(meta);
	}

	return -1;
}


static int slbt_driver_fail_incompatible_args(
	int				fderr,
	uint64_t			drvflags,
	struct argv_meta *		meta,
	const char *			program,
	const char *			afirst,
	const char *			asecond)
{
	int fcolor;

	fcolor = (drvflags & SLBT_DRIVER_ANNOTATE_NEVER)
		? 0 : isatty(fderr);

	if (drvflags & SLBT_DRIVER_VERBOSITY_ERRORS){
		if (fcolor)
			slbt_dprintf(
				fderr,"%s%s",
				aclr_bold,aclr_red);

		slbt_dprintf(fderr,
			"%s: error: incompatible arguments: "
			"at the most one of %s and %s "
			"may be used.\n",
			program,afirst,asecond);

		if (fcolor)
			slbt_dprintf(
				fderr,"%s",
				aclr_reset);
	}

	return slbt_lib_get_driver_ctx_fail(0,meta);
}


static int slbt_driver_parse_tool_argv(const char * tool, char *** tool_argv)
{
	return slbt_txtline_to_string_vector(tool,tool_argv);
}


int slbt_lib_get_driver_ctx(
	char **				argv,
	char **				envp,
	uint64_t			flags,
	const struct slbt_fd_ctx *	fdctx,
	struct slbt_driver_ctx **	pctx)
{
	struct slbt_split_vector	sargv;
	struct slbt_obj_list *		objlistv;
	struct slbt_driver_ctx_impl *	ctx;
	struct slbt_common_ctx		cctx;
	struct slbt_error_info**        errinfp;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	struct argv_entry *		cmdstatic;
	struct argv_entry *		cmdshared;
	struct argv_entry *		cmdnostatic;
	struct argv_entry *		cmdnoshared;
	const char *			program;
	const char *			lconf;
	const char *			mkvars;
	uint64_t			lflags;
	size_t                          ndlopen;
	struct argv_entry **            dlopenv;
	const char *                    cfgmeta_host;
	const char *                    cfgmeta_ar;
	const char *                    cfgmeta_as;
	const char *                    cfgmeta_nm;
	const char *                    cfgmeta_ranlib;
	const char *                    cfgmeta_dlltool;

	if (flags & SLBT_DRIVER_MODE_AR) {
		argv_optv_init(slbt_ar_options,optv);

	} else if (flags & SLBT_DRIVER_MODE_STOOLIE) {
		argv_optv_init(slbt_stoolie_options,optv);

	} else {
		argv_optv_init(slbt_default_options,optv);
	}

	if (!fdctx)
		fdctx = &slbt_default_fdctx;

	sargv.dargs = 0;
	sargv.dargv = 0;
	sargv.targv = 0;
	sargv.cargv = 0;
	objlistv    = 0;
	ndlopen     = 0;
	lflags      = 0;

	switch (slbt_split_argv(argv,flags,&sargv,&objlistv,fdctx->fderr,fdctx->fdcwd)) {
		case SLBT_OK:
			break;

		case SLBT_USAGE:
			return SLBT_USAGE;

		default:
			return slbt_free_argv_buffer(&sargv,objlistv);
	}

	if (!(meta = argv_get(
			sargv.targv,optv,
			slbt_argv_flags(flags),
			fdctx->fderr)))
		return slbt_free_argv_buffer(&sargv,objlistv);

	lconf   = 0;
	mkvars  = 0;
	program = argv_program_name(argv[0]);

	memset(&cctx,0,sizeof(cctx));

	if (flags & SLBT_DRIVER_MODE_AR)
		cctx.mode = SLBT_MODE_AR;

	else if (flags & SLBT_DRIVER_MODE_STOOLIE)
		cctx.mode = SLBT_MODE_STOOLIE;

	/* default flags (set at compile time and derived from symlink) */
	cctx.drvflags = flags;

	/* full annotation when annotation is on; */
	if (!(cctx.drvflags & SLBT_DRIVER_ANNOTATE_NEVER))
		cctx.drvflags |= SLBT_DRIVER_ANNOTATE_FULL;

	/* track incompatible command-line arguments */
	cmdstatic   = 0;
	cmdshared   = 0;
	cmdnostatic = 0;
	cmdnoshared = 0;

	cfgmeta_host   = 0;
	cfgmeta_ar     = 0;
	cfgmeta_as     = 0;
	cfgmeta_nm     = 0;
	cfgmeta_ranlib = 0;
	cfgmeta_dlltool = 0;

	/* get options */
	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_HELP:
				case TAG_HELP_ALL:
					switch (cctx.mode) {
						case SLBT_MODE_INSTALL:
						case SLBT_MODE_UNINSTALL:
						case SLBT_MODE_AR:
						case SLBT_MODE_STOOLIE:
							break;

					default:
						return (flags & SLBT_DRIVER_VERBOSITY_USAGE)
							? slbt_driver_usage(
								fdctx->fdout,program,
								entry->arg,optv,
								meta,&sargv,objlistv,
								(cctx.drvflags & SLBT_DRIVER_ANNOTATE_NEVER))
							: SLBT_USAGE;
					}

					break;

				case TAG_VERSION:
					cctx.drvflags |= SLBT_DRIVER_VERSION;
					break;

				case TAG_HEURISTICS:
					cctx.drvflags |= SLBT_DRIVER_HEURISTICS;
					lconf = entry->arg;
					break;

				case TAG_MKVARS:
					mkvars = entry->arg;
					break;

				case TAG_MODE:
					if (!strcmp("clean",entry->arg))
						cctx.mode = SLBT_MODE_CLEAN;

					else if (!strcmp("compile",entry->arg))
						cctx.mode = SLBT_MODE_COMPILE;

					else if (!strcmp("execute",entry->arg))
						cctx.mode = SLBT_MODE_EXECUTE;

					else if (!strcmp("finish",entry->arg))
						cctx.mode = SLBT_MODE_FINISH;

					else if (!strcmp("install",entry->arg))
						cctx.mode = SLBT_MODE_INSTALL;

					else if (!strcmp("link",entry->arg))
						cctx.mode = SLBT_MODE_LINK;

					else if (!strcmp("uninstall",entry->arg))
						cctx.mode = SLBT_MODE_UNINSTALL;

					else if (!strcmp("ar",entry->arg))
						cctx.mode = SLBT_MODE_AR;

					else if (!strcmp("stoolie",entry->arg))
						cctx.mode = SLBT_MODE_STOOLIE;

					else if (!strcmp("slibtoolize",entry->arg))
						cctx.mode = SLBT_MODE_STOOLIE;
					break;

				case TAG_FINISH:
					cctx.mode = SLBT_MODE_FINISH;
					break;

				case TAG_DRY_RUN:
					cctx.drvflags |= SLBT_DRIVER_DRY_RUN;
					break;

				case TAG_TAG:
					if (!strcmp("CC",entry->arg))
						cctx.tag = SLBT_TAG_CC;

					else if (!strcmp("CXX",entry->arg))
						cctx.tag = SLBT_TAG_CXX;

					else if (!strcmp("FC",entry->arg))
						cctx.tag = SLBT_TAG_FC;

					else if (!strcmp("F77",entry->arg))
						cctx.tag = SLBT_TAG_F77;

					else if (!strcmp("ASM",entry->arg))
						cctx.tag = SLBT_TAG_ASM;

					else if (!strcmp("NASM",entry->arg))
						cctx.tag = SLBT_TAG_NASM;

					else if (!strcmp("RC",entry->arg))
						cctx.tag = SLBT_TAG_RC;

					else if (!strcmp("disable-static",entry->arg))
						cmdnostatic = entry;

					else if (!strcmp("disable-shared",entry->arg))
						cmdnoshared = entry;
					break;

				case TAG_INFO:
					cctx.drvflags |= SLBT_DRIVER_INFO;
					break;

				case TAG_CONFIG:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_CONFIG;
					break;

				case TAG_DUMPMACHINE:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_MACHINE;
					break;

				case TAG_PRINT_AUX_DIR:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_AUX_DIR;
					break;

				case TAG_PRINT_M4_DIR:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_M4_DIR;
					break;

				case TAG_PRINT_SHARED_EXT:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_SHARED_EXT;
					break;

				case TAG_PRINT_STATIC_EXT:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_STATIC_EXT;
					break;

				case TAG_DEBUG:
					cctx.drvflags |= SLBT_DRIVER_DEBUG;
					break;

				case TAG_FEATURES:
					cctx.drvflags |= SLBT_DRIVER_FEATURES;
					break;

				case TAG_LEGABITS:
					if (!entry->arg)
						cctx.drvflags |= SLBT_DRIVER_LEGABITS;

					else if (!strcmp("enabled",entry->arg))
						cctx.drvflags |= SLBT_DRIVER_LEGABITS;

					else
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_LEGABITS;

					break;

				case TAG_CCWRAP:
					cctx.ccwrap = entry->arg;
					break;

				case TAG_IMPLIB:
					if (!strcmp("idata",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_IMPLIB_IDATA;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_IMPLIB_DSOMETA;

					} else if (!strcmp("never",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_IMPLIB_DSOMETA;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_IMPLIB_IDATA;
					}

					break;

				case TAG_WARNINGS:
					if (!strcmp("all",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_ALL;

					else if (!strcmp("error",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_ERROR;

					else if (!strcmp("none",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_NONE;
					break;

				case TAG_ANNOTATE:
					if (!strcmp("always",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_ALWAYS;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_NEVER;

					} else if (!strcmp("never",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_NEVER;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_ALWAYS;

					} else if (!strcmp("minimal",entry->arg)) {
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_FULL;

					} else if (!strcmp("full",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_FULL;
					}

					break;

				case TAG_DEPS:
					cctx.drvflags |= SLBT_DRIVER_DEPS;
					break;

				case TAG_SILENT:
					cctx.drvflags |= SLBT_DRIVER_SILENT;
					break;

				case TAG_VERBOSE:
					cctx.drvflags |= SLBT_DRIVER_VERBOSE;
					break;

				case TAG_HOST:
					cctx.host.host = entry->arg;
					cfgmeta_host   = cfgexplicit;
					break;

				case TAG_FLAVOR:
					cctx.host.flavor = entry->arg;
					break;

				case TAG_AR:
					cctx.host.ar = entry->arg;
					cfgmeta_ar   = cfgexplicit;
					break;

				case TAG_AS:
					cctx.host.as = entry->arg;
					cfgmeta_as   = cfgexplicit;
					break;

				case TAG_NM:
					cctx.host.nm = entry->arg;
					cfgmeta_nm   = cfgexplicit;
					break;

				case TAG_RANLIB:
					cctx.host.ranlib = entry->arg;
					cfgmeta_ranlib   = cfgexplicit;
					break;

				case TAG_WINDRES:
					cctx.host.windres = entry->arg;
					break;

				case TAG_DLLTOOL:
					cctx.host.dlltool = entry->arg;
					cfgmeta_dlltool   = cfgexplicit;
					break;

				case TAG_MDSO:
					cctx.host.mdso = entry->arg;
					break;

				case TAG_OUTPUT:
					cctx.output = entry->arg;
					break;

				case TAG_SHREXT:
					cctx.shrext = entry->arg;
					break;

				case TAG_RPATH:
					cctx.rpath = entry->arg;
					break;

				case TAG_SYSROOT:
					cctx.sysroot = entry->arg;
					break;

				case TAG_RELEASE:
					cctx.release = entry->arg;
					break;

				case TAG_DLOPEN:
				case TAG_DLPREOPEN:
					ndlopen++;
					break;

				case TAG_STATIC_LIBTOOL_LIBS:
					cctx.drvflags |= SLBT_DRIVER_STATIC_LIBTOOL_LIBS;
					break;

				case TAG_EXPORT_DYNAMIC:
					cctx.drvflags |= SLBT_DRIVER_EXPORT_DYNAMIC;
					break;

				case TAG_EXPSYMS_FILE:
					cctx.expsyms = entry->arg;
					break;

				case TAG_EXPSYMS_REGEX:
					cctx.regex = entry->arg;
					break;

				case TAG_VERSION_INFO:
					cctx.verinfo.verinfo = entry->arg;
					break;

				case TAG_VERSION_NUMBER:
					cctx.verinfo.vernumber = entry->arg;
					break;

				case TAG_TARGET:
					cctx.target = entry->arg;
					break;

				case TAG_PREFER_PIC:
					cctx.drvflags |= SLBT_DRIVER_PRO_PIC;
					break;

				case TAG_PREFER_NON_PIC:
					cctx.drvflags |= SLBT_DRIVER_ANTI_PIC;
					break;

				case TAG_NO_UNDEFINED:
					cctx.drvflags |= SLBT_DRIVER_NO_UNDEFINED;
					break;

				case TAG_PREFER_SLTDL:
					cctx.drvflags |= SLBT_DRIVER_PREFER_SLTDL;
					break;

				case TAG_MODULE:
					cctx.drvflags |= SLBT_DRIVER_MODULE;
					break;

				case TAG_ALL_STATIC:
					cctx.drvflags |= SLBT_DRIVER_ALL_STATIC;
					break;

				case TAG_DISABLE_STATIC:
					cmdnostatic = entry;
					break;

				case TAG_DISABLE_SHARED:
					cmdnoshared = entry;
					break;

				case TAG_AVOID_VERSION:
					cctx.drvflags |= SLBT_DRIVER_AVOID_VERSION;
					break;

				case TAG_SHARED:
					cmdshared = entry;
					cctx.drvflags |= SLBT_DRIVER_PREFER_SHARED;
					break;

				case TAG_STATIC:
					cmdstatic = entry;
					cctx.drvflags |= SLBT_DRIVER_PREFER_STATIC;
					break;

				case TAG_WEAK:
					break;
			}
		}
	}

	/* enable both shared and static targets by default as appropriate */
	if (!(cctx.drvflags & SLBT_DRIVER_HEURISTICS)) {
		if (!cmdnoshared)
			cctx.drvflags |= SLBT_DRIVER_SHARED;

		if (!cmdnostatic)
			cctx.drvflags |= SLBT_DRIVER_STATIC;
	}

	/* incompatible command-line arguments? */
	if (cmdstatic && cmdshared)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"-static",
			"-shared");

	if (cmdstatic && cmdnostatic)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"-static",
			"--disable-static");

	if (cmdshared && cmdnoshared)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"-shared",
			"--disable-shared");

	if (cmdnostatic && cmdnoshared)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"--disable-static",
			"--disable-shared");

	if (lconf && mkvars)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"--heuristics=<cfgfile>",
			"--mkvars=<makefile>");

	/* -static? */
	if (cmdstatic) {
		cctx.drvflags |= SLBT_DRIVER_STATIC;
		cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;
	}

	/* shared? */
	if (cmdshared) {
		cctx.drvflags |= SLBT_DRIVER_SHARED;
		cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_SHARED;
	}

	/* -disable-static? */
	if (cmdnostatic) {
		cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_STATIC;
	}

	/* -disable-shared? */
	if (cmdnoshared) {
		cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SHARED;
	}

	/* debug: raw argument vector */
	if (cctx.drvflags & SLBT_DRIVER_DEBUG)
		slbt_output_raw_vector(
			fdctx->fderr,argv,envp,
			(cctx.drvflags & SLBT_DRIVER_ANNOTATE_NEVER)
				? 0 : isatty(fdctx->fderr));

	/* -o in install mode means USER */
	if ((cctx.mode == SLBT_MODE_INSTALL) && cctx.output) {
		cctx.user   = cctx.output;
		cctx.output = 0;
	}

	/* config mode */
	if (cctx.drvflags & SLBT_DRIVER_OUTPUT_CONFIG)
		cctx.mode = SLBT_MODE_CONFIG;

	/* info mode */
	if (cctx.drvflags & (SLBT_DRIVER_INFO | SLBT_DRIVER_FEATURES))
		cctx.mode = SLBT_MODE_INFO;

	/* --tag */
	if (cctx.mode == SLBT_MODE_COMPILE)
		if (cctx.tag == SLBT_TAG_UNKNOWN)
			cctx.tag = SLBT_TAG_CC;

	/* dlopen/dlpreopen: first vector member is a virtual archive */
	if (ndlopen)
		ndlopen++;

	/* driver context */
	if (!(ctx = slbt_driver_ctx_alloc(fdctx,&cctx,&sargv,objlistv,envp,ndlopen)))
		return slbt_lib_get_driver_ctx_fail(0,meta);

	/* ctx */
	ctx->ctx.program	= program;
	ctx->ctx.cctx		= &ctx->cctx;

	ctx->cctx.targv		= sargv.targv;
	ctx->cctx.cargv		= sargv.cargv;
	ctx->meta               = meta;

	/* mkvars */
	if (mkvars) {
		if (slbt_get_mkvars_flags(&ctx->ctx,mkvars,&lflags) < 0)
			return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

		if (ctx->cctx.host.host && !cfgmeta_host)
			cfgmeta_host = cfgmkvars;

		if (ctx->cctx.host.ar && !cfgmeta_ar)
			cfgmeta_ar = cfgmkvars;

		if (ctx->cctx.host.as && !cfgmeta_as)
			cfgmeta_as = cfgmkvars;

		if (ctx->cctx.host.nm && !cfgmeta_nm)
			cfgmeta_nm = cfgmkvars;

		if (ctx->cctx.host.ranlib && !cfgmeta_ranlib)
			cfgmeta_ranlib = cfgmkvars;

		if (ctx->cctx.host.dlltool && !cfgmeta_dlltool)
			cfgmeta_dlltool = cfgmkvars;

		if (cmdnoshared)
			lflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;

		if (cmdnostatic)
			if (lflags & SLBT_DRIVER_DISABLE_SHARED)
				cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;

		cctx.drvflags |= lflags;

		/* -disable-static? */
		if (cctx.drvflags & SLBT_DRIVER_DISABLE_STATIC)
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_STATIC;

		/* -disable-shared? */
		if (cctx.drvflags & SLBT_DRIVER_DISABLE_SHARED)
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SHARED;

		ctx->cctx.drvflags = cctx.drvflags;
	}

	/* heuristics */
	if (mkvars)
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_HEURISTICS;

	if (cctx.drvflags & SLBT_DRIVER_HEURISTICS) {
		if (slbt_get_lconf_flags(&ctx->ctx,lconf,&lflags,false) < 0)
			if (!(cctx.drvflags & SLBT_DRIVER_OUTPUT_MASK))
				return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);
	} else {
		switch (cctx.mode) {
			case SLBT_MODE_UNKNOWN:
			case SLBT_MODE_STOOLIE:
				break;

			case SLBT_MODE_CONFIG:
				lconf = mkvars ? 0 : "slibtool.cfg";
				break;

			default:
				lconf = "slibtool.cfg";
				break;
		}

		if (lconf && (errinfp = ctx->errinfp))
			if (slbt_get_lconf_flags(&ctx->ctx,lconf,&lflags,true) < 0)
				for (ctx->errinfp=errinfp; *errinfp; errinfp++)
					*errinfp = 0;

		if (ctx->lconfctx)
			cctx.drvflags |= SLBT_DRIVER_HEURISTICS;
	}

	if (cctx.drvflags & SLBT_DRIVER_HEURISTICS) {
		if (ctx->cctx.host.host && !cfgmeta_host)
			cfgmeta_host = cfglconf;

		if (ctx->cctx.host.ar && !cfgmeta_ar)
			cfgmeta_ar = cfglconf;

		if (ctx->cctx.host.as && !cfgmeta_as)
			cfgmeta_as = cfglconf;

		if (ctx->cctx.host.nm && !cfgmeta_nm)
			cfgmeta_nm = cfglconf;

		if (ctx->cctx.host.ranlib && !cfgmeta_ranlib)
			cfgmeta_ranlib = cfglconf;

		if (ctx->cctx.host.dlltool && !cfgmeta_dlltool)
			cfgmeta_dlltool = cfglconf;

		if (cmdnoshared)
			lflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;

		if (cmdnostatic)
			if (lflags & SLBT_DRIVER_DISABLE_SHARED)
				cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;

		cctx.drvflags |= lflags;

		/* -disable-static? */
		if (cctx.drvflags & SLBT_DRIVER_DISABLE_STATIC)
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_STATIC;

		/* -disable-shared? */
		if (cctx.drvflags & SLBT_DRIVER_DISABLE_SHARED)
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SHARED;

		ctx->cctx.drvflags = cctx.drvflags;
	}

	/* host params */
	if (slbt_init_host_params(
			&ctx->ctx,
			&ctx->cctx,
			&ctx->host,
			&ctx->cctx.host,
			&ctx->cctx.cfgmeta,
			cfgmeta_host,
			cfgmeta_ar,
			cfgmeta_as,
			cfgmeta_nm,
			cfgmeta_ranlib,
			cfgmeta_dlltool))
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	/* host tool arguments */
	if (slbt_driver_parse_tool_argv(ctx->cctx.host.ar,&ctx->host.ar_argv) < 0)
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	if (slbt_driver_parse_tool_argv(ctx->cctx.host.nm,&ctx->host.nm_argv) < 0)
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	if (slbt_driver_parse_tool_argv(ctx->cctx.host.ranlib,&ctx->host.ranlib_argv) < 0)
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	if (slbt_driver_parse_tool_argv(ctx->cctx.host.as,&ctx->host.as_argv) < 0)
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	if (slbt_driver_parse_tool_argv(ctx->cctx.host.dlltool,&ctx->host.dlltool_argv) < 0)
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	if (slbt_driver_parse_tool_argv(ctx->cctx.host.mdso,&ctx->host.mdso_argv) < 0)
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	/* flavor settings */
	slbt_init_flavor_settings(
		&ctx->cctx,0,
		&ctx->cctx.settings);

	/* ldpath */
	if (slbt_init_ldrpath(&ctx->cctx,&ctx->cctx.host))
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	/* version info */
	if (slbt_init_version_info(ctx,&ctx->cctx.verinfo))
		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	/* link params */
	if (cctx.mode == SLBT_MODE_LINK)
		if (slbt_init_link_params(ctx))
			return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);

	/* dlpreopen */
	if ((dlopenv = ctx->dlopenv)) {
		for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
			if (entry->fopt) {
				switch (entry->tag) {
					case TAG_DLOPEN:
						if (!strcmp(entry->arg,"self")) {
							ctx->cctx.drvflags |= SLBT_DRIVER_DLOPEN_FORCE;

						} else if (!strcmp(entry->arg,"force")) {
							ctx->cctx.drvflags |= SLBT_DRIVER_DLOPEN_FORCE;

						} else {
							*dlopenv++ = entry;
						}

						break;

					case TAG_DLPREOPEN:
						if (!strcmp(entry->arg,"self")) {
							ctx->cctx.drvflags |= SLBT_DRIVER_DLPREOPEN_SELF;

						} else if (!strcmp(entry->arg,"force")) {
							ctx->cctx.drvflags |= SLBT_DRIVER_DLPREOPEN_FORCE;

						} else {
							*dlopenv++ = entry;
						}

						break;

					default:
						break;
				}
			}
		}
	}

	/* -dlopen & -dlpreopen semantic validation */
	uint64_t fmask;

	fmask  = SLBT_DRIVER_DLOPEN_SELF  | SLBT_DRIVER_DLPREOPEN_SELF;
	fmask |= SLBT_DRIVER_DLOPEN_FORCE | SLBT_DRIVER_DLPREOPEN_FORCE;

	if (ctx->cctx.libname && (cctx.drvflags & fmask)) {
		slbt_dprintf(ctx->fdctx.fderr,
			"%s: error: -dlopen/-dlpreopen: "
			"the special 'self' and 'force' arguments "
			"may only be used when linking a program.\n",
			ctx->ctx.program);

		return slbt_lib_get_driver_ctx_fail(&ctx->ctx,0);
	}

	/* all ready */
	*pctx = &ctx->ctx;

	return 0;
}


static void slbt_lib_free_driver_ctx_impl(struct slbt_driver_ctx_alloc * ictx)
{
	struct slbt_error_info ** perr;
	struct slbt_error_info *  erri;
	struct slbt_obj_list *    objlistp;

	for (perr=ictx->ctx.errinfp; *perr; perr++) {
		erri = *perr;

		if (erri->eany && (erri->esyscode == ENOENT))
			free(erri->eany);
	}

	if (ictx->ctx.libname)
		free(ictx->ctx.libname);

	if (ictx->ctx.dlopenv)
		free(ictx->ctx.dlopenv);

	if (ictx->ctx.lconf.addr)
		munmap(
			ictx->ctx.lconf.addr,
			ictx->ctx.lconf.size);

	if (ictx->ctx.lconfctx)
		slbt_lib_free_txtfile_ctx(ictx->ctx.lconfctx);

	if (ictx->ctx.mkvarsctx)
		slbt_lib_free_txtfile_ctx(ictx->ctx.mkvarsctx);

	for (objlistp=ictx->ctx.objlistv; objlistp->name; objlistp++) {
		free(objlistp->objv);
		free(objlistp->addr);
	}

	free(ictx->ctx.objlistv);

	free(ictx->ctx.dargs);
	free(ictx->ctx.dargv);
	free(ictx->ctx.targv);

	slbt_free_host_params(&ictx->ctx.host);
	slbt_free_host_params(&ictx->ctx.ahost);
	argv_free(ictx->ctx.meta);

	free(ictx);
}


void slbt_lib_free_driver_ctx(struct slbt_driver_ctx * ctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	if (ctx) {
		addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_impl,ctx);
		addr = addr - offsetof(struct slbt_driver_ctx_alloc,ctx);
		ictx = (struct slbt_driver_ctx_alloc *)addr;
		slbt_lib_free_driver_ctx_impl(ictx);
	}
}


const struct slbt_source_version * slbt_api_source_version(void)
{
	return &slbt_src_version;
}


int slbt_lib_get_driver_fdctx(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_fd_ctx *		fdctx)
{
	struct slbt_driver_ctx_impl *	ictx;

	ictx = slbt_get_driver_ictx(dctx);

	fdctx->fdin  = ictx->fdctx.fdin;
	fdctx->fdout = ictx->fdctx.fdout;
	fdctx->fderr = ictx->fdctx.fderr;
	fdctx->fdlog = ictx->fdctx.fdlog;
	fdctx->fdcwd = ictx->fdctx.fdcwd;
	fdctx->fddst = ictx->fdctx.fddst;

	return 0;
}


int slbt_lib_set_driver_fdctx(
	struct slbt_driver_ctx *	dctx,
	const struct slbt_fd_ctx *	fdctx)
{
	struct slbt_driver_ctx_impl *	ictx;

	ictx = slbt_get_driver_ictx(dctx);

	ictx->fdctx.fdin  = fdctx->fdin;
	ictx->fdctx.fdout = fdctx->fdout;
	ictx->fdctx.fderr = fdctx->fderr;
	ictx->fdctx.fdlog = fdctx->fdlog;
	ictx->fdctx.fdcwd = fdctx->fdcwd;
	ictx->fdctx.fddst = fdctx->fddst;

	return 0;
}
