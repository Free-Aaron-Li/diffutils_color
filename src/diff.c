/* GNU diff - compare files line by line

   Copyright (C) 1988-1989, 1992-1994, 1996, 1998, 2001-2002, 2004, 2006-2007,
   2009-2013, 2015-2021 Free Software Foundation, Inc.

   This file is part of GNU DIFF.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#define GDIFF_MAIN
#include "diff.h"
#include "die.h"
#include <assert.h>
#include "paths.h"
#include <c-stack.h>
#include <dirname.h>
#include <error.h>
#include <exclude.h>
#include <exitfail.h>
#include <filenamecat.h>
#include <file-type.h>
#include <fnmatch.h>
#include <getopt.h>
#include <hard-locale.h>
#include <prepargs.h>
#include <progname.h>
#include <sh-quote.h>
#include <stat-time.h>
#include <timespec.h>
#include <version-etc.h>
#include <xalloc.h>
#include <xreadlink.h>
#include <xstdopen.h>
#include <binary-io.h>

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "diff"

#define AUTHORS \
  proper_name ("Paul Eggert"), \
  proper_name ("Mike Haertel"), \
  proper_name ("David Hayes"), \
  proper_name ("Richard Stallman"), \
  proper_name ("Len Tower")

#ifndef GUTTER_WIDTH_MINIMUM
# define GUTTER_WIDTH_MINIMUM 3
#endif

struct regexp_list {
    char *regexps; /* chars representing disjunction of the regexps */
    size_t len; /* chars used in 'regexps' */
    size_t size; /* size malloc'ed for 'regexps'; 0 if not malloc'ed */
    bool multiple_regexps; /* Does 'regexps' represent a disjunction?  */
    struct re_pattern_buffer *buf;
};

static int compare_files(struct comparison const *, char const *, char const *);

static void add_regexp(struct regexp_list *, char const *);

static void summarize_regexp_list(struct regexp_list *);

static void specify_style(enum output_style);

static void specify_value(char const **, char const *, char const *);

static void specify_colors_style(char const *);

static void try_help(char const *, char const *) __attribute__((noreturn));

static void check_stdout(void);

static void usage(void);

/* If comparing directories, compare their common subdirectories
   recursively.  */
static bool recursive;

/* In context diffs, show previous lines that match these regexps.  */
static struct regexp_list function_regexp_list;

/* Ignore changes affecting only lines that match these regexps.  */
static struct regexp_list ignore_regexp_list;

#if O_BINARY
/* Use binary I/O when reading and writing data (--binary).
   On POSIX hosts, this has no effect.  */
static bool binary;
#else
enum { binary = true };
#endif

/* If one file is missing, treat it as present but empty (-N).  */
static bool new_file;

/* If the first file is missing, treat it as present but empty
   (--unidirectional-new-file).  */
static bool unidirectional_new_file;

/* Report files compared that are the same (-s).
   Normally nothing is output when that happens.  */
static bool report_identical_files;

static char const shortopts[] =
        "0123456789abBcC:dD:eEfF:hHiI:lL:nNpPqrsS:tTuU:vwW:x:X:yZ";

/* Values for long options that do not have single-letter equivalents.  */
enum {
    BINARY_OPTION = CHAR_MAX + 1,
    FROM_FILE_OPTION,
    HELP_OPTION,
    HORIZON_LINES_OPTION,
    IGNORE_FILE_NAME_CASE_OPTION,
    INHIBIT_HUNK_MERGE_OPTION,
    LEFT_COLUMN_OPTION,
    LINE_FORMAT_OPTION,
    NO_DEREFERENCE_OPTION,
    NO_IGNORE_FILE_NAME_CASE_OPTION,
    NORMAL_OPTION,
    SDIFF_MERGE_ASSIST_OPTION,
    STRIP_TRAILING_CR_OPTION,
    SUPPRESS_BLANK_EMPTY_OPTION,
    SUPPRESS_COMMON_LINES_OPTION,
    TABSIZE_OPTION,
    TO_FILE_OPTION,

    /* These options must be in sequence.  */
    UNCHANGED_LINE_FORMAT_OPTION,
    OLD_LINE_FORMAT_OPTION,
    NEW_LINE_FORMAT_OPTION,

    /* These options must be in sequence.  */
    UNCHANGED_GROUP_FORMAT_OPTION,
    OLD_GROUP_FORMAT_OPTION,
    NEW_GROUP_FORMAT_OPTION,
    CHANGED_GROUP_FORMAT_OPTION,

    COLOR_OPTION,
    COLOR_PALETTE_OPTION,

    PRESUME_OUTPUT_TTY_OPTION,
};

static char const group_format_option[][sizeof "--unchanged-group-format"] =
{
    "--unchanged-group-format",
    "--old-group-format",
    "--new-group-format",
    "--changed-group-format"
};

static char const line_format_option[][sizeof "--unchanged-line-format"] =
{
    "--unchanged-line-format",
    "--old-line-format",
    "--new-line-format"
};

