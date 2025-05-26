/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2025  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_spawn_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_visibility_impl.h"
#include "slibtool_ar_impl.h"

/* preferred native tools, specified at the time of configuration for the host */
static const char * slbt_native_ar_variant     = SLBT_PREFERRED_HOST_AR;
static const char * slbt_native_as_variant     = SLBT_PREFERRED_HOST_AS;
static const char * slbt_native_nm_variant     = SLBT_PREFERRED_HOST_NM;
static const char * slbt_native_ranlib_variant = SLBT_PREFERRED_HOST_RANLIB;

/* annotation strings */
static const char cfgexplicit[] = "command-line argument";
static const char cfghost[]     = "derived from <host>";
static const char cfgar[]       = "derived from <ar>";
static const char cfgranlib[]   = "derived from <ranlib>";
static const char cfgtarget[]   = "derived from <target>";
static const char cfgcompiler[] = "derived from <compiler>";
static const char cfgnmachine[] = "native (cached in ccenv/host.mk)";
static const char cfgxmachine[] = "foreign (derived from -dumpmachine)";
static const char cfguseropt[]  = "native (preferred alternative)";
static const char cfgnative[]   = "native (system default)";

static void slbt_get_host_quad(
	char *	hostbuf,
	char ** hostquad)
{
	char *	mark;
	char *	ch;
	int	i;

	for (i=0, ch=hostbuf, mark=hostbuf; *ch && i<4; ch++) {
		if (*ch == '-') {
			*ch = 0;
			hostquad[i++] = mark;
			mark = &ch[1];
		}
	}

	if (i<4)
		hostquad[i] = mark;

	if (i==3) {
		hostquad[1] = hostquad[2];
		hostquad[2] = hostquad[3];
		hostquad[3] = 0;
	}
}


static void slbt_spawn_ar(char ** argv, int * ecode)
{
	int	estatus;
	pid_t	pid;

	*ecode = 127;

	if ((pid = slbt_fork()) < 0) {
		return;

	} else if (pid == 0) {
		execvp(argv[0],argv);
		_exit(errno);

	} else {
		waitpid(pid,&estatus,0);

		if (WIFEXITED(estatus))
			*ecode = WEXITSTATUS(estatus);
	}
}


