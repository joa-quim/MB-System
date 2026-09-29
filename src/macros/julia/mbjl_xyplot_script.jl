#--------------------------------------------------------------------
#    The MB-system:  mbjl_xyplot_script.jl
#
#    Copyright (c) 2026 by
#    David W. Caress (caress@mbari.org)
#      Monterey Bay Aquarium Research Institute
#      Moss Landing, California, USA
#
#    See README.md file for copying and redistribution conditions.
#--------------------------------------------------------------------
#
# Purpose:
#   Julia port of src/macros/python/mbpy_xyplot_script.py, itself a port
#   of mbm_xyplot: page/scale-fit layout (shared with mbm_histplot -- see
#   below) plus the GMT **modern mode** script generator for plotting one
#   or more xy data files.
#
#   mbm_xyplot's own page/scale-fit algorithm (its subs GetPageSize,
#   GetProjection, GetBaseTick, GetBaseTickLinear, and the main-body
#   scaling logic around "get user constraints on map scale" through
#   "place the origin") was confirmed, by direct diff against both
#   mbm_grdplot and mbm_histplot, to be byte-for-byte identical (modulo
#   whitespace) to mbm_histplot's own copies of the same subs -- NOT
#   mbm_grdplot's (which uses a different, scale_loc-aware, clamped
#   GetPageSize and a gridprojected-aware GetBaseTick; see
#   mbjl_histplot_script.jl's own top comment for how that was
#   established). get_page_size_hist()/get_base_tick_linear()/
#   get_base_tick_geo() are therefore imported and reused here rather
#   than re-derived, the same way mbjl_histplot_script.jl already
#   imports baseline_gmt_defaults() from mbjl_grdplot_script.jl.
#
#   A real, confirmed bug was found in mbm_xyplot's own -MGT (text
#   label) misc-option parsing while comparing it against mbm_grdplot's
#   otherwise-identical -MGT handling (mbm_xyplot lines ~245-260): the
#   outer match tests for "GT" but the *inner* extraction regex tests
#   for "GP" (probably a copy/paste slip), so $txt is always undefined
#   and every -MGT text label mbm_xyplot has ever been given is silently
#   discarded ("Invalid text label ignored"), regardless of validity.
#   Moot for this port: like mbm_grdplot's own Python/Julia replacement,
#   this tool exposes text labels as a plain --text long option (see
#   mbjl_xyplot) instead of reproducing the "-M<sub>:<sub>:..." mini-
#   language at all, so the bug has nothing to be ported *into*.
#
#   The other real, deliberate change from the classic macro: mbm_xyplot
#   preprocesses every input file (delimiter-splitting, per-file column-
#   selection/math via its "-IC..." mini-language) by writing a
#   *persistent* temporary file "<file><index>.<pid>" into the current
#   working directory for every run -- left behind unless the user also
#   passes -Z, and even then only actually deleted if the resulting
#   command script is executed with -Z's cleanup lines intact. This was
#   the whole reason this replacement was requested. Here, the
#   equivalent column-selection/math is instead performed:
#     - once in Julia, in scan_xy_bounds(), purely in memory, to find
#       the auto-fit plot bounds (no file written at all); and
#     - at plot time, by piping each input file through a small `awk`
#       program straight into `gmt plot`'s stdin (see _plot_pipeline())
#       -- a live, ephemeral Unix pipe, not a file on disk.
#   No file this tool writes outlives the plotting script itself except
#   the final plot.
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))
isdefined(@__MODULE__, :MbjlLayout) || include(joinpath(@__DIR__, "mbjl_layout.jl"))
isdefined(@__MODULE__, :MbjlMisc) || include(joinpath(@__DIR__, "mbjl_misc.jl"))
isdefined(@__MODULE__, :MbjlGrdplotScript) || include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))
isdefined(@__MODULE__, :MbjlHistplotScript) || include(joinpath(@__DIR__, "mbjl_histplot_script.jl"))

module MbjlXyplotScript

using ..MbjlUtil
using ..MbjlLayout
using ..MbjlMisc: CoastOptions
using ..MbjlGrdplotScript: baseline_gmt_defaults
using ..MbjlHistplotScript: get_page_size_hist, get_base_tick_linear, get_base_tick_geo

export XYFileEntry, scan_xy_bounds, LayoutXY, compute_layout_xy, ScriptOptionsXY,
       generate_script_xy, generate_and_write_xy

const layout = MbjlLayout