static struct option const longopts[] =
{
    {"binary", 0, 0, BINARY_OPTION},
    {"brief", 0, 0, 'q'},
    {"changed-group-format", 1, 0, CHANGED_GROUP_FORMAT_OPTION},
    {"color", 2, 0, COLOR_OPTION},
    {"context", 2, 0, 'C'},
    {"ed", 0, 0, 'e'},
    {"exclude", 1, 0, 'x'},
    {"exclude-from", 1, 0, 'X'},
    {"expand-tabs", 0, 0, 't'},
    {"forward-ed", 0, 0, 'f'},
    {"from-file", 1, 0, FROM_FILE_OPTION},
    {"help", 0, 0, HELP_OPTION},
    {"horizon-lines", 1, 0, HORIZON_LINES_OPTION},
    {"ifdef", 1, 0, 'D'},
    {"ignore-all-space", 0, 0, 'w'},
    {"ignore-blank-lines", 0, 0, 'B'},
    {"ignore-case", 0, 0, 'i'},
    {"ignore-file-name-case", 0, 0, IGNORE_FILE_NAME_CASE_OPTION},
    {"ignore-matching-lines", 1, 0, 'I'},
    {"ignore-space-change", 0, 0, 'b'},
    {"ignore-tab-expansion", 0, 0, 'E'},
    {"ignore-trailing-space", 0, 0, 'Z'},
    {"inhibit-hunk-merge", 0, 0, INHIBIT_HUNK_MERGE_OPTION},
    {"initial-tab", 0, 0, 'T'},
    {"label", 1, 0, 'L'},
    {"left-column", 0, 0, LEFT_COLUMN_OPTION},
    {"line-format", 1, 0, LINE_FORMAT_OPTION},
    {"minimal", 0, 0, 'd'},
    {"new-file", 0, 0, 'N'},
    {"new-group-format", 1, 0, NEW_GROUP_FORMAT_OPTION},
    {"new-line-format", 1, 0, NEW_LINE_FORMAT_OPTION},
    {"no-dereference", 0, 0, NO_DEREFERENCE_OPTION},
    {"no-ignore-file-name-case", 0, 0, NO_IGNORE_FILE_NAME_CASE_OPTION},
    {"normal", 0, 0, NORMAL_OPTION},
    {"old-group-format", 1, 0, OLD_GROUP_FORMAT_OPTION},
    {"old-line-format", 1, 0, OLD_LINE_FORMAT_OPTION},
    {"paginate", 0, 0, 'l'},
    {"palette", 1, 0, COLOR_PALETTE_OPTION},
    {"rcs", 0, 0, 'n'},
    {"recursive", 0, 0, 'r'},
    {"report-identical-files", 0, 0, 's'},
    {"sdiff-merge-assist", 0, 0, SDIFF_MERGE_ASSIST_OPTION},
    {"show-c-function", 0, 0, 'p'},
    {"show-function-line", 1, 0, 'F'},
    {"side-by-side", 0, 0, 'y'},
    {"speed-large-files", 0, 0, 'H'},
    {"starting-file", 1, 0, 'S'},
    {"strip-trailing-cr", 0, 0, STRIP_TRAILING_CR_OPTION},
    {"suppress-blank-empty", 0, 0, SUPPRESS_BLANK_EMPTY_OPTION},
    {"suppress-common-lines", 0, 0, SUPPRESS_COMMON_LINES_OPTION},
    {"tabsize", 1, 0, TABSIZE_OPTION},
    {"text", 0, 0, 'a'},
    {"to-file", 1, 0, TO_FILE_OPTION},
    {"unchanged-group-format", 1, 0, UNCHANGED_GROUP_FORMAT_OPTION},
    {"unchanged-line-format", 1, 0, UNCHANGED_LINE_FORMAT_OPTION},
    {"unidirectional-new-file", 0, 0, 'P'},
    {"unified", 2, 0, 'U'},
    {"version", 0, 0, 'v'},
    {"width", 1, 0, 'W'},

    /* This is solely for testing.  Do not document.  */
    {"-presume-output-tty", no_argument, NULL, PRESUME_OUTPUT_TTY_OPTION},
    {0, 0, 0, 0}
};

/* Return a string containing the command options with which diff was invoked.
   Spaces appear between what were separate ARGV-elements.
   There is a space at the beginning but none at the end.
   If there were no options, the result is an empty string.

   Arguments: OPTIONVEC, a vector containing separate ARGV-elements, and COUNT,
   the length of that vector.  */

static char *
option_list(char **optionvec, int count) {
    int i;
    size_t size = 1;
    char *result;
    char *p;

    for (i = 0; i < count; i++)
        size += 1 + shell_quote_length(optionvec[i]);

    p = result = xmalloc(size);

    for (i = 0; i < count; i++) {
        *p++ = ' ';
        p = shell_quote_copy(p, optionvec[i]);
    }

    *p = '\0';
    return result;
}


/* Return an option value suitable for add_exclude.  */

static int
exclude_options(void) {
    return EXCLUDE_WILDCARDS | (ignore_file_name_case ? FNM_CASEFOLD : 0);
}

