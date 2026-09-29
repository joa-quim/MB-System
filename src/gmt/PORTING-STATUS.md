# GMT module porting status

Which MB-System programs are available as GMT modules in the `mbsystem`
supplement, and which are not. Counts taken from a full Windows/MSVC build.

## Ported (56 modules in mbsystem.dll)

mb7k2jstar, mbabsorption, mbareaclean, mbauvloglist, mbbackangle, mbclean, mbconfig,
mbcontour, mbcopy, mbctdlist, mbdatalist, mbdefaults, mbdumpesf, mbextractsegy, mbfilter,
mbfnv2navlab, mbformat, mbgetdata, mbgetesf, mbgpstide, mbgrd2gltf, mbgrd2obj, mbgrdtiff,
mbgrid, mbhistogram, mbimport, mbinfo, mblevitus, mblist, mbmakedatalist, mbmakeplatform,
mbmapscale, mbmesh, mbminirovnav, mbmosaic, mbnavadjustmerge, mbnavlab2fnv, mbnavlist,
mbotps, mbpreprocess, mbprocess, mbrolltimelag, mbroutetime, mbsegygrid, mbsegyinfo,
mbsegylist, mbsegypsd, mbset, mbsslayout, mbsvplist, mbsvpselect, mbswath, mbswath2las,
mbtime, mbusbl2fnv, mbvoxelclean

`mbgetdata` and `mbimport` are GMT-only: they have no command-line counterpart.

## Not ported

Every command-line program that was a candidate is now a module. What is left
was never a candidate:

| group | candidates? |
|---|---|
| `src/mbtrn`, `src/mbtrnav`, `src/mbtrnutils` | no - TRN daemons, servers, socket/serial CLI tools |
| `test/mbio`, `src/mbmesh` tests | no - test binaries |
| `dump_gsf`, `man2html` | no - developer / build tools |
| CMake compiler-id probes | no - build artifacts |

## How a port is done

Three shapes.

**Verbatim option loop** (the 16 modules added last: mb7k2jstar, mbauvloglist,
mbdefaults, mbdumpesf, mbfnv2navlab, mbmakeplatform, mbminirovnav,
mbnavadjustmerge, mbnavlab2fnv, mbrolltimelag, mbroutetime, mbsegygrid,
mbsegylist, mbsegypsd, mbsslayout, mbusbl2fnv): the whole program text is kept,
comments included, and its own `getopt_long()` loop runs unchanged on
`mb_getopt_long()` (`mb_getopt.c`), a reentrant version whose state lives in a
local structure - so a module can run any number of times in one GMT session,
and long-only options (several of these programs have no short options at all)
keep working. `mb_getopt_args_build()` rebuilds `argc/argv` from whichever
shape GMT hands the module; GMT itself only sees `-V` and `-I`. These files are
produced by a generator that does the mechanical C++ -> C changes (headers,
`nullptr`, `constexpr`, casts, `std::min/max`, `static` on file-scope
definitions, `exit()`/`return` -> `Return()`); fix the program, regenerate the
module.

**C or C-like `.cc` sources** (most of `src/utilities`): the module is a single
C file in `src/gmt`. Replace `main()` and its `getopt` loop with the GMT
scaffolding - `THIS_MODULE_*` macros, a `<NAME>_CTRL` structure with one
sub-structure per option letter, `New_*_Ctrl` / `Free_*_Ctrl`, `usage()`,
`parse_*()`, and `GMT_<name>()` holding the old body with locals seeded from
`Ctrl`. Every `exit()` becomes `Return(code)`. See `mbgrid.c`.

**C++ sources whose pipeline cannot be rewritten in C** (`mbmesh`, and
`mbgrd2gltf`): keep the module itself plain C and reach the C++ through an
`extern "C"` bridge that lives beside the C++ library, not in `src/gmt`. The
bridge exposes opaque handles plus the accessors the driver needs for its
progress reporting, and carries no logic of its own. See `mbmesh.c` with
`src/mbmesh/mbmesh_capi.h` / `.cpp`.

`gmt_dev.h` cannot be compiled as C++ (`postscriptlight.h` has a member named
`try`, and `gmt_hidden.h` relies on implicit `void*` conversions), so the GMT
side is C in both shapes.

## Verifying a port

The supplement does not have to be installed. Put

```
GMT_CUSTOM_LIBS = <build>/src/gmt/mbsystem.dll
```