# ---------------------------------------------------------------------------
# Column-expression mini-language (mbm_xyplot's own "-IC<xexpr>_<yexpr>"):
# "c[N]" selects field N (1-indexed), "#" is the input line number. mbm_xyplot
# itself just textually substitutes these into a string handed to Perl's
# eval(); the same textual substitution is used here, targeting first a
# small, safely-sandboxed expression evaluator (for scan_xy_bounds()'s
# in-memory bounds computation) and separately an `awk` expression (for the
# actual plot-time pipeline) -- see _expr_to_jl()/_expr_to_awk().
#
# The Python original evaluates its substituted string with eval() and an
# empty __builtins__, which admits exactly Python's arithmetic operators on
# numbers, _col(N) and _linecnt. The Julia port parses the substituted
# string once (Meta.parse) and walks the resulting expression tree itself,
# admitting the same operator set with Python's semantics (true division,
# "**" power, "//" floor division, "%" modulo with the divisor's sign, and
# comparisons yielding 1.0/0.0); anything else raises and the line is
# skipped, just as Python's eval() failure skips it.
# ---------------------------------------------------------------------------

const _COL_RE = r"c\[(\d+)\]"


function _expr_to_jl(expr::AbstractString)::String
    e = replace(expr, _COL_RE => s"_col(\1)")
    e = replace(e, "#" => "_linecnt")
    # Python operators with no Julia spelling of the same meaning
    e = replace(e, "**" => "^")
    return replace(e, "//" => "÷")
end


function _expr_to_awk(expr::AbstractString)::String
    e = replace(expr, _COL_RE => s"$\1")
    e = replace(e, "#" => "NR")
    return replace(e, "**" => "^")
end


function _awk_string_literal(s::AbstractString)::String
    escaped = replace(replace(s, "\\" => "\\\\"), "\"" => "\\\"")
    return "\"$escaped\""
end


_awk_regex_escape(ch::AbstractString)::String =
    join((c in ".^\$*+?()[]{}|\\" ? "\\" * c : string(c)) for c in ch)


"""Evaluate a parsed column expression (see the section comment above).
`col(n)` returns field n as Float64; `linecnt` is the line number."""
function _eval_expr(ex, col, linecnt)::Float64
    if ex isa Number
        return Float64(ex)
    elseif ex === :_linecnt
        return Float64(linecnt)
    elseif ex isa Expr && ex.head === :call
        f = ex.args[1]
        if f === :_col && length(ex.args) == 2 && ex.args[2] isa Integer
            return col(Int(ex.args[2]))
        end
        a = [_eval_expr(x, col, linecnt) for x in ex.args[2:end]]
        if length(a) == 1
            f === :- && return -a[1]
            f === :+ && return a[1]
        elseif length(a) >= 2 && (f === :+ || f === :*)
            return f === :+ ? sum(a) : prod(a)
        elseif length(a) == 2
            x, y = a
            f === :- && return x - y
            f === :/ && (y == 0 ? throw(DivideError()) : return x / y)
            f === :^ && return x^y
            f === :÷ && (y == 0 ? throw(DivideError()) : return fld(x, y))
            f === :% && (y == 0 ? throw(DivideError()) : return mod(x, y))
            f === :< && return Float64(x < y)
            f === :> && return Float64(x > y)
            f === :<= && return Float64(x <= y)
            f === :>= && return Float64(x >= y)
            f === :(==) && return Float64(x == y)
            f === :!= && return Float64(x != y)
        end
        error("unsupported operation in column expression: $f")
    elseif ex isa Expr && ex.head === :comparison
        # chained comparison, e.g. 0 < c[3] < 10
        vals = [_eval_expr(ex.args[i], col, linecnt) for i in 1:2:length(ex.args)]
        for (k, i) in enumerate(2:2:length(ex.args))
            op = ex.args[i]
            x, y = vals[k], vals[k+1]
            ok = op === :< ? x < y : op === :> ? x > y : op === :<= ? x <= y :
                 op === :>= ? x >= y : op === :(==) ? x == y : op === :!= ? x != y :
                 error("unsupported comparison in column expression: $op")
            ok || return 0.0
        end
        return 1.0
    end
    error("unsupported column expression: $ex")
end


"""One -I input file plus its (possibly per-file-overridden) styling
and column-selection, i.e. one iteration of mbm_xyplot's @xyfiles/
@xysymbols/@xyfills/@xypens/@delimiters/@xmath/@ymath parallel
arrays (mbm_xyplot lines ~413-476)."""
Base.@kwdef mutable struct XYFileEntry
    file::String
    xexpr::String = "c[1]"
    yexpr::String = "c[2]"
    delimiter::Union{Nothing,String} = nothing   # nothing => default whitespace splitting
    symbol::String = "N"
    fill::String = "N"
    pen::String = "N"