int
main(int argc, char **argv) {
    int exit_status = EXIT_SUCCESS;
    int c;
    int i;
    int prev = -1;
    lin ocontext = -1;
    bool explicit_context = false;
    size_t width = 0;
    bool show_c_function = false;
    char const *from_file = NULL;
    char const *to_file = NULL;
    intmax_t numval;
    char *numend;

    /* Do our initializations.  */
    exit_failure = EXIT_TROUBLE;
    initialize_main(&argc, &argv);
    set_program_name(argv[0]); // 设置程序名称
    setlocale(LC_ALL, ""); // 设置区域语言
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
    c_stack_action(0);
    function_regexp_list.buf = &function_regexp;
    ignore_regexp_list.buf = &ignore_regexp;
    re_set_syntax(RE_SYNTAX_GREP | RE_NO_POSIX_BACKTRACKING);
    excluded = new_exclude();
    presume_output_tty = false;
    xstdopen();

    /* Decode the options.  */

    while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
        switch (c) {
            case 0:
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                ocontext = (!ISDIGIT(prev)
                                ? c - '0'
                                : (ocontext - (c - '0' <= CONTEXT_MAX % 10)
                                   < CONTEXT_MAX / 10)
                                      ? 10 * ocontext + (c - '0')
                                      : CONTEXT_MAX);
                break;

            case 'a':
                text = true;
                break;

            case 'b':
                if (ignore_white_space < IGNORE_SPACE_CHANGE)
                    ignore_white_space = IGNORE_SPACE_CHANGE;
                break;

            case 'Z':
                if (ignore_white_space < IGNORE_SPACE_CHANGE)
                    ignore_white_space |= IGNORE_TRAILING_SPACE;
                break;

            case 'B':
                ignore_blank_lines = true;
                break;

            case 'C':
            case 'U': {
                if (optarg) {
                    numval = strtoimax(optarg, &numend, 10);
                    if (*numend || numval < 0)
                        try_help("invalid context length '%s'", optarg);
                    if (CONTEXT_MAX < numval)
                        numval = CONTEXT_MAX;
                } else
                    numval = 3;

                specify_style(c == 'U' ? OUTPUT_UNIFIED : OUTPUT_CONTEXT);
                if (context < numval)
                    context = numval;
                explicit_context = true;
            }
            break;

            case 'c':
                specify_style(OUTPUT_CONTEXT);
                if (context < 3)
                    context = 3;
                break;

            case 'd':
                minimal = true;
                break;

            case 'D':
                specify_style(OUTPUT_IFDEF); {
                    static char const C_ifdef_group_formats[] =
                            "%%=%c#ifndef %s\n%%<#endif /* ! %s */\n%c#ifdef %s\n%%>#endif /* %s */\n%c#ifndef %s\n%%<#else /* %s */\n%%>#endif /* %s */\n";
                    char *b = xmalloc(sizeof C_ifdef_group_formats
                                      + 7 * strlen(optarg) - 14 /* 7*"%s" */
                                      - 8 /* 5*"%%" + 3*"%c" */);
                    sprintf(b, C_ifdef_group_formats,
                            0,
                            optarg, optarg, 0,
                            optarg, optarg, 0,
                            optarg, optarg, optarg);
                    for (i = 0; i < sizeof group_format / sizeof group_format[0]; i++) {
                        specify_value(&group_format[i], b, "-D");
                        b += strlen(b) + 1;
                    }
                }
                break;

            case 'e':
                specify_style(OUTPUT_ED);
                break;

            case 'E':
                if (ignore_white_space < IGNORE_SPACE_CHANGE)
                    ignore_white_space |= IGNORE_TAB_EXPANSION;
                break;

            case 'f':
                specify_style(OUTPUT_FORWARD_ED);
                break;

            case 'F':
                add_regexp(&function_regexp_list, optarg);
                break;

            case 'h':
                /* Split the files into chunks for faster processing.
                   Usually does not change the result.

                   This currently has no effect.  */
                break;

            case 'H':
                speed_large_files = true;
                break;

            case 'i':
                ignore_case = true;
                break;

            case 'I':
                add_regexp(&ignore_regexp_list, optarg);
                break;

            case 'l':
                if (!pr_program[0])
                    try_help("pagination not supported on this host", NULL);
                paginate = true;
#ifdef SIGCHLD
            /* Pagination requires forking and waiting, and
               System V fork+wait does not work if SIGCHLD is ignored.  */
                signal(SIGCHLD, SIG_DFL);
#endif
                break;

            case 'L':
                if (!file_label[0])
                    file_label[0] = optarg;
                else if (!file_label[1])
                    file_label[1] = optarg;
                else
                    fatal("too many file label options");
                break;

            case 'n':
                specify_style(OUTPUT_RCS);
                break;

            case 'N':
                new_file = true;
                break;

            case 'p':
                show_c_function = true;
                add_regexp(&function_regexp_list, "^[[:alpha:]$_]");
                break;

            case 'P':
                unidirectional_new_file = true;
                break;

            case 'q':
                brief = true;
                break;

            case 'r':
                recursive = true;
                break;

            case 's':
                report_identical_files = true;
                break;

            case 'S':
                specify_value(&starting_file, optarg, "-S");
                break;

            case 't':
                expand_tabs = true;
                break;

            case 'T':
                initial_tab = true;
                break;

            case 'u':
                specify_style(OUTPUT_UNIFIED);
                if (context < 3)
                    context = 3;
                break;

            case 'v':
                version_etc(stdout, PROGRAM_NAME, PACKAGE_NAME, Version,
                            AUTHORS, (char *) NULL);
                check_stdout();
                return EXIT_SUCCESS;

            case 'w':
                ignore_white_space = IGNORE_ALL_SPACE;
                break;

            case 'x':
                add_exclude(excluded, optarg, exclude_options());
                break;

            case 'X':
                if (add_exclude_file(add_exclude, excluded, optarg,
                                     exclude_options(), '\n'))
                    pfatal_with_name(optarg);
                break;

            case 'y':
                specify_style(OUTPUT_SDIFF);
                break;

            case 'W':
                numval = strtoimax(optarg, &numend, 10);
                if (!(0 < numval && numval <= SIZE_MAX) || *numend)
                    try_help("invalid width '%s'", optarg);
                if (width != numval) {
                    if (width)
                        fatal("conflicting width options");
                    width = numval;
                }
                break;

            case BINARY_OPTION:
#if O_BINARY
          binary = true;
          if (! isatty (STDOUT_FILENO))
            set_binary_mode (STDOUT_FILENO, O_BINARY);
#endif
                break;

            case FROM_FILE_OPTION:
                specify_value(&from_file, optarg, "--from-file");
                break;

            case HELP_OPTION:
                usage();
                check_stdout();
                return EXIT_SUCCESS;

            case HORIZON_LINES_OPTION:
                numval = strtoimax(optarg, &numend, 10);
                if (*numend || numval < 0)
                    try_help("invalid horizon length '%s'", optarg);
                horizon_lines = MAX(horizon_lines, MIN (numval, LIN_MAX));
                break;

            case IGNORE_FILE_NAME_CASE_OPTION:
                ignore_file_name_case = true;
                break;

            case INHIBIT_HUNK_MERGE_OPTION:
                /* This option is obsolete, but accept it for backward
                   compatibility.  */
                break;

            case LEFT_COLUMN_OPTION:
                left_column = true;
                break;

            case LINE_FORMAT_OPTION:
                specify_style(OUTPUT_IFDEF);
                for (i = 0; i < sizeof line_format / sizeof line_format[0]; i++)
                    specify_value(&line_format[i], optarg, "--line-format");
                break;

            case NO_DEREFERENCE_OPTION:
                no_dereference_symlinks = true;
                break;

            case NO_IGNORE_FILE_NAME_CASE_OPTION:
                ignore_file_name_case = false;
                break;

            case NORMAL_OPTION:
                specify_style(OUTPUT_NORMAL);
                break;

            case SDIFF_MERGE_ASSIST_OPTION:
                specify_style(OUTPUT_SDIFF);
                sdiff_merge_assist = true;
                break;

            case STRIP_TRAILING_CR_OPTION:
                strip_trailing_cr = true;
                break;

            case SUPPRESS_BLANK_EMPTY_OPTION:
                suppress_blank_empty = true;
                break;

            case SUPPRESS_COMMON_LINES_OPTION:
                suppress_common_lines = true;
                break;

            case TABSIZE_OPTION:
                numval = strtoimax(optarg, &numend, 10);
                if (!(0 < numval && numval <= SIZE_MAX - GUTTER_WIDTH_MINIMUM)
                    || *numend)
                    try_help("invalid tabsize '%s'", optarg);
                if (tabsize != numval) {
                    if (tabsize)
                        fatal("conflicting tabsize options");
                    tabsize = numval;
                }
                break;

            case TO_FILE_OPTION:
                specify_value(&to_file, optarg, "--to-file");
                break;

            case UNCHANGED_LINE_FORMAT_OPTION:
            case OLD_LINE_FORMAT_OPTION:
            case NEW_LINE_FORMAT_OPTION:
                specify_style(OUTPUT_IFDEF);
                c -= UNCHANGED_LINE_FORMAT_OPTION;
                specify_value(&line_format[c], optarg, line_format_option[c]);
                break;

            case UNCHANGED_GROUP_FORMAT_OPTION:
            case OLD_GROUP_FORMAT_OPTION:
            case NEW_GROUP_FORMAT_OPTION:
            case CHANGED_GROUP_FORMAT_OPTION:
                specify_style(OUTPUT_IFDEF);
                c -= UNCHANGED_GROUP_FORMAT_OPTION;
                specify_value(&group_format[c], optarg, group_format_option[c]);
                break;

            case COLOR_OPTION:
                specify_colors_style(optarg);
                break;

            case COLOR_PALETTE_OPTION:
                set_color_palette(optarg);
                break;

            case PRESUME_OUTPUT_TTY_OPTION:
                presume_output_tty = true;
                break;

            default:
                try_help(NULL, NULL);
        }
        prev = c;
    }

    if (colors_style == AUTO) {
        char const *t = getenv("TERM");
        if (t && STREQ(t, "dumb"))
            colors_style = NEVER;
    }

    if (output_style == OUTPUT_UNSPECIFIED) {
        if (show_c_function) {
            specify_style(OUTPUT_CONTEXT);
            if (ocontext < 0)
                context = 3;
        } else
            specify_style(OUTPUT_NORMAL);
    }

    if (output_style != OUTPUT_CONTEXT || hard_locale(LC_TIME)) {
#if (defined STAT_TIMESPEC || defined STAT_TIMESPEC_NS \
     || defined HAVE_STRUCT_STAT_ST_SPARE1)
        time_format = "%Y-%m-%d %H:%M:%S.%N %z";
