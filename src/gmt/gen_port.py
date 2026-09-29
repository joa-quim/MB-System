"""Generate a GMT module (C) from an MB-System program, keeping its original
getopt_long() option loop verbatim on top of the reentrant mb_getopt_long().

Mechanical changes only - the original text (comments included) is kept:
  * <cXXX> -> <XXX.h>, getopt.h and C++-only headers dropped, nullptr -> NULL,
    constexpr, C++ casts, std::min/max/abs, numeric_limits NaN;
    'static' on file-scope definitions (they share one DLL);
  * struct option / getopt_long / optarg / optind -> the mb_getopt_* versions,
    whose state lives in a local structure (no globals);
  * main(argc, argv) -> GMT_<name>(): argc/argv are rebuilt from whatever
    shape GMT hands the module; every exit()/return in main becomes Return().

usage: gen_port.py <module> [...]   (specs in gen_port_specs.json)
"""
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..') + '/'
STATIC_RE = re.compile(r'^(bool|int|char|double|float|void|FILE|long|unsigned|size_t|short|mb_path|mb_pathplus|time_t)\b[\w\s\*]*?\b\w+\s*(\(|\[|=|;)')


def conv_line(s, in_main, make_static):
    if make_static and not s.startswith('static') and not s.startswith('extern'):
        if STATIC_RE.match(s) or re.match(r'^struct\s+\w+\s*\*?\s*\w+\s*(\(|\[|=|;)', s):
            s = 'static ' + s
    s = s.replace('nullptr', 'NULL')
    s = re.sub(r'\bconstexpr\s+char\b', 'static const char', s)
    m = re.match(r'^(\s*)(?:static\s+)?constexpr\s+(?!double\b|float\b|char\b)[\w ]+?\s+(\w+)\s*=\s*(.+);(.*)$', s)
    if m:
        val = m.group(3)
        if re.search(r'\d\.\d|\d\.\b', val):
            val = '(int)(%s)' % val     # e.g. constexpr int X = 15.0
        s = '%senum { %s = %s };%s' % (m.group(1), m.group(2), val, m.group(4))
    # C++ declaration in a condition: if (int n = expr) -> if (expr)
    s = re.sub(r'\bif \((?:const )?(?:int|bool|double) \w+ = ', 'if (', s)
    m = re.match(r'^(\s*)(?:static\s+)?constexpr\s+(?:double|float)\s+(\w+)\s*=\s*(.+);(.*)$', s)
    if m:
        s = '%s#define %s (%s)%s' % (m.group(1), m.group(2), m.group(3), m.group(4))
    for cast in ('static_cast', 'reinterpret_cast', 'const_cast'):
        s = re.sub(cast + r'<([^<>]+)>\(', r'(\1)(', s)
    s = re.sub(r'\bstd::min\(', 'MIN(', s)
    s = re.sub(r'\bstd::max\(', 'MAX(', s)
    s = re.sub(r'\bstd::abs\(', 'fabs(', s)
    s = re.sub(r'\bstd::fabs\(', 'fabs(', s)
    s = re.sub(r'std::numeric_limits<\s*(?:double|float)\s*>::quiet_NaN\(\)', 'NAN', s)
    # getopt -> reentrant mb_getopt
    s = re.sub(r'\bstruct option\b', 'struct mb_getopt_option', s)
    s = re.sub(r'\b(no_argument|required_argument|optional_argument)\b', r'mb_\1', s)
    s = re.sub(r'\bgetopt_long\(', 'mb_getopt_long(&getopt_state, ', s)
    s = re.sub(r'\boptarg\b', 'getopt_state.optarg', s)
    s = re.sub(r'\boptind\b', 'getopt_state.optind', s)
    if in_main:
        s = re.sub(r'\breturn\s+([^;]+);', r'Return(\1);', s)
        s = re.sub(r'\breturn\s*;', r'Return(MB_ERROR_NO_ERROR);', s)
        s = re.sub(r'\bexit\(([^;]+)\);', r'Return(\1);', s)
    return s


def convert_includes(lines):
    out = []
    for l in lines:
        m = re.match(r'#include <c(math|stdio|stdlib|string|time|ctype|errno|limits|stdint|float|assert|signal|stdarg)>', l)
        if m:
            out.append('#include <%s.h>' % m.group(1))
        elif re.match(r'#include <(getopt\.h|algorithm|thread|string|vector|iostream|fstream|sstream|cstdint|cinttypes|limits|array|memory|utility|cstddef)>', l):
            continue
        elif '_capi.h' in l:
            continue
        else:
            out.append(l)
    return out