end


"""Port of mbm_xyplot's own per-line data extraction and bounds-finding
(lines ~499-596): read every entry's file, split each line on its
delimiter, evaluate its x/y column-expression, and track the running
min/max over *all* files combined (mbm_xyplot's @xvalues/@yvalues are
global across every -I file, not per-file). Lines containing ">"
(psxy/plot multi-segment markers) are skipped, matching mbm_xyplot's
own segment-header passthrough. Unlike mbm_xyplot, nothing is written
to disk -- this is purely an in-memory scan.

NOTE: mbm_xyplot's own per-line numeric-result validation regex
("-?\\d*\\.?\\d*|-?\\.\\d+") is satisfied by the empty string (every
branch of the alternation is fully optional), so it never actually
rejects anything; the only input that is really ever skipped in
practice is one whose column-expression throws in eval() (chiefly a
literal divide-by-zero -- Perl is otherwise very lenient, e.g. an
out-of-range c[N] just evaluates as 0, not an error) or whose result
stringifies to contain "nan". That real behavior -- skip with a
warning on eval failure or a non-finite result -- is what is
reproduced below, not the vestigial regex.
"""
function scan_xy_bounds(entries::AbstractVector{XYFileEntry})
    xmin = ymin = Inf
    xmax = ymax = -Inf

    for entry in entries
        jl_x = try Meta.parse(_expr_to_jl(entry.xexpr)) catch; nothing end
        jl_y = try Meta.parse(_expr_to_jl(entry.yexpr)) catch; nothing end
        linecnt = 1
        # Python's text-mode iteration: universal newlines, final line
        # kept even without a trailing newline
        text = read(entry.file, String)
        rawlines = split(text, r"\r\n|\n|\r")
        if !isempty(rawlines) && isempty(rawlines[end])
            pop!(rawlines)
        end
        for raw_line in rawlines
            line = String(raw_line)
            if occursin(">", line)
                linecnt += 1
                continue
            end

            if entry.delimiter !== nothing && !isempty(entry.delimiter)
                if startswith(line, entry.delimiter)
                    line = line[nextind(line, 0, length(entry.delimiter) + 1):end]
                end
                fields = split(line, entry.delimiter)
            else
                fields = split(line)
            end

            _col(n::Int) = (1 <= n <= length(fields)) ? pyfloat(fields[n]) :
                           throw(BoundsError(fields, n))

            local xval, yval
            try
                (jl_x === nothing || jl_y === nothing) && error("unparseable column expression")
                xval = _eval_expr(jl_x, _col, linecnt)
                yval = _eval_expr(jl_y, _col, linecnt)
                if !(isfinite(xval) && isfinite(yval))
                    error("non-finite result")
                end
            catch
                println(stderr,
                    "WARNING!!! NON-NUMERIC RESULT DETECTED! " *
                    "Skipping line $linecnt of $(entry.file)...")
                linecnt += 1
                continue
            end

            xmin, xmax = min(xmin, xval), max(xmax, xval)
            ymin, ymax = min(ymin, yval), max(ymax, yval)
            linecnt += 1
        end
    end

    if !isfinite(xmin)
        throw(ArgumentError("no valid xy data found in any --input file"))
    end
    return xmin, xmax, ymin, ymax
end


