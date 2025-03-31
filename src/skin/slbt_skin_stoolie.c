#include "slibtool_driver_impl.h"
#include "slibtool_visibility_impl.h"
#include "argv/argv.h"

const slbt_hidden struct argv_option slbt_stoolie_options[] = {
	{"version",	0,TAG_STLE_VERSION,ARGV_OPTARG_NONE,0,0,0,
			"show version information"},

	{"help",	'h',TAG_STLE_HELP,ARGV_OPTARG_NONE,0,0,0,
			"display slibtoolize (stoolie) mode help"},

	{"copy",	'c',TAG_STLE_COPY,ARGV_OPTARG_NONE,0,0,0,
			"copy build-auxiliary m4 files "
			"(create symbolic links otherwise."},

	{"force",	'f',TAG_STLE_FORCE,ARGV_OPTARG_NONE,0,0,0,
			"replace existing build-auxiliary and m4 "
			"files and/or symbolic links."},

	{"install",	'i',TAG_STLE_INSTALL,ARGV_OPTARG_NONE,0,0,0,
			"create symbolic links to, or otherwise copy "
			"missing build-auxiliary and m4 files."},

	{"debug",	'd',TAG_STLE_DEBUG,ARGV_OPTARG_NONE,0,0,0,
			"display internal debug information."},

	{"dry-run",	'n',TAG_STLE_DRY_RUN,ARGV_OPTARG_NONE,0,0,0,
			"do not spawn any processes, "
			"do not make any changes to the file system."},

	{"automake",	0,TAG_STLE_SILENT,ARGV_OPTARG_NONE,0,0,0,
			"backward-compatible alias for --quiet."},

	{"quiet",	'q',TAG_STLE_SILENT,ARGV_OPTARG_NONE,0,0,0,
			"do not say anything."},

	{"silent",	's',TAG_STLE_SILENT,ARGV_OPTARG_NONE,0,0,0,
			"say absolutely nothing."},

	{"verbose",	'v',TAG_STLE_VERBOSE,ARGV_OPTARG_NONE,0,0,0,
			"generate lots of informational messages "
			"that nobody can understand."},

	{"warnings",	'W',TAG_STLE_WARNINGS,ARGV_OPTARG_REQUIRED,0,
			"all|none|error|doubt|concern|environment|file",0,
			"specify category of warnings to display (or not)."},

	{"no-warnings",	0,TAG_STLE_NO_WARNINGS,ARGV_OPTARG_NONE,0,0,0,
			"suppress all warning messages (or not)."},

	{"ltdl",	0,TAG_STLE_LTDL,ARGV_OPTARG_OPTIONAL,0,0,"<dir>",
			"install [s]ltdl sources to %s; this option "
			"provided is provided for the purpose of backward "
			"compatibility, and is currently a no-op, thereby "
			"deferring -lltdl to the system installed library."},

	{"system-ltdl",0,TAG_STLE_SYSTEM_LTDL,ARGV_OPTARG_NONE,0,0,0,
			"Do not enforce the use of libsltdl. "
			"Without this option, --prefer-sltdl will be added to "
			"slibtool's configured arguments."},

	{0,0,0,0,0,0,0,0}
};