slbt_hidden int slbt_init_host_params(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_common_ctx *	cctx,
	struct slbt_host_strs *		drvhost,
	struct slbt_host_params *	host,
	struct slbt_host_params *	cfgmeta,
	const char *                    cfgmeta_host,
	const char *                    cfgmeta_ar,
	const char *                    cfgmeta_as,
	const char *                    cfgmeta_nm,
	const char *                    cfgmeta_ranlib,
	const char *                    cfgmeta_dlltool)
{
	int		fdcwd;
	int		arprobe;
	int		arfd;
	int		ecode;
	int             cint;
	size_t		toollen;
	size_t          altlen;
	char *		dash;
	char *		base;
	char *		mark;
	const char *	machine;
	bool		ftarget       = false;
	bool		fhost         = false;
	bool		fcompiler     = false;
	bool		fnative       = false;
	bool		fnativear     = false;
	bool		fdumpmachine  = false;
	char		buf        [256];
	char		hostbuf    [256];
	char		machinebuf [256];
	char *		hostquad   [4];
	char *		machinequad[4];
	char *		arprobeargv[4];
	char		archivename[] = "/tmp/slibtool.ar.probe.XXXXXXXXXXXXXXXX";

	/* base */
	if ((base = strrchr(cctx->cargv[0],'/')))
		base++;
	else
		base = cctx->cargv[0];

	fdumpmachine  = (cctx->mode == SLBT_MODE_COMPILE)
			|| (cctx->mode == SLBT_MODE_LINK)
			|| (cctx->mode == SLBT_MODE_INFO);

	fdumpmachine &= (!strcmp(base,"xgcc")
			|| !strcmp(base,"xg++"));

	/* support the portbld <--> unknown synonym */
	if (!(drvhost->machine = strdup(SLBT_MACHINE)))
		return -1;

	if ((mark = strstr(drvhost->machine,"-portbld-")))
		memcpy(mark,"-unknown",8);

	/* host */
	if (host->host) {
		cfgmeta->host = cfgmeta_host ? cfgmeta_host : cfgexplicit;
		fhost         = true;

	} else if (cctx->target) {
		host->host    = cctx->target;
		cfgmeta->host = cfgtarget;
		ftarget       = true;

	} else if (!strcmp(base,"slibtool-ar")) {
		host->host    = drvhost->machine;
		cfgmeta->host = cfgnmachine;
		fnative       = true;

	} else if (strrchr(base,'-')) {
		if (!(drvhost->host = strdup(cctx->cargv[0])))
			return -1;

		dash          = strrchr(drvhost->host,'-');
		*dash         = 0;
		host->host    = drvhost->host;
		cfgmeta->host = cfgcompiler;
		fcompiler     = true;

	} else if (!fdumpmachine) {
		host->host    = drvhost->machine;
		cfgmeta->host = cfgnmachine;

	} else if (slbt_util_dump_machine(cctx->cargv[0],buf,sizeof(buf)) < 0) {
		if (dctx)
			slbt_dprintf(
				slbt_driver_fderr(dctx),
				"%s: could not determine host "
				"via -dumpmachine\n",
				dctx->program);
		return -1;

	} else {
		if (!(drvhost->host = strdup(buf)))
			return -1;

		host->host    = drvhost->host;
		fcompiler     = true;
		fnative       = !strcmp(host->host,drvhost->machine);
		cfgmeta->host = fnative ? cfgnmachine : cfgxmachine;

		if (!fnative) {
			strcpy(hostbuf,host->host);
			strcpy(machinebuf,drvhost->machine);

			slbt_get_host_quad(hostbuf,hostquad);
			slbt_get_host_quad(machinebuf,machinequad);

			if (hostquad[2] && machinequad[2])
				fnative = !strcmp(hostquad[0],machinequad[0])
					&& !strcmp(hostquad[1],machinequad[1])
					&& !strcmp(hostquad[2],machinequad[2]);
		}
	}

	/* flavor */
	if (host->flavor) {
		cfgmeta->flavor = cfgexplicit;
	} else {
		if (fhost) {
			machine         = host->host;
			cfgmeta->flavor = cfghost;
		} else if (ftarget) {
			machine         = cctx->target;
			cfgmeta->flavor = cfgtarget;
		} else if (fcompiler) {
			machine         = drvhost->host;
			cfgmeta->flavor = cfgcompiler;
		} else {
			machine         = drvhost->machine;
			cfgmeta->flavor = cfgnmachine;
		}

		dash = strrchr(machine,'-');
		cfgmeta->flavor = cfghost;

		if ((dash && !strcmp(dash,"-bsd")) || strstr(machine,"-bsd-"))
			host->flavor = "bsd";
		else if ((dash && !strcmp(dash,"-cygwin")) || strstr(machine,"-cygwin-"))
			host->flavor = "cygwin";
		else if ((dash && !strcmp(dash,"-msys")) || strstr(machine,"-msys-"))
			host->flavor = "msys";
		else if ((dash && !strcmp(dash,"-darwin")) || strstr(machine,"-darwin"))
			host->flavor = "darwin";
		else if ((dash && !strcmp(dash,"-linux")) || strstr(machine,"-linux-"))
			host->flavor = "linux";
		else if ((dash && !strcmp(dash,"-midipix")) || strstr(machine,"-midipix-"))
			host->flavor = "midipix";
		else if ((dash && !strcmp(dash,"-mingw")) || strstr(machine,"-mingw-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-mingw32")) || strstr(machine,"-mingw32-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-mingw64")) || strstr(machine,"-mingw64-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-windows")) || strstr(machine,"-windows-"))
			host->flavor = "mingw";
		else {
			host->flavor   = "default";
			cfgmeta->flavor = "fallback, unverified";
		}

		if (fcompiler && !fnative)
			if ((mark = strstr(drvhost->machine,host->flavor)))
				if (mark > drvhost->machine)
					fnative = (*--mark == '-');
	}

	/* toollen */
	toollen =  fnative ? 0 : strlen(host->host);
	toollen += strlen("-utility-name");
	toollen += host->ranlib ? strlen(host->ranlib) : 0;

	if (fnative && slbt_native_ar_variant)
		if ((altlen = strlen(slbt_native_ar_variant)) > toollen)
			toollen = altlen;

	if (fnative && slbt_native_as_variant)
		if ((altlen = strlen(slbt_native_as_variant)) > toollen)
			toollen = altlen;

	if (fnative && slbt_native_nm_variant)
		if ((altlen = strlen(slbt_native_nm_variant)) > toollen)
			toollen = altlen;

	if (fnative && slbt_native_ranlib_variant)
		if ((altlen = strlen(slbt_native_ranlib_variant)) > toollen)
			toollen = altlen;

	/* ar */
	if (host->ar)
		cfgmeta->ar = cfgmeta_ar ? cfgmeta_ar : cfgexplicit;
	else {
		if (!(drvhost->ar = calloc(1,toollen)))
			return -1;

		if (fnative && slbt_native_ar_variant) {
			strcpy(drvhost->ar,slbt_native_ar_variant);
			cfgmeta->ar = cfguseropt;
			arprobe = 0;
		} else if (fnative) {
			strcpy(drvhost->ar,"ar");
			cfgmeta->ar = cfgnative;
			arprobe = 0;
		} else if (cctx->mode == SLBT_MODE_LINK) {
			arprobe = true;
		} else if (cctx->mode == SLBT_MODE_INFO) {
			arprobe = true;
		} else {
			arprobe = false;
		}

		/* arprobe */
		if (arprobe) {
			sprintf(drvhost->ar,"%s-ar",host->host);
			cfgmeta->ar = cfghost;
			ecode       = 127;

			/* empty archive */
			if ((arfd = mkstemp(archivename)) >= 0) {
				slbt_dprintf(arfd,"!<arch>\n");

				arprobeargv[0] = drvhost->ar;
				arprobeargv[1] = "-t";
				arprobeargv[2] = archivename;
				arprobeargv[3] = 0;

				/* <target>-ar */
				slbt_spawn_ar(
					arprobeargv,
					&ecode);
			}

			/* <target>-<compiler>-ar */
			if (ecode && !strchr(base,'-')) {
				sprintf(drvhost->ar,"%s-%s-ar",host->host,base);

				slbt_spawn_ar(
					arprobeargv,
					&ecode);
			}

			/* <compiler>-ar */
			if (ecode && !strchr(base,'-')) {
				sprintf(drvhost->ar,"%s-ar",base);

				slbt_spawn_ar(
					arprobeargv,
					&ecode);
			}

			/* if target is the native target, fallback to native ar */
			if ((fnative = (ecode && !strcmp(host->host,SLBT_MACHINE)))) {
				if (slbt_native_ar_variant) {
					strcpy(drvhost->ar,slbt_native_ar_variant);
					cfgmeta->ar = cfguseropt;
				} else {
					strcpy(drvhost->ar,"ar");
					cfgmeta->ar = cfgnative;
				}
			}

			/* fdcwd */
			fdcwd = slbt_driver_fdcwd(dctx);

			/* clean up */
			if (arfd >= 0) {
				unlinkat(fdcwd,archivename,0);
				close(arfd);
			}
		}

		host->ar = drvhost->ar;
	}

	if (!fnative && slbt_native_ar_variant) {
		if (!strcmp(host->ar,slbt_native_ar_variant)) {
			fnative   = true;
			fnativear = true;
		}
	}

	if (!fnative && !strncmp(host->ar,"ar",2)) {
		if (!host->ar[2] || isspace((cint = host->ar[2]))) {
			fnative   = true;
			fnativear = true;
		}
	}

	/* as */
	if (host->as)
		cfgmeta->as = cfgmeta_as ? cfgmeta_as : cfgexplicit;
	else {
		if (!(drvhost->as = calloc(1,toollen)))
			return -1;

		if (fnative && slbt_native_as_variant) {
			strcpy(drvhost->as,slbt_native_as_variant);
			cfgmeta->as = cfguseropt;
		} else if (fnative) {
			strcpy(drvhost->as,"as");
			cfgmeta->as = fnativear ? cfgar : cfgnative;
		} else {
			sprintf(drvhost->as,"%s-as",host->host);
			cfgmeta->as = cfghost;
		}

		if (host->ranlib && (mark = strrchr(host->ranlib,'/'))) {
			if (strcmp(++mark,"ranlib"))
				if ((mark = strrchr(mark,'-')))
					if (strcmp(++mark,"ranlib"))
						mark = 0;

			if (mark) {
				strcpy(drvhost->as,host->ranlib);
				strcpy(&drvhost->as[mark-host->ranlib],"as");
				cfgmeta->as = cfgranlib;
			}
		}

		host->as = drvhost->as;
	}

	/* nm */
	if (host->nm)
		cfgmeta->nm = cfgmeta_nm ? cfgmeta_nm : cfgexplicit;
	else {
		if (!(drvhost->nm = calloc(1,toollen)))
			return -1;

		if (fnative && slbt_native_nm_variant) {
			strcpy(drvhost->nm,slbt_native_nm_variant);
			cfgmeta->nm = cfguseropt;
		} else if (fnative) {
			strcpy(drvhost->nm,"nm");
			cfgmeta->nm = fnativear ? cfgar : cfgnative;
		} else {
			sprintf(drvhost->nm,"%s-nm",host->host);
			cfgmeta->nm = cfghost;
		}

		host->nm = drvhost->nm;
	}

	/* ranlib */
	if (host->ranlib)
		cfgmeta->ranlib = cfgmeta_ranlib ? cfgmeta_ranlib : cfgexplicit;
	else {
		if (!(drvhost->ranlib = calloc(1,toollen)))
			return -1;

		if (fnative && slbt_native_ranlib_variant) {
			strcpy(drvhost->ranlib,slbt_native_ranlib_variant);
			cfgmeta->ranlib = cfguseropt;
		} else if (fnative) {
			strcpy(drvhost->ranlib,"ranlib");
			cfgmeta->ranlib = fnativear ? cfgar : cfgnative;
		} else {
			sprintf(drvhost->ranlib,"%s-ranlib",host->host);
			cfgmeta->ranlib = cfghost;
		}

		host->ranlib = drvhost->ranlib;
	}

	/* windres */
	if (host->windres)
		cfgmeta->windres = cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"msys")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->windres    = "";
		cfgmeta->windres = "not applicable";

	} else {
		if (!(drvhost->windres = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->windres,"windres");
			cfgmeta->windres = fnativear ? cfgar : cfgnative;
		} else {
			sprintf(drvhost->windres,"%s-windres",host->host);
			cfgmeta->windres = cfghost;
		}

		if ((mark = strrchr(host->ranlib,'/'))) {
			if (strcmp(++mark,"ranlib"))
				if ((mark = strrchr(mark,'-')))
					if (strcmp(++mark,"ranlib"))
						mark = 0;

			if (mark) {
				strcpy(drvhost->windres,host->ranlib);
				strcpy(&drvhost->windres[mark-host->ranlib],"windres");
				cfgmeta->windres = cfgranlib;
			}
		}

		host->windres = drvhost->windres;
	}

	/* dlltool */
	if (host->dlltool)
		cfgmeta->dlltool = cfgmeta_dlltool ? cfgmeta_dlltool : cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"msys")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->dlltool = "";
		cfgmeta->dlltool = "not applicable";

	} else {
		if (!(drvhost->dlltool = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->dlltool,"dlltool");
			cfgmeta->dlltool = fnativear ? cfgar : cfgnative;
		} else {
			sprintf(drvhost->dlltool,"%s-dlltool",host->host);
			cfgmeta->dlltool = cfghost;
		}

		if ((mark = strrchr(host->ranlib,'/'))) {
			if (strcmp(++mark,"ranlib"))
				if ((mark = strrchr(mark,'-')))
					if (strcmp(++mark,"ranlib"))
						mark = 0;

			if (mark) {
				strcpy(drvhost->dlltool,host->ranlib);
				strcpy(&drvhost->dlltool[mark-host->ranlib],"dlltool");
				cfgmeta->dlltool = cfgranlib;
			}
		}

		host->dlltool = drvhost->dlltool;
	}

	/* mdso */
	if (host->mdso)
		cfgmeta->mdso = cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"msys")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->mdso = "";
		cfgmeta->mdso = "not applicable";

	} else {
		if (!(drvhost->mdso = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->mdso,"mdso");
			cfgmeta->mdso = fnativear ? cfgar : cfgnative;
		} else {
			sprintf(drvhost->mdso,"%s-mdso",host->host);
			cfgmeta->mdso = cfghost;
		}

		host->mdso = drvhost->mdso;
	}

	return 0;
}