"""Port of mbm_xyplot's -R handling (lines ~587-635): a plain
"w/e/s/n" region overrides xmin/xmax/ymin/ymax (each field individually
run through GetDecimalDegrees, so dd:mm:ss values are accepted) but is
otherwise passed through to GMT's own -R verbatim; a "...r" suffixed
region gives two corner points instead of a w/e/s/n range (also passed
through verbatim -- GMT's -R itself understands the trailing "r"); a
bare "r" reformats the already-known data bounds into that corner-point
form. With no -R at all, a fresh "w/e/s/n" string is built from the
data bounds.

NOTE: mbm_xyplot's own Perl silently falls through to the data-bounds
default if -R matches none of these three forms (a malformed -R value
is simply ignored with no warning at all, because none of its 'if'/
'elsif' branches assign \$bounds_plot, which is only caught by a later
independent "if (!\$bounds_plot)" default-fill check). Raising here
instead is a deliberate improvement -- failing loudly on a malformed
--region beats silently discarding the user's actual request.

A second, separate deliberate addition not present in mbm_xyplot at
all: a degenerate region (xmin == xmax, or ymin == ymax -- e.g. a real
"heave" column that is exactly 0.0 for an entire dataset because that
sensor never logged a nonzero value) is padded by a small margin
rather than passed through as-is. mbm_xyplot itself has no such
handling either, but neither classic nor modern GMT can compute a
projection over a zero-width axis (`gmt mapproject`/`gmt basemap`
simply error out: "unable to init projection"-style failures),
so an unpadded degenerate region does not faithfully reproduce a
quirky-but-working classic-mode plot -- it can only ever crash,
Perl, Python and Julia alike. Padding is the only way to actually get a
(flat-line) plot out for this real, unremarkable case.
"""
function _resolve_bounds(xmin::Real, xmax::Real, ymin::Real, ymax::Real,
                         region::Union{Nothing,AbstractString})
    if region === nothing || isempty(region)
        xmin, xmax = _pad_degenerate(xmin, xmax)
        ymin, ymax = _pad_degenerate(ymin, ymax)
        return xmin, xmax, ymin, ymax, "$(fmtg(xmin, 8))/$(fmtg(xmax, 8))/$(fmtg(ymin, 8))/$(fmtg(ymax, 8))"
    end
    region = String(region)

    m = match(r"^(\S+)/(\S+)/(\S+)/(\S+)r$", region)
    if m !== nothing
        xmin_raw, ymin_raw, xmax_raw, ymax_raw = m.captures
        xmin = layout.get_decimal_degrees(xmin_raw)
        xmax = layout.get_decimal_degrees(xmax_raw)
        ymin = layout.get_decimal_degrees(ymin_raw)
        ymax = layout.get_decimal_degrees(ymax_raw)
        if xmin == xmax || ymin == ymax
            xmin, xmax = _pad_degenerate(xmin, xmax)
            ymin, ymax = _pad_degenerate(ymin, ymax)
            region = "$(fmtg(xmin, 8))/$(fmtg(ymin, 8))/$(fmtg(xmax, 8))/$(fmtg(ymax, 8))r"
        end
        return xmin, xmax, ymin, ymax, region
    end

    m = match(r"^(\S+)/(\S+)/(\S+)/(\S+)$", region)
    if m !== nothing
        xmin_raw, xmax_raw, ymin_raw, ymax_raw = m.captures
        xmin = layout.get_decimal_degrees(xmin_raw)
        xmax = layout.get_decimal_degrees(xmax_raw)
        ymin = layout.get_decimal_degrees(ymin_raw)
        ymax = layout.get_decimal_degrees(ymax_raw)
        if xmin == xmax || ymin == ymax
            xmin, xmax = _pad_degenerate(xmin, xmax)
            ymin, ymax = _pad_degenerate(ymin, ymax)
            region = "$(fmtg(xmin, 8))/$(fmtg(xmax, 8))/$(fmtg(ymin, 8))/$(fmtg(ymax, 8))"
        end
        return xmin, xmax, ymin, ymax, region
    end

    if region == "r"
        xmin, xmax = _pad_degenerate(xmin, xmax)
        ymin, ymax = _pad_degenerate(ymin, ymax)
        return xmin, xmax, ymin, ymax, "$(fmtg(xmin, 8))/$(fmtg(ymin, 8))/$(fmtg(xmax, 8))/$(fmtg(ymax, 8))r"
    end

    throw(ArgumentError("unrecognized --region spec: $(repr(region))"))
end


"""If vmin == vmax (a constant data column, e.g. an all-zero heave
trace), pad symmetrically so the value plots as a visible flat line
instead of a zero-width axis that GMT cannot project at all. See
_resolve_bounds()'s docstring."""
function _pad_degenerate(vmin::Real, vmax::Real)
    if vmin != vmax
        return vmin, vmax
    end
    pad = abs(vmin) * 0.05
    pad == 0 && (pad = 1.0)
    return vmin - pad, vmax + pad
end


Base.@kwdef mutable struct LayoutXY
    pagesize::String
    projection::String
    projection_pars::String
    region::String            # the resolved -R value (verbatim user string, or data-derived)
    xoffset::Float64
    yoffset::Float64
    axes_interval::String      # tick portion only of -B (no WESN/title -- see generate_script_xy)
    degree_format::String
end


_truthy(x) = x !== nothing && x != 0