#else
      time_format = "%Y-%m-%d %H:%M:%S %z";
#endif
    } else {
        /* See POSIX 1003.1-2001 for this format.  */
        time_format = "%a %b %e %T %Y";
    }

    if (0 <= ocontext
        && (output_style == OUTPUT_CONTEXT
            || output_style == OUTPUT_UNIFIED)
        && (context < ocontext
            || (ocontext < context && !explicit_context)))
        context = ocontext;

    if (!tabsize)
        tabsize = 8;
    if (!width)
        width = 130; {
        /* Maximize first the half line width, and then the gutter width,
           according to the following constraints:

            1.  Two half lines plus a gutter must fit in a line.
            2.  If the half line width is nonzero:
                a.  The gutter width is at least GUTTER_WIDTH_MINIMUM.
                b.  If tabs are not expanded to spaces,
                    a half line plus a gutter is an integral number of tabs,
                    so that tabs in the right column line up.  */

        size_t t = expand_tabs ? 1 : tabsize;
        size_t w = width;
        size_t t_plus_g = t + GUTTER_WIDTH_MINIMUM;
        size_t unaligned_off = (w >> 1) + (t_plus_g >> 1) + (w & t_plus_g & 1);
        size_t off = unaligned_off - unaligned_off % t;
        sdiff_half_width = (off <= GUTTER_WIDTH_MINIMUM || w <= off
                                ? 0
                                : MIN(off - GUTTER_WIDTH_MINIMUM, w - off));
        sdiff_column2_offset = sdiff_half_width ? off : w;
    }

    /* Make the horizon at least as large as the context, so that
       shift_boundaries has more freedom to shift the first and last hunks.  */
    if (horizon_lines < context)
        horizon_lines = context;

    summarize_regexp_list(&function_regexp_list);
    summarize_regexp_list(&ignore_regexp_list);

    if (output_style == OUTPUT_IFDEF) {
        for (i = 0; i < sizeof line_format / sizeof line_format[0]; i++)
            if (!line_format[i])
                line_format[i] = "%l\n";
        if (!group_format[OLD])
            group_format[OLD]
                    = group_format[CHANGED] ? group_format[CHANGED] : "%<";
        if (!group_format[NEW])
            group_format[NEW]
                    = group_format[CHANGED] ? group_format[CHANGED] : "%>";
        if (!group_format[UNCHANGED])
            group_format[UNCHANGED] = "%=";
        if (!group_format[CHANGED])
            group_format[CHANGED] = concat(group_format[OLD],
                                           group_format[NEW], "");
    }

    no_diff_means_no_output =
    (output_style == OUTPUT_IFDEF
         ? (!*group_format[UNCHANGED]
            || (STREQ(group_format[UNCHANGED], "%=")
                && !*line_format[UNCHANGED]))
         : (output_style != OUTPUT_SDIFF) | suppress_common_lines);

    files_can_be_treated_as_binary =
    (brief & binary
     & ~(ignore_blank_lines | ignore_case | strip_trailing_cr
         | (ignore_regexp_list.regexps || ignore_white_space)));

    switch_string = option_list(argv + 1, optind - 1);

    if (from_file) {
        if (to_file)
            fatal("--from-file and --to-file both specified");
        else
            for (; optind < argc; optind++) {
                int status = compare_files(NULL, from_file, argv[optind]);
                if (exit_status < status)
                    exit_status = status;
            }
    } else {
        if (to_file)
            for (; optind < argc; optind++) {
                int status = compare_files(NULL, argv[optind], to_file);
                if (exit_status < status)
                    exit_status = status;
            }
        else {
            if (argc - optind != 2) {
                if (argc - optind < 2)
                    try_help("missing operand after '%s'", argv[argc - 1]);
                else
                    try_help("extra operand '%s'", argv[optind + 2]);
            }

            exit_status = compare_files(NULL, argv[optind], argv[optind + 1]);
        }
    }

    /* Print any messages that were saved up for last.  */
    print_message_queue();

    check_stdout();
    exit(exit_status);
    return exit_status;
}