static void slbt_free_host_tool_argv(char ** argv)
{
	char ** parg;

	if (!argv)
		return;

	for (parg=argv; *parg; parg++)
		free(*parg);

	free(argv);
}

slbt_hidden void slbt_free_host_params(struct slbt_host_strs * host)
{
	if (host->machine)
		free(host->machine);

	if (host->host)
		free(host->host);

	if (host->flavor)
		free(host->flavor);

	if (host->ar)
		free(host->ar);

	if (host->as)
		free(host->as);

	if (host->nm)
		free(host->nm);

	if (host->ranlib)
		free(host->ranlib);

	if (host->windres)
		free(host->windres);

	if (host->dlltool)
		free(host->dlltool);

	if (host->mdso)
		free(host->mdso);

	slbt_free_host_tool_argv(host->ar_argv);
	slbt_free_host_tool_argv(host->as_argv);
	slbt_free_host_tool_argv(host->nm_argv);
	slbt_free_host_tool_argv(host->ranlib_argv);
	slbt_free_host_tool_argv(host->windres_argv);
	slbt_free_host_tool_argv(host->dlltool_argv);
	slbt_free_host_tool_argv(host->mdso_argv);

	memset(host,0,sizeof(*host));
}


void slbt_host_reset_althost(const struct slbt_driver_ctx * ctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_alloc,ctx);
	addr = addr - offsetof(struct slbt_driver_ctx_impl,ctx);
	ictx = (struct slbt_driver_ctx_alloc *)addr;

	slbt_free_host_params(&ictx->ctx.ahost);
}