"""Port of mbm_xyplot's page/scale-fit algorithm (lines ~587-1254).
`data_xmin/xmax/ymin/ymax` are only used as the auto-fit bounds when
`region` is not given -- see _resolve_bounds(). This is otherwise the
same algorithm as MbjlHistplotScript.compute_layout_hist(): see this
module's own top comment for why the two are ported jointly.
"""
function compute_layout_xy(
    data_xmin::Real, data_xmax::Real, data_ymin::Real, data_ymax::Real;
    pagesize::AbstractString = "a",
    map_scale::Union{Nothing,AbstractString} = nothing,
    orientation::Integer = 0,
    region::Union{Nothing,AbstractString} = nothing,
    tick_info::Union{Nothing,AbstractString} = nothing,
    xlabel::AbstractString = " ",
    ylabel::AbstractString = " ",
)::LayoutXY
    pagesize = String(pagesize)
    map_scale = (map_scale === nothing || isempty(map_scale)) ? nothing : String(map_scale)
    xmin, xmax, ymin, ymax, bounds_plot = _resolve_bounds(data_xmin, data_xmax, data_ymin, data_ymax, region)

    # get plot degree annotation format (mbm_xyplot lines ~1042-1068; the
    # "!$gridprojected" guard there is unconditionally true -- mbm_xyplot
    # has no grid at all and never sets $gridprojected -- so it always runs)
    xsize, ysize = xmax - xmin, ymax - ymin
    size = min(xsize, ysize)
    if size > 1.0
        degree_format = "ddd"
    elseif size > (1.0 / 60.0)
        degree_format = "ddd:mm"
    else
        degree_format = "ddd:mm:ss"
    end

    margins = get_page_size_hist(pagesize)

    use_scale = use_width = use_ratio = linear = false
    separator = ""
    trial_value = "1.0"
    plot_scale = plot_width = nothing
    projection = projection_pars = nothing

    if map_scale !== nothing
        spec = layout.get_projection(map_scale)
        projection = spec.projection
        projection_pars = spec.projection_pars
        use_scale, use_width = spec.use_scale, spec.use_width
        use_ratio = spec.use_ratio
        separator, trial_value = spec.separator, spec.trial_value
        plot_scale, plot_width = spec.plot_scale, spec.plot_width

        # BUG FIX (present in mbpy_xyplot_script.py, not in mbm_xyplot):
        # the Python port took `linear` from the shared get_projection(),
        # whose mbm_grdplot semantics set it for EVERY x/X spec. mbm_xyplot
        # itself instead uses $geographic, which its own GetProjection sets
        # only for the literal "xd"/"Xd" flag -- and it also chop()s the
        # trailing "d" off the *global* $map_scale there, so the later
        # re-derivation of projection_pars from $map_scale sees "x"/"X".
        # The Python port missed both halves: "-J X" came out as the
        # geographic "-JX6.5d" (never reaching the independent x/y fill
        # branch below), and "-J xd"/"-J Xd" came out as "-Jxd18.367d",
        # which GMT rejects outright ("d18.367 not a valid number").
        # Confirmed by running the generated scripts through GMT 6.
        linear = map_scale == "xd" || map_scale == "Xd"
        if linear
            map_scale = map_scale[1:end-1]
        end

        if (use_scale && _truthy(plot_scale)) || (use_width && _truthy(plot_width))
            # projection/projection_pars as parsed
        elseif use_scale || use_width
            projection_pars = "$(projection_pars)$(separator)$(trial_value)"
        end
    else
        projection, projection_pars = "x", "1/1"
        use_scale = true
    end

    dxx, dyy = layout._mapproject_bbox(xmin, xmax, ymin, ymax, projection, projection_pars, bounds_plot)
    dxx, dyy = abs(dxx), abs(dyy)

    landscape = portrait = false
    width = height = 0.0
    width_max = height_max = 0.0

    if (use_scale && _truthy(plot_scale)) || (use_width && _truthy(plot_width))
        plot_width_, plot_height_ = dxx, dyy
        if orientation == 1
            portrait = true
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
            width_max, height_max = margins.width_max_portrait, margins.height_max_portrait
        elseif orientation == 2
            landscape = true
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
            width_max, height_max = margins.width_max_landscape, margins.height_max_landscape
        elseif dxx > dyy
            landscape = true
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
            width_max, height_max = margins.width_max_landscape, margins.height_max_landscape
        else
            portrait = true
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
            width_max, height_max = margins.width_max_portrait, margins.height_max_portrait
        end

        if plot_width_ > width_max || plot_height_ > height_max
            good_page = nothing
            for elem in layout.PAGE_SIZE_NAMES
                m2 = get_page_size_hist(elem)
                wm = portrait ? m2.width_max_portrait : m2.width_max_landscape
                hm = portrait ? m2.height_max_portrait : m2.height_max_landscape
                if plot_width_ <= wm && plot_height_ <= hm
                    good_page = elem
                    break
                end
            end
            if good_page === nothing
                good_page = pagesize
            end
            pagesize = good_page
            margins = get_page_size_hist(pagesize)
            width = portrait ? layout.PAGE_WIDTH_IN[pagesize] : layout.PAGE_HEIGHT_IN[pagesize]
            height = portrait ? layout.PAGE_HEIGHT_IN[pagesize] : layout.PAGE_WIDTH_IN[pagesize]
        end

        plot_width, plot_height = plot_width_, plot_height_

    elseif use_scale && projection == "x" && !linear
        if orientation == 1
            portrait = true
            plot_scale_x = margins.width_max_portrait / dxx
            plot_scale_y = margins.height_max_portrait / dyy
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
        else
            landscape = true
            plot_scale_x = margins.width_max_landscape / dxx
            plot_scale_y = margins.height_max_landscape / dyy
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
        end

        plot_width = dxx * plot_scale_x
        plot_height = dyy * plot_scale_y
        projection_pars = "$(pystr(plot_scale_x))/$(pystr(plot_scale_y))"

    elseif use_width && projection == "X" && !linear
        if orientation == 1
            portrait = true
            plot_width_x = margins.width_max_portrait
            plot_width_y = margins.height_max_portrait
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
        else
            landscape = true
            plot_width_x = margins.width_max_landscape
            plot_width_y = margins.height_max_landscape
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
        end

        plot_width, plot_height = plot_width_x, plot_width_y
        projection_pars = "$(pystr(plot_width_x))/$(pystr(plot_width_y))"

    elseif use_scale
        plot_scale_landscape = margins.width_max_landscape / dxx
        if plot_scale_landscape * dyy > margins.height_max_landscape
            plot_scale_landscape = margins.height_max_landscape / dyy
        end
        plot_scale_portrait = margins.width_max_portrait / dxx
        if plot_scale_portrait * dyy > margins.height_max_portrait
            plot_scale_portrait = margins.height_max_portrait / dyy
        end

        if orientation == 1
            portrait = true
            plot_scale = plot_scale_portrait
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
        elseif orientation == 2
            landscape = true
            plot_scale = plot_scale_landscape
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
        elseif plot_scale_landscape > plot_scale_portrait
            landscape = true
            plot_scale = plot_scale_landscape
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
        else
            portrait = true
            plot_scale = plot_scale_portrait
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
        end

        plot_width = dxx * plot_scale
        plot_height = dyy * plot_scale

        base = layout._base_pars(projection, map_scale)
        if use_ratio
            top = pytrunc(1 / plot_scale)
            projection_pars = "$(base)$(separator)1:$(top)"
        else
            projection_pars = layout._sprintf_g5(base, separator, plot_scale)
        end
        if linear
            projection_pars = "$(projection_pars)d"
        end

    elseif use_width
        plot_width_landscape = min(margins.height_max_landscape * dxx / dyy, margins.width_max_landscape)
        plot_width_portrait = min(margins.height_max_portrait * dxx / dyy, margins.width_max_portrait)

        if orientation == 1
            portrait = true
            plot_width = plot_width_portrait
        elseif orientation == 2
            landscape = true
            plot_width = plot_width_landscape
        elseif plot_width_landscape > plot_width_portrait
            landscape = true
            plot_width = plot_width_landscape
        else
            portrait = true
            plot_width = plot_width_portrait
        end

        plot_height = plot_width * dyy / dxx
        if landscape
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
        else
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
        end

        base = layout._base_pars(projection, map_scale)
        projection_pars = layout._sprintf_g5(base, separator, plot_width)
        if linear
            projection_pars = "$(projection_pars)d"
        end
    end

    xoffset = (width - abs(plot_width) - margins.space_left - margins.space_right) / 2 + margins.space_left
    yoffset = (height - abs(plot_height) - margins.space_bottom - margins.space_top) / 2 + margins.space_bottom

    if tick_info !== nothing && !isempty(tick_info)
        axes_interval = "-B$(tick_info)"
    elseif first(projection, 1) in ("x", "X") && !linear
        tick_x, tick_y = get_base_tick_linear(xmin, xmax, ymin, ymax)
        axes_interval = "-Bx$(fmtg(tick_x, 6))+l\"$(xlabel)\" -By$(fmtg(tick_y, 6))+l\"$(ylabel)\""
    else
        tick = get_base_tick_geo(xmin, xmax, ymin, ymax)
        axes_interval = "-B$(tick)"
    end

    return LayoutXY(
        pagesize = pagesize, projection = projection, projection_pars = projection_pars,
        region = bounds_plot, xoffset = xoffset, yoffset = yoffset,
        axes_interval = axes_interval, degree_format = degree_format,
    )