/* Append to REGLIST the regexp PATTERN.  */

static void
add_regexp(struct regexp_list *reglist, char const *pattern) {
    size_t patlen = strlen(pattern);
    char const *m = re_compile_pattern(pattern, patlen, reglist->buf);

    if (m != 0)
        error(EXIT_TROUBLE, 0, "%s: %s", pattern, m);
    else {
        char *regexps = reglist->regexps;
        size_t len = reglist->len;
        bool multiple_regexps = reglist->multiple_regexps = regexps != 0;
        size_t newlen = reglist->len = len + 2 * multiple_regexps + patlen;
        size_t size = reglist->size;

        if (size <= newlen) {
            if (!size)
                size = 1;

            do size *= 2;
            while (size <= newlen);

            reglist->size = size;
            reglist->regexps = regexps = xrealloc(regexps, size);
        }
        if (multiple_regexps) {
            regexps[len++] = '\\';
            regexps[len++] = '|';
        }
        memcpy(regexps + len, pattern, patlen + 1);
    }
}

/* Ensure that REGLIST represents the disjunction of its regexps.
   This is done here, rather than earlier, to avoid O(N^2) behavior.  */

static void
summarize_regexp_list(struct regexp_list *reglist) {
    if (reglist->regexps) {
        /* At least one regexp was specified.  Allocate a fastmap for it.  */
        reglist->buf->fastmap = xmalloc(1 << CHAR_BIT);
        if (reglist->multiple_regexps) {
            /* Compile the disjunction of the regexps.
               (If just one regexp was specified, it is already compiled.)  */
            char const *m = re_compile_pattern(reglist->regexps, reglist->len,
                                               reglist->buf);
            if (m)
                die(EXIT_TROUBLE, 0, "%s: %s", reglist->regexps, m);
        }
    }
}

static void
try_help(char const *reason_msgid, char const *operand) {
    if (reason_msgid)
        error(0, 0, _(reason_msgid), operand);
    die(EXIT_TROUBLE, 0, _("Try '%s --help' for more information."),
        program_name);
}

static void
check_stdout(void) {
    if (ferror(stdout))
        fatal("write failed");
    else if (fclose(stdout) != 0)
        pfatal_with_name(_("standard output"));
}

static char const *const option_help_msgid[] = {
    N_("    --normal                  output a normal diff (the default)"),
    N_("-q, --brief                   report only when files differ"),
    N_("-s, --report-identical-files  report when two files are the same"),
    N_("-c, -C NUM, --context[=NUM]   output NUM (default 3) lines of copied context"),
    N_("-u, -U NUM, --unified[=NUM]   output NUM (default 3) lines of unified context"),
    N_("-e, --ed                      output an ed script"),
    N_("-n, --rcs                     output an RCS format diff"),
    N_("-y, --side-by-side            output in two columns"),
    N_("-W, --width=NUM               output at most NUM (default 130) print columns"),
    N_("    --left-column             output only the left column of common lines"),
    N_("    --suppress-common-lines   do not output common lines"),
    "",
    N_("-p, --show-c-function         show which C function each change is in"),
    N_("-F, --show-function-line=RE   show the most recent line matching RE"),
    N_("    --label LABEL             use LABEL instead of file name and timestamp\n"
        "                                (can be repeated)"),
    "",
    N_("-t, --expand-tabs             expand tabs to spaces in output"),
    N_("-T, --initial-tab             make tabs line up by prepending a tab"),
    N_("    --tabsize=NUM             tab stops every NUM (default 8) print columns"),
    N_("    --suppress-blank-empty    suppress space or tab before empty output lines"),
    N_("-l, --paginate                pass output through 'pr' to paginate it"),
    "",
    N_("-r, --recursive                 recursively compare any subdirectories found"),
    N_("    --no-dereference            don't follow symbolic links"),
    N_("-N, --new-file                  treat absent files as empty"),
    N_("    --unidirectional-new-file   treat absent first files as empty"),
    N_("    --ignore-file-name-case     ignore case when comparing file names"),
    N_("    --no-ignore-file-name-case  consider case when comparing file names"),
    N_("-x, --exclude=PAT               exclude files that match PAT"),
    N_("-X, --exclude-from=FILE         exclude files that match any pattern in FILE"),
    N_("-S, --starting-file=FILE        start with FILE when comparing directories"),
    N_("    --from-file=FILE1           compare FILE1 to all operands;\n"
        "                                  FILE1 can be a directory"),
    N_("    --to-file=FILE2             compare all operands to FILE2;\n"
        "                                  FILE2 can be a directory"),
    "",
    N_("-i, --ignore-case               ignore case differences in file contents"),
    N_("-E, --ignore-tab-expansion      ignore changes due to tab expansion"),
    N_("-Z, --ignore-trailing-space     ignore white space at line end"),
    N_("-b, --ignore-space-change       ignore changes in the amount of white space"),
    N_("-w, --ignore-all-space          ignore all white space"),
    N_("-B, --ignore-blank-lines        ignore changes where lines are all blank"),
    N_("-I, --ignore-matching-lines=RE  ignore changes where all lines match RE"),
    "",
    N_("-a, --text                      treat all files as text"),
    N_("    --strip-trailing-cr         strip trailing carriage return on input"),
#if O_BINARY
  N_("    --binary                    read and write data in binary mode"),
#endif
    "",
    N_("-D, --ifdef=NAME                output merged file with '#ifdef NAME' diffs"),
    N_("    --GTYPE-group-format=GFMT   format GTYPE input groups with GFMT"),
    N_("    --line-format=LFMT          format all input lines with LFMT"),
    N_("    --LTYPE-line-format=LFMT    format LTYPE input lines with LFMT"),
    N_("  These format options provide fine-grained control over the output\n"
        "    of diff, generalizing -D/--ifdef."),
    N_("  LTYPE is 'old', 'new', or 'unchanged'.  GTYPE is LTYPE or 'changed'."),
    N_("  GFMT (only) may contain:\n\
    %<  lines from FILE1\n\
    %>  lines from FILE2\n\
    %=  lines common to FILE1 and FILE2\n\
    %[-][WIDTH][.[PREC]]{doxX}LETTER  printf-style spec for LETTER\n\
      LETTERs are as follows for new group, lower case for old group:\n\
        F  first line number\n\
        L  last line number\n\
        N  number of lines = L-F+1\n\
        E  F-1\n\
        M  L+1\n\
    %(A=B?T:E)  if A equals B then T else E"),
    N_("  LFMT (only) may contain:\n\
    %L  contents of line\n\
    %l  contents of line, excluding any trailing newline\n\
    %[-][WIDTH][.[PREC]]{doxX}n  printf-style spec for input line number"),
    N_("  Both GFMT and LFMT may contain:\n\
    %%  %\n\
    %c'C'  the single character C\n\
    %c'\\OOO'  the character with octal code OOO\n\
    C    the character C (other characters represent themselves)"),
    "",
    N_("-d, --minimal            try hard to find a smaller set of changes"),
    N_("    --horizon-lines=NUM  keep NUM lines of the common prefix and suffix"),
    N_("    --speed-large-files  assume large files and many scattered small changes"),
    N_("    --color[=WHEN]       color output; WHEN is 'never', 'always', or 'auto';\n"
        "                           plain --color means --color='auto'"),
    N_("    --palette=PALETTE    the colors to use when --color is active; PALETTE is\n"
        "                           a colon-separated list of terminfo capabilities"),
    "",
    N_("    --help               display this help and exit"),
    N_("-v, --version            output version information and exit"),
    "",
    N_("FILES are 'FILE1 FILE2' or 'DIR1 DIR2' or 'DIR FILE' or 'FILE DIR'."),
    N_("If --from-file or --to-file is given, there are no restrictions on FILE(s)."),
    N_("If a FILE is '-', read standard input."),
    N_("Exit status is 0 if inputs are the same, 1 if different, 2 if trouble."),
    0
};