int  slbt_host_set_althost(
	const struct slbt_driver_ctx *	ctx,
	const char *			host,
	const char *			flavor)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_alloc,ctx);
	addr = addr - offsetof(struct slbt_driver_ctx_impl,ctx);
	ictx = (struct slbt_driver_ctx_alloc *)addr;
	slbt_free_host_params(&ictx->ctx.ahost);

	if (!(ictx->ctx.ahost.host = strdup(host)))
		return SLBT_SYSTEM_ERROR(ctx,0);

	if (!(ictx->ctx.ahost.flavor = strdup(flavor))) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_SYSTEM_ERROR(ctx,0);
	}

	ictx->ctx.cctx.ahost.host   = ictx->ctx.ahost.host;
	ictx->ctx.cctx.ahost.flavor = ictx->ctx.ahost.flavor;

	if (slbt_init_host_params(
			0,
			ctx->cctx,
			&ictx->ctx.ahost,
			&ictx->ctx.cctx.ahost,
			&ictx->ctx.cctx.acfgmeta,
			0,0,0,0,0,0)) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_CUSTOM_ERROR(ctx,SLBT_ERR_HOST_INIT);
	}

	slbt_init_flavor_settings(
		&ictx->ctx.cctx,
		&ictx->ctx.cctx.ahost,
		&ictx->ctx.cctx.asettings);

	if (slbt_init_ldrpath(
			&ictx->ctx.cctx,
			&ictx->ctx.cctx.ahost)) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_CUSTOM_ERROR(ctx,SLBT_ERR_LDRPATH_INIT);
	}

	return 0;
}