end


Base.@kwdef mutable struct ScriptOptionsXY
    entries::Vector{XYFileEntry}
    root::String
    layout::LayoutXY
    output_format::String = "pdf"
    title::Union{Nothing,String} = nothing
    dpi::Union{Nothing,Int} = nothing
    length_scale::Union{Nothing,String} = nothing   # -MGL, emitted as -L<value> on `gmt basemap`
    coast::Union{Nothing,CoastOptions} = nothing
    text_labels::Vector{String} = String[]   # raw "tx/ty/tsize/tangle/font/just/txt"
    gmt_defs::Vector{String} = String[]
end


# A conservative "is this a plain decimal/scientific number" pattern, used
# to validate each computed x/y value in the awk pipeline below -- see
# _plot_pipeline()'s docstring for why this real check (absent from
# mbm_xyplot itself) is necessary here.
const _AWK_NUMERIC_RE = raw"^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$"


"""Build the `awk '...' file | gmt plot ...` pipeline for one input
file: a live pipe (no file written to disk at any point) that applies
the entry's delimiter-splitting and x/y column-expression, mirroring
what mbm_xyplot's own preprocessing pass wrote into a persistent
"<file><n>.<pid>" temp file instead (see this module's top comment).
A line containing ">" (a segment header) is passed through unchanged,
matching mbm_xyplot's own segment-passthrough.

Unlike mbm_xyplot's own per-line numeric-result validation (a no-op
regex -- see scan_xy_bounds()'s docstring), the check here is real: it
was added after a genuine, reproducible bug surfaced in testing. A CSV
file with an ordinary text header row (e.g. "id,lon,lat,depth") is
common real-world input; mbm_xyplot's own vestigial check lets that
header row's non-numeric evaluated "value" through unfiltered into its
temp file, same as this port's awk pipeline would without this
check -- but `gmt plot`/`psxy` infers each column's data type from the
*first* record it reads, and a text value there makes it treat the
entire file as headerless trailing text, silently discarding every
following (perfectly valid, numeric) row instead of just the one bad
line. Confirmed directly: a 20-row CSV with one such header line
rendered only a handful of stray points before this check was added.
Skipping non-numeric rows here (matching what scan_xy_bounds() already
does correctly on the Julia side) fixes it.
"""
function _plot_pipeline(entry::XYFileEntry, j::AbstractString, r::AbstractString)::String
    awk_x = _expr_to_awk(entry.xexpr)
    awk_y = _expr_to_awk(entry.yexpr)
    begin_ = "OFMT=\"%.10g\""
    strip_ = ""
    if entry.delimiter !== nothing && !isempty(entry.delimiter)
        begin_ *= "; FS=$(_awk_string_literal(entry.delimiter))"
        strip_ = "sub(/^$(_awk_regex_escape(entry.delimiter))/, \"\"); "
    end
    awk_prog = (
        "BEGIN{$(begin_)} " *
        "/^>/{print; next} " *
        "{$(strip_)x=($(awk_x)); y=($(awk_y)); " *
        "if (x ~ /$(_AWK_NUMERIC_RE)/ && y ~ /$(_AWK_NUMERIC_RE)/) print x, y; " *
        "else print \"WARNING!!! NON-NUMERIC RESULT DETECTED! Skipping line \" NR > \"/dev/stderr\"}"
    )

    parts = ["awk $(shquote(awk_prog)) $(shquote(entry.file)) | gmt plot $j $r"]
    if entry.fill != "N"
        push!(parts, "-G$(entry.fill)")
    end
    if entry.symbol != "N"
        push!(parts, "-S$(entry.symbol)")
    end
    if entry.pen != "N"
        push!(parts, "-W$(entry.pen)")
    end
    return join(parts, " ")