in a `gmt.conf` inside a scratch directory, point `GMT_USERDIR` at that
directory, and both `gmt <module>` and `GMT_Encode_Options` will resolve the
module. `PATH` needs the GMT `bin` plus `build/src/{mbio,mbaux,bsio,gsf,mbbitpack,surf}`
and the netCDF / PROJ / GDAL / glib / pthreads `bin` directories; a missing
dependency shows up as exit code 53 or `0xC0000135`, not as a missing module.

Four checks are worth running on every new module:

1. **Same output as the program it was ported from.** Run both on
   `test/utilities/testdata/<format>/`, then compare the products rather than
   the console: `gmt grd2xyz` on each grid and `diff`. Expect the grid data to
   match exactly; the netCDF `Command`/`Remark` strings will differ, because
   the program records its own `argv[0]` and the module has none.
2. **Both invocation shapes.** `gmt <module> <args>` (the `argv[]` form) and a
   single command string through the C API exercise different code paths - see
   the argument-shape requirement below. A module can work through one and
   ignore every argument through the other.
3. **Both long-option spellings**, `--opt=value` and `--opt value`, since the
   pre-parser handles them separately.
4. **The external-API path.** `GMT_Encode_Options(API, "<module>", -1, &head,
   &n)` should report "every key already satisfied" when the input option is
   given explicitly, and bind one input resource when it is not. That is what
   GMT.jl does before calling the module.

Two traps when running the tests themselves:

- **Run them from bash, not PowerShell.** PowerShell mangles arguments such as
  `-Idatalist.mb-1` (it eats the trailing `-1`). Use `MSYS2_ARG_CONV_EXCL='*'`
  in Git-Bash.
- **`test/utilities/testdata/mb11/TN136HS.309.snipped.mb11`** has 19 bathymetry
  beams, 0 amplitude beams and 0 sidescan pixels, which makes it a good probe
  for `beams_amp` / `beams_bath` / `pixels_ss` array-sizing bugs. Two such bugs
  in `mbmosaic` only appeared on this file.

## Gotchas worth knowing before starting one

- **GMT reserves option letters**, but only some uses actually break.
  `-B -J -K -O -P -R -U -V -X -Y` and the lowercase common set are handled by
  GMT before the module's parser runs. What bites is GMT's history expansion,
  which only applies to a reserved letter given *without* an argument: a bare
  `-B` fails with "Found no history for option -B". A reserved letter that
  always carries an argument passes through to the module untouched -
  `mbgrid` and `mbmosaic` both use `-B<border>`, `-J<projection>`,
  `-R<bounds>`, `-X<extend>` and `-Y<...>` with no trouble, as long as
  `THIS_MODULE_OPTIONS` leaves them out (`"->V"`). Where a program uses a
  reserved letter as a bare switch, give the module a free letter and have the
  long-option pre-parser rewrite both the long form and the legacy short form
  onto it (`mbgrd2gltf` does this: `--binary` and `-B` both become `-Z`).
- **Support sources that are not modules** go in `MBSYSTEM_EXTRA_SRCS`, not
  `MBSYSTEM_MODULE_SRCS`: the moduleinfo generator scrapes `THIS_MODULE_*`
  from every entry of the latter.
- **Header-only libraries** instantiated with a `*_IMPLEMENTATION` define
  collide once two pipelines land in the same supplement; instantiate once in
  a small library everything links (see `src/mbgrd2gltf/tinygltf_impl.cpp`).
- **Watch for link cycles** when splitting a program into a pipeline library:
  the pipeline must not link `mbsystem`, since the supplement links it.
- **Do not wrap a C-like program's `main()`.** Compiling the `.cc` into a
  static library and handing it a rebuilt `argv[]` is not a port: `getopt`'s
  `optind` is never reset, so a second call in the same GMT session (Julia,
  PyGMT) parses nothing; the GMT verbosity level leaks in as several `-V`s;
  and C++ ends up in the supplement. `mbprocess`, `mbdatalist`, `mbgetesf`,
  `mbsvplist`, `mbctdlist`, `mbsvpselect` and `mbmakedatalist` were first done
  that way and had to be redone as real C ports.
- **A GMT session switches the Windows C runtime to binary file mode.** Any
  port that opens text with `fopen(..., "r")` and compares whole lines ending
  in `"\n"` (or trims only `'\n'`) breaks on CRLF files inside the module while
  the standalone program works. Open such files with `"rt"` on `_WIN32`
  (`mbsvpselect.c` does). MBIO's own parsers tolerate the `'\r'`.