static void
usage(void) {
    char const *const *p;

    printf(_("Usage: %s [OPTION]... FILES\n"), program_name);
    printf("%s\n\n", _("Compare FILES line by line."));

    fputs(_("\
Mandatory arguments to long options are mandatory for short options too.\n\
"), stdout);

    for (p = option_help_msgid; *p; p++) {
        if (!**p)
            putchar('\n');
        else {
            char const *msg = _(*p);
            char const *nl;
            while ((nl = strchr(msg, '\n'))) {
                int msglen = nl + 1 - msg;
                /* This assertion is solely to avoid a warning from
                   gcc's -Wformat-overflow=.  */
                assert(msglen < 4096);
                printf("  %.*s", msglen, msg);
                msg = nl + 1;
            }

            printf(&"  %s\n"[2 * (*msg != ' ' && *msg != '-')], msg);
        }
    }
    emit_bug_reporting_address();
}

/* Set VAR to VALUE, reporting an OPTION error if this is a
   conflict.  */
static void
specify_value(char const **var, char const *value, char const *option) {
    if (*var && !STREQ(*var, value)) {
        error(0, 0, _("conflicting %s option value '%s'"), option, value);
        try_help(NULL, NULL);
    }
    *var = value;
}

/* Set the output style to STYLE, diagnosing conflicts.  */
static void
specify_style(enum output_style style) {
    if (output_style != style) {
        if (output_style != OUTPUT_UNSPECIFIED)
            try_help("conflicting output style options", NULL);
        output_style = style;
    }
}

/* Set the color mode.  */
static void
specify_colors_style(char const *value) {
    if (value == NULL || STREQ(value, "auto"))
        colors_style = AUTO;
    else if (STREQ(value, "always"))
        colors_style = ALWAYS;
    else if (STREQ(value, "never"))
        colors_style = NEVER;
    else
        try_help("invalid color '%s'", value);
}


/* Set the last-modified time of *ST to be the current time.  */

static void
set_mtime_to_now(struct stat *st) {
#ifdef STAT_TIMESPEC
    gettime(&STAT_TIMESPEC(st, st_mtim));
#else
  struct timespec t;
  gettime (&t);
  st->st_mtime = t.tv_sec;
# if defined STAT_TIMESPEC_NS
  STAT_TIMESPEC_NS (st, st_mtim) = t.tv_nsec;
# elif defined HAVE_STRUCT_STAT_ST_SPARE1
  st->st_spare1 = t.tv_nsec / 1000;
# endif
#endif
}

/* Compare two files (or dirs) with parent comparison PARENT
   and names NAME0 and NAME1.
   (If PARENT is null, then the first name is just NAME0, etc.)
   This is self-contained; it opens the files and closes them.

   Value is EXIT_SUCCESS if files are the same, EXIT_FAILURE if
   different, EXIT_TROUBLE if there is a problem opening them.  */