end


"""Port of the -B assembly at mbm_xyplot lines ~1234-1254 (WESN forced
on, as for mbjl_grdplot -- see that module's _basemap_axes() docstring
for why modern mode requires it here where classic mode did not)."""
function _basemap_axes_xy(opts::ScriptOptionsXY)::String
    frame = "-BWESN" * ((opts.title !== nothing && !isempty(opts.title)) ? "+t\"$(opts.title)\"" : "")
    return "$frame $(opts.layout.axes_interval)"
end


_partition(s::AbstractString, sep::AbstractString) =
    (i = findfirst(sep, s); i === nothing ? (String(s), "") : (String(s[1:prevind(s, first(i))]), String(s[last(i)+1:end])))


"""Build the full GMT modern-mode shellscript text for `opts`. Touches
no files at all -- not even a temporary one; see this module's own
top comment."""
function generate_script_xy(opts::ScriptOptionsXY)::String
    lay = opts.layout
    j = "-J$(lay.projection)$(lay.projection_pars)"
    r = "-R$(lay.region)"

    lines = String[
        "#!/usr/bin/env bash",
        "#",
        "# GMT modern-mode xy plotting script",
        "# Generated by mbjl_xyplot_script.jl (Julia port of mbm_xyplot)",
        "#",
        "set -e",
        "",
        "gmt begin $(opts.root) $(opts.output_format)",
        "",
    ]

    if opts.dpi !== nothing && opts.dpi != 0
        push!(lines, "gmt figure $(opts.root) $(opts.output_format) E$(opts.dpi)")
        push!(lines, "")
    end

    push!(lines, "# Set temporary GMT defaults")
    baseline_pairs = String[]
    for gmt_def in baseline_gmt_defaults(lay.pagesize, false, lay.degree_format)
        param, value = _partition(gmt_def, " ")
        push!(baseline_pairs, "$param $value")
    end
    push!(lines, "gmt set $(join(baseline_pairs, " "))")
    push!(lines, "")

    if !isempty(opts.gmt_defs)
        push!(lines, "# User-defined GMT parameter overrides")
        user_pairs = String[]
        for gmt_def in opts.gmt_defs
            param, value = _partition(gmt_def, "/")
            push!(user_pairs, "$param $value")
        end
        push!(lines, "gmt set $(join(user_pairs, " "))")
        push!(lines, "")
    end

    # Order matches mbm_xyplot's own layer order exactly: xy data plots,
    # then coastline, then text labels, then the basemap frame last (mbm_
    # xyplot lines ~1256-1387) -- unlike mbjl_grdplot, which draws its
    # basemap frame *before* xy overlays/text (see mbjl_grdplot_script.jl's
    # generate_script()); each macro's own stacking order is preserved
    # rather than unified, since it affects which layer's ink ends up on
    # top in the final plot.
    if !isempty(opts.entries)
        push!(lines, "# Make xy data plot(s)")
        for entry in opts.entries
            push!(lines, _plot_pipeline(entry, j, r))
        end
        push!(lines, "")
    end

    if opts.coast !== nothing
        co = opts.coast
        nonempty(s) = s !== nothing && !isempty(s)
        push!(lines, "# Make coastline data plot")
        cparts = ["gmt coast $j $r"]
        if nonempty(co.coast_resolution)
            push!(cparts, "-D$(co.coast_resolution)")
        end
        if nonempty(co.coast_dryfill)
            push!(cparts, "-G$(co.coast_dryfill)")
        end
        if nonempty(co.coast_wetfill)
            push!(cparts, "-S$(co.coast_wetfill)")
        end
        if nonempty(co.coast_lakefill)
            push!(cparts, "-C$(co.coast_lakefill)")
        end
        if nonempty(co.coast_river)
            push!(cparts, "-I$(co.coast_river)")
        end
        for boundary in co.coast_boundaries
            push!(cparts, "-N$(boundary)")
        end
        if nonempty(co.coast_pen)
            push!(cparts, "-W$(co.coast_pen)")
        end
        push!(lines, join(cparts, " "))
        push!(lines, "")
    end

    if !isempty(opts.text_labels)
        push!(lines, "# Make text labels")
        push!(lines, "gmt text $j $r -F+f+a+j <<EOF")
        for raw in opts.text_labels
            tx, ty, tsize, tangle, font, just, txt = split(raw, "/"; limit = 7)
            push!(lines, "$tx $ty $tsize,$font $tangle $just $txt")
        end
        push!(lines, "EOF")
        push!(lines, "")
    end

    push!(lines, "# Make basemap")
    basemap_parts = ["gmt basemap $j $r $(_basemap_axes_xy(opts))"]
    if opts.length_scale !== nothing && !isempty(opts.length_scale)
        push!(basemap_parts, "-L$(opts.length_scale)")
    end
    push!(lines, join(basemap_parts, " "))
    push!(lines, "")

    push!(lines, "gmt end")
    push!(lines, "")
    return join(lines, "\n")
end


"""Write the generated shellscript to `script_path`. (As in the Python
original, the script is not made executable -- mbjl_xyplot runs it
through `bash` explicitly.)"""
function generate_and_write_xy(opts::ScriptOptionsXY, script_path::AbstractString)
    write(script_path, generate_script_xy(opts))
    return nothing
end

end # module MbjlXyplotScript