- **Test a second call in one session**, not just one call per process: stale
  file-scope state (globals, `optind`) only shows up there.
- **No new runtime DLL dependencies.** `mbprocess.c` runs its threads on native
  Win32 threads on Windows: linking pthreads would make `mbsystem.dll` import
  `pthreadVC2_w64.dll`, which the host program's DLL search did not find.

## Requirements every module must meet

- **`THIS_MODULE_KEYS` must not be empty.** With no keys `GMT_Encode_Options`
  encodes no resources and the module cannot be called from Julia, PyGMT or
  MATLAB at all. Use `ID{` when `-I` is the input (a positional argument from an
  external still binds to it); add `>D}` only when the module's product really
  is a table on stdout. Do not declare an output key for a module that writes
  its own files - a promised resource that is never registered leaves an
  external caller waiting for data that never arrives.
- **Know which shape the arguments arrive in.** `GMT_<module>(API, mode, args)`
  gets an `argv[]` array of `mode` entries when `mode > 0` (this is what the
  `gmt` executable does), a single command string when `mode == GMT_MODULE_CMD`
  (which is `0`, and is what the C API and the external interfaces use), and a
  ready-made option list when `mode < 0`. A module that preparses long options
  must join the `argv[]` form into one string first; treating only
  `GMT_MODULE_CMD` as text and handing GMT an empty string for the rest makes
  `gmt <module> <args>` silently ignore every argument and print the usage.
  See `join_args()` in `mbmosaic.c`.
- **Long options need handling in the module.** GMT only translates long
  options for modules with a `GMT_KEYWORD_DICTIONARY`, which an out-of-tree
  supplement cannot have. If the original used `getopt_long`, re-implement the
  mapping before `GMT_Create_Options` sees the command line. See
  `preparse_long_options()` in `mbmesh.c`.
- **Results go to stdout with `fprintf`, never `GMT_Report`.**
  `GMT_MSG_NORMAL` renders as `[ERROR]` on stderr, so redirecting the module's
  output would produce an empty file. The two ported shapes differ here and
  the simpler one is preferred: `mbgrid.c` rewrote every `fprintf(outfp, ...)`
  as `GMT_Report`, which sends its progress report to stderr tagged
  `[ERROR]`; `mbmosaic.c` instead keeps the original's
  `FILE *outfp = (verbose >= 2) ? stderr : stdout`, which is both a smaller
  diff and the behaviour a user redirecting the output expects.
- **No `exit()`.** It terminates the whole GMT session.

## Open defects in the ported modules

Found while porting `mbmosaic`, not yet acted on because each changes the
behaviour of a module that is already in use:

| module | defect | effect |
|---|---|---|
| `mbgrdtiff.c`, `mbgrdtifforg.c`, `mbswath.c` | `THIS_MODULE_KEYS ""` | not callable from Julia, PyGMT or MATLAB at all. These are plot modules, so the keys should describe their table/CPT inputs and their PostScript output, along the lines of `mbcontour`'s `"<G{+,>}"`. |
| `mbgrid.c` | `THIS_MODULE_KEYS "ID{,OG}"` | `-O` is a file *root*, and the module writes its grids itself with `mb_write_gmt_grd()`; it never registers a grid with GMT. The promised output resource is never delivered, so an external caller waits for data that never arrives. `mbmosaic`, which writes grids the same way, declares `"ID{"` only. |

The two `mbmosaic` array-sizing bugs found at the same time (`gangles`,
`slopes` and `priorities` registered as `MB_MEM_TYPE_AMPLITUDE` although they
are filled to `beams_bath`, and `slopes` never registered in the second
gridding pass) were fixed in both `src/gmt/mbmosaic.c` and the original
`src/utilities/mbmosaic.cc`.

## Known environment issue

GMT auto-loads an older `mbgmt_w64.dll` from `<gmt>/bin/gmt_plugins`, and its
module names take precedence over the `mbsystem` ones. While it is installed,
these names resolve to the old library: mbareaclean, mbclean, mbcontour,
mbgetdata, mbgrd2obj, mbgrdtiff, mbinfo, mbswath. Remove it to use the new
supplement, or check the `[<lib>]` tag GMT prints in the module banner.