TEMPLATE = '''
/* --- GMT front end ---------------------------------------------------- */

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\\n", @@USAGE@@);
	if (level == GMT_SYNOPSIS) return GMT_PARSE_ERROR;
	GMT_Message(API, GMT_TIME_NONE, "%s\\n", @@HELP@@);
	return GMT_PARSE_ERROR;
}

/* The options GMT itself should see: -V (verbosity) and -I (the input the
 * module keys bind). Everything else, long options included, is parsed by
 * the program's own option loop below. */
static char *mb_gmt_options_string(int argc, char **argv) {
	size_t total = 1;
	for (int i = 1; i < argc; i++)
		total += strlen(argv[i]) + 1;
	char *s = (char *)calloc(total + 8, 1);
	if (s == NULL)
		return NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && (argv[i][1] == 'V' || (argv[i][1] == 'I' && argv[i][2] != '\\0'))) {
			if (s[0] != '\\0')
				strcat(s, " ");
			strcat(s, argv[i]);
		}
	}
	return s;
}

/* gmt_M_free_options() hard-codes a variable named "options", which the
   program's own option table shadows here, so destroy gmt_options directly */
#define bailout(code) { mb_getopt_args_free(argc, argv); free(gmt_args); GMT_Destroy_Options(API, &gmt_options); return (code); }
#define Return(code) { gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_@@MOD@@(void *V_API, int gmt_mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_@@MOD@@(void *V_API, int gmt_mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *gmt_options = NULL;
	char *gmt_args = NULL;
	char **argv = NULL;
	int argc = 0;
	struct mb_getopt_state getopt_state;
	mb_getopt_init(&getopt_state);

	if (!API) return GMT_NOT_A_SESSION;
	if (gmt_mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);

	/* the program's own argv[], whatever shape GMT handed us */
	argc = mb_getopt_args_build(THIS_MODULE_NAME, gmt_mode, args, &argv);
	if (argc == 2 && (strcmp(argv[1], "-") == 0 || strcmp(argv[1], "?") == 0))
		bailout(usage(API, GMT_USAGE));
	if (argc == 2 && strcmp(argv[1], "+") == 0)
		bailout(usage(API, GMT_SYNOPSIS));

	gmt_args = mb_gmt_options_string(argc, argv);
	gmt_options = GMT_Create_Options(API, GMT_MODULE_CMD, (gmt_args != NULL && gmt_args[0] != '\\0') ? gmt_args : NULL);
	if (API->error) bailout(API->error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, NULL, &gmt_options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, gmt_options)) Return(API->error);
'''


def main():
    spec = SPEC
    name = spec['name']
    rel = spec.get('source', 'utilities/%s.cc' % name)
    src = ROOT + rel
    L = open(src, encoding='utf-8', newline='').read().replace('\r\n', '\n').split('\n')

    first_inc = next(i for i, l in enumerate(L) if l.startswith('#include'))
    main_i = next(i for i, l in enumerate(L) if re.match(r'^int main\(int argc, char \*\*argv\)\s*\{', l))
    main_end = max(i for i, l in enumerate(L) if l.rstrip() == '}' and i > main_i)
    inc_end = first_inc
    for i in range(first_inc, main_i):
        if L[i].startswith('#include') or L[i].startswith('#if') or L[i].startswith('#else') \
                or L[i].startswith('#endif') or L[i].strip() == '':
            inc_end = i
        else:
            break
    header = L[:first_inc]
    while header and header[-1].strip() == '':
        header.pop()
    includes = convert_includes(L[first_inc:inc_end + 1])
    pre = [conv_line(l, False, True) for l in L[inc_end + 1:main_i]]
    body = [conv_line(l, True, False) for l in L[main_i + 1:main_end]]

    mod = spec.get('module', name)
    out = list(header)
    out += ['/*',
            ' * GMT-module port of src/%s. The program\'s getopt_long() option loop' % rel,
            ' * is kept as it is, running on the reentrant mb_getopt_long() (the state',
            ' * lives in a local structure, so the module can run any number of times in',
            ' * one GMT session), and main() becomes GMT_%s(), with every exit()' % mod,
            ' * turned into Return().',
            ' */',
            '',
            '#define THIS_MODULE_NAME "%s"' % mod,
            '#define THIS_MODULE_LIB "mbsystem"',
            '#define THIS_MODULE_PURPOSE "%s"' % spec['purpose'],
            '/* %s */' % spec['keys_comment'],
            '#define THIS_MODULE_KEYS "%s"' % spec['keys'],
            '#define THIS_MODULE_NEEDS ""',
            '#define THIS_MODULE_OPTIONS "->V"',
            '',
            '#include "gmt_dev.h"',
            '']
    out += includes
    out.append('#include "mb_getopt.h"')
    out.append('')
    out += pre
    out.append(TEMPLATE.replace('@@MOD@@', mod).replace('@@USAGE@@', spec.get('usage_var', 'usage_message'))
               .replace('@@HELP@@', spec.get('help_var', 'help_message')))
    out += body
    out.append('}')
    out.append('/*--------------------------------------------------------------------*/')
    text = '\n'.join(out) + '\n'
    dst = ROOT + 'gmt/%s.c' % mod
    open(dst, 'w', encoding='utf-8', newline='').write(text)
    print('wrote', dst)
    for l in text.split('\n'):
        if re.search(r'nullptr|constexpr|static_cast|std::|\bnew\s+\w|delete\s*\[|\bauto\b|template\s*<|::', l) and not l.strip().startswith(('*', '/*', '//')):
            print('CHECK:', l.strip()[:130])


SPECS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gen_port_specs.json')))
for SPEC_NAME in sys.argv[1:] or sorted(SPECS):
    SPEC = SPECS[SPEC_NAME]
    main()