static int
compare_files(struct comparison const *parent,
              char const *name0,
              char const *name1) {
    struct comparison cmp;
#define DIR_P(f) (S_ISDIR (cmp.file[f].stat.st_mode) != 0)
    register int f;
    int status = EXIT_SUCCESS;
    bool same_files;
    char *free0;
    char *free1;

    /* If this is directory comparison, perhaps we have a file
       that exists only in one of the directories.
       If so, just print a message to that effect.  */

    if (!((name0 && name1)
          || (unidirectional_new_file && name1)
          || new_file)) {
        char const *name = name0 ? name0 : name1;
        char const *dir = parent->file[!name0].name;

        /* See POSIX 1003.1-2001 for this format.  */
        message("Only in %s: %s\n", dir, name);

        /* Return EXIT_FAILURE so that diff_dirs will return
           EXIT_FAILURE ("some files differ").  */
        return EXIT_FAILURE;
    }

    memset(cmp.file, 0, sizeof cmp.file);
    cmp.parent = parent;

    /* cmp.file[f].desc markers */
#define NONEXISTENT (-1) /* nonexistent file */
#define UNOPENED (-2) /* unopened file (e.g. directory) */
#define ERRNO_ENCODE(errno) (-3 - (errno)) /* encoded errno value */

#define ERRNO_DECODE(desc) (-3 - (desc)) /* inverse of ERRNO_ENCODE */

    cmp.file[0].desc = name0 ? UNOPENED : NONEXISTENT;
    cmp.file[1].desc = name1 ? UNOPENED : NONEXISTENT;

    /* Now record the full name of each file, including nonexistent ones.  */

    if (!name0)
        name0 = name1;
    if (!name1)
        name1 = name0;

    if (!parent) {
        free0 = NULL;
        free1 = NULL;
        cmp.file[0].name = name0;
        cmp.file[1].name = name1;
    } else {
        cmp.file[0].name = free0
                           = file_name_concat(parent->file[0].name, name0, NULL);
        cmp.file[1].name = free1
                           = file_name_concat(parent->file[1].name, name1, NULL);
    }

    /* Stat the files.  */

    for (f = 0; f < 2; f++) {
        if (cmp.file[f].desc != NONEXISTENT) {
            if (f && file_name_cmp(cmp.file[f].name, cmp.file[0].name) == 0) {
                cmp.file[f].desc = cmp.file[0].desc;
                cmp.file[f].stat = cmp.file[0].stat;
            } else if (STREQ(cmp.file[f].name, "-")) {
                cmp.file[f].desc = STDIN_FILENO;
                if (binary && !isatty(STDIN_FILENO))
                    set_binary_mode(STDIN_FILENO, O_BINARY);
                if (fstat(STDIN_FILENO, &cmp.file[f].stat) != 0)
                    cmp.file[f].desc = ERRNO_ENCODE(errno);
                else {
                    if (S_ISREG(cmp.file[f].stat.st_mode)) {
                        off_t pos = lseek(STDIN_FILENO, 0, SEEK_CUR);
                        if (pos < 0)
                            cmp.file[f].desc = ERRNO_ENCODE(errno);
                        else
                            cmp.file[f].stat.st_size =
                                    MAX(0, cmp.file[f].stat.st_size - pos);
                    }

                    /* POSIX 1003.1-2001 requires current time for
                       stdin.  */
                    set_mtime_to_now(&cmp.file[f].stat);
                }
            } else if ((no_dereference_symlinks
                            ? lstat(cmp.file[f].name, &cmp.file[f].stat)
                            : stat(cmp.file[f].name, &cmp.file[f].stat))
                       != 0)
                cmp.file[f].desc = ERRNO_ENCODE(errno);
        }
    }

    /* Mark files as nonexistent as needed for -N and -P, if they are
       inaccessible empty regular files (the kind of files that 'patch'
       creates to indicate nonexistent backups), or if they are
       top-level files that do not exist but their counterparts do
       exist.  */
    for (f = 0; f < 2; f++)
        if ((new_file || (f == 0 && unidirectional_new_file))
            && (cmp.file[f].desc == UNOPENED
                    ? (S_ISREG(cmp.file[f].stat.st_mode)
                       && !(cmp.file[f].stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO))
                       && cmp.file[f].stat.st_size == 0)
                    : ((cmp.file[f].desc == ERRNO_ENCODE(ENOENT)
                        || cmp.file[f].desc == ERRNO_ENCODE(EBADF))
                       && !parent
                       && (cmp.file[1 - f].desc == UNOPENED
                           || cmp.file[1 - f].desc == STDIN_FILENO))))
            cmp.file[f].desc = NONEXISTENT;

    for (f = 0; f < 2; f++)
        if (cmp.file[f].desc == NONEXISTENT) {
            memset(&cmp.file[f].stat, 0, sizeof cmp.file[f].stat);
            cmp.file[f].stat.st_mode = cmp.file[1 - f].stat.st_mode;
        }

    for (f = 0; f < 2; f++) {
        int e = ERRNO_DECODE(cmp.file[f].desc);
        if (0 <= e) {
            errno = e;
            perror_with_name(cmp.file[f].name);
            status = EXIT_TROUBLE;
        }
    }

    if (status == EXIT_SUCCESS && !parent && DIR_P(0) != DIR_P(1)) {
        /* If one is a directory, and it was specified in the command line,
           use the file in that dir with the other file's basename.  */

        int fnm_arg = DIR_P(0);
        int dir_arg = 1 - fnm_arg;
        char const *fnm = cmp.file[fnm_arg].name;
        char const *dir = cmp.file[dir_arg].name;
        char const *filename = cmp.file[dir_arg].name = free0
                                                        = find_dir_file_pathname(dir, last_component(fnm));

        if (STREQ(fnm, "-"))
            fatal("cannot compare '-' to a directory");

        if ((no_dereference_symlinks
                 ? lstat(filename, &cmp.file[dir_arg].stat)
                 : stat(filename, &cmp.file[dir_arg].stat))
            != 0) {
            perror_with_name(filename);
            status = EXIT_TROUBLE;
        }
    }

    if (status != EXIT_SUCCESS) {
        /* One of the files should exist but does not.  */
    } else if (cmp.file[0].desc == NONEXISTENT
               && cmp.file[1].desc == NONEXISTENT) {
        /* Neither file "exists", so there's nothing to compare.  */
    } else if ((same_files
                = (cmp.file[0].desc != NONEXISTENT
                   && cmp.file[1].desc != NONEXISTENT
                   && 0 < same_file(&cmp.file[0].stat, &cmp.file[1].stat)
                   && same_file_attributes(&cmp.file[0].stat,
                                           &cmp.file[1].stat)))
               && no_diff_means_no_output) {
        /* The two named files are actually the same physical file.
           We know they are identical without actually reading them.  */
    } else if (DIR_P(0) & DIR_P(1)) {
        if (output_style == OUTPUT_IFDEF)
            fatal("-D option not supported with directories");

        /* If both are directories, compare the files in them.  */

        if (parent && !recursive) {
            /* But don't compare dir contents one level down
               unless -r was specified.
               See POSIX 1003.1-2001 for this format.  */
            message("Common subdirectories: %s and %s\n",
                    cmp.file[0].name, cmp.file[1].name);
        } else
            status = diff_dirs(&cmp, compare_files);
    } else if ((DIR_P(0) | DIR_P(1))
               || (parent
                   && !((S_ISREG(cmp.file[0].stat.st_mode)
                         || S_ISLNK(cmp.file[0].stat.st_mode))
                        && (S_ISREG(cmp.file[1].stat.st_mode)
                            || S_ISLNK(cmp.file[1].stat.st_mode))))) {
        if (cmp.file[0].desc == NONEXISTENT || cmp.file[1].desc == NONEXISTENT) {
            /* We have a subdirectory that exists only in one directory.  */

            if ((DIR_P(0) | DIR_P(1))
                && recursive
                && (new_file
                    || (unidirectional_new_file
                        && cmp.file[0].desc == NONEXISTENT)))
                status = diff_dirs(&cmp, compare_files);
            else {
                char const *dir;

                /* PARENT must be non-NULL here.  */
                assert(parent);
                dir = parent->file[cmp.file[0].desc == NONEXISTENT].name;

                /* See POSIX 1003.1-2001 for this format.  */
                message("Only in %s: %s\n", dir, name0);

                status = EXIT_FAILURE;
            }
        } else {
            /* We have two files that are not to be compared.  */

            /* See POSIX 1003.1-2001 for this format.  */
            message5("File %s is a %s while file %s is a %s\n",
                     file_label[0] ? file_label[0] : cmp.file[0].name,
                     file_type(&cmp.file[0].stat),
                     file_label[1] ? file_label[1] : cmp.file[1].name,
                     file_type(&cmp.file[1].stat));

            /* This is a difference.  */
            status = EXIT_FAILURE;
        }
    } else if (S_ISLNK(cmp.file[0].stat.st_mode)
               || S_ISLNK(cmp.file[1].stat.st_mode)) {
        /* We get here only if we use lstat(), not stat().  */
        assert(no_dereference_symlinks);

        if (S_ISLNK(cmp.file[0].stat.st_mode)
            && S_ISLNK(cmp.file[1].stat.st_mode)) {
            /* Compare the values of the symbolic links.  */
            char *link_value[2] = {NULL, NULL};

            for (f = 0; f < 2; f++) {
                link_value[f] = xreadlink(cmp.file[f].name);
                if (link_value[f] == NULL) {
                    perror_with_name(cmp.file[f].name);
                    status = EXIT_TROUBLE;
                    break;
                }
            }
            if (status == EXIT_SUCCESS) {
                if (!STREQ(link_value[0], link_value[1])) {
                    message("Symbolic links %s and %s differ\n",
                            cmp.file[0].name, cmp.file[1].name);
                    /* This is a difference.  */
                    status = EXIT_FAILURE;
                }
            }
            for (f = 0; f < 2; f++)
                free(link_value[f]);
        } else {
            /* We have two files that are not to be compared, because
               one of them is a symbolic link and the other one is not.  */

            message5("File %s is a %s while file %s is a %s\n",
                     file_label[0] ? file_label[0] : cmp.file[0].name,
                     file_type(&cmp.file[0].stat),
                     file_label[1] ? file_label[1] : cmp.file[1].name,
                     file_type(&cmp.file[1].stat));

            /* This is a difference.  */
            status = EXIT_FAILURE;
        }
    } else if (files_can_be_treated_as_binary
               && S_ISREG(cmp.file[0].stat.st_mode)
               && S_ISREG(cmp.file[1].stat.st_mode)
               && cmp.file[0].stat.st_size != cmp.file[1].stat.st_size
               && 0 < cmp.file[0].stat.st_size
               && 0 < cmp.file[1].stat.st_size) {
        message("Files %s and %s differ\n",
                file_label[0] ? file_label[0] : cmp.file[0].name,
                file_label[1] ? file_label[1] : cmp.file[1].name);
        status = EXIT_FAILURE;
    } else {
        /* Both exist and neither is a directory.  */

        /* Open the files and record their descriptors.  */

        int oflags = O_RDONLY | (binary ? O_BINARY : 0);

        if (cmp.file[0].desc == UNOPENED)
            if ((cmp.file[0].desc = open(cmp.file[0].name, oflags, 0)) < 0) {
                perror_with_name(cmp.file[0].name);
                status = EXIT_TROUBLE;
            }
        if (cmp.file[1].desc == UNOPENED) {
            if (same_files)
                cmp.file[1].desc = cmp.file[0].desc;
            else if ((cmp.file[1].desc = open(cmp.file[1].name, oflags, 0)) < 0) {
                perror_with_name(cmp.file[1].name);
                status = EXIT_TROUBLE;
            }
        }

        /* Compare the files, if no error was found.  */

        if (status == EXIT_SUCCESS)
            status = diff_2_files(&cmp);

        /* Close the file descriptors.  */

        if (0 <= cmp.file[0].desc && close(cmp.file[0].desc) != 0) {
            perror_with_name(cmp.file[0].name);
            status = EXIT_TROUBLE;
        }
        if (0 <= cmp.file[1].desc && cmp.file[0].desc != cmp.file[1].desc
            && close(cmp.file[1].desc) != 0) {
            perror_with_name(cmp.file[1].name);
            status = EXIT_TROUBLE;
        }
    }

    /* Now the comparison has been done, if no error prevented it,
       and STATUS is the value this function will return.  */

    if (status == EXIT_SUCCESS) {
        if (report_identical_files && !DIR_P(0))
            message("Files %s and %s are identical\n",
                    file_label[0] ? file_label[0] : cmp.file[0].name,
                    file_label[1] ? file_label[1] : cmp.file[1].name);
    } else {
        /* Flush stdout so that the user sees differences immediately.
           This can hurt performance, unfortunately.  */
        if (fflush(stdout) != 0)
            pfatal_with_name(_("standard output"));
    }

    free(free0);
    free(free1);

    return status;
}
