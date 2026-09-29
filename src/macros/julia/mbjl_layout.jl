#--------------------------------------------------------------------
#    The MB-system:  mbjl_layout.jl
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
#   Julia port of src/macros/python/mbpy_layout.py, itself a port of the
#   page-size / projection / scale / tick-interval heuristics from
#   src/macros/mbm_grdplot (the Perl subs GetPageSize, GetProjection,
#   and GetBaseTick, plus the auto-layout algorithm in the main body
#   that decides page size, orientation, plot scale, plot origin, color
#   scale placement, contour interval, and degree annotation format).
#
#   This module only computes the *layout*: it does not generate a
#   GMT (classic or modern mode) script and does not build color
#   palettes. It is meant to be validated numerically against the
#   Perl macro's output before it is wired into a script- or
#   GMT.jl-based backend.
#
# Status:
#   Ported and cross-checked against `mbm_grdplot` (Perl) output for
#   several synthetic grids -- see test_mbjl_layout.jl.
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))

module MbjlLayout

using Printf
using ..MbjlUtil

export PAGE_SIZE_NAMES, PAGE_WIDTH_IN, PAGE_HEIGHT_IN, get_decimal_degrees,
       PageMargins, get_page_size, ProjectionSpec, get_projection, BaseTick,
       get_base_tick, GridInfo, run_grdinfo, Layout, compute_layout

# ---------------------------------------------------------------------------
# Page size database (verbatim from mbm_grdplot: @page_size_names,
# %page_width_in, %page_height_in). Dimensions are portrait width/height
# in inches.
# ---------------------------------------------------------------------------

const PAGE_SIZE_NAMES = [
    "a", "b", "c", "d", "e", "f", "e1",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "a10",
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "b10",
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7",
    "m1", "m2", "m3", "m4", "m5", "m6",
]

const PAGE_WIDTH_IN = Dict(
    "a" => 8.50, "b" => 11.00, "c" => 17.00, "d" => 22.00,
    "e" => 34.00, "f" => 28.00, "e1" => 44.00, "a0" => 33.11,
    "a1" => 23.39, "a2" => 16.54, "a3" => 11.69, "a4" => 8.27,
    "a5" => 5.83, "a6" => 4.13, "a7" => 2.91, "a8" => 2.05,
    "a9" => 1.46, "a10" => 1.02, "b0" => 39.37, "b1" => 27.83,
    "b2" => 19.68, "b3" => 13.90, "b4" => 9.84, "b5" => 6.93,
    "b6" => 4.92, "b7" => 3.46, "b8" => 2.44, "b9" => 1.73,
    "b10" => 1.22, "c0" => 36.00, "c1" => 25.60, "c2" => 18.00,
    "c3" => 12.80, "c4" => 9.00, "c5" => 6.40, "c6" => 4.50,
    "c7" => 3.20, "m1" => 54.00, "m2" => 54.00, "m3" => 54.00,
    "m4" => 60.00, "m5" => 60.00, "m6" => 60.00,
)

const PAGE_HEIGHT_IN = Dict(
    "a" => 11.00, "b" => 17.00, "c" => 22.00, "d" => 34.00,
    "e" => 44.00, "f" => 40.00, "e1" => 68.00, "a0" => 46.81,
    "a1" => 33.11, "a2" => 23.39, "a3" => 16.54, "a4" => 11.69,
    "a5" => 8.27, "a6" => 5.83, "a7" => 4.13, "a8" => 2.91,
    "a9" => 2.05, "a10" => 1.46, "b0" => 56.67, "b1" => 39.37,
    "b2" => 27.83, "b3" => 19.68, "b4" => 13.90, "b5" => 9.84,
    "b6" => 6.93, "b7" => 4.92, "b8" => 3.46, "b9" => 2.44,
    "b10" => 1.73, "c0" => 51.20, "c1" => 36.00, "c2" => 25.60,
    "c3" => 18.00, "c4" => 12.80, "c5" => 9.00, "c6" => 6.40,
    "c7" => 4.50, "m1" => 72.00, "m2" => 84.00, "m3" => 96.00,
    "m4" => 72.00, "m5" => 84.00, "m6" => 96.00,
)


"""Port of GetDecimalDegrees: accepts dd, dd:mm, or dd:mm:ss strings."""
function get_decimal_degrees(value::AbstractString)::Float64
    s = strip(value)
    m = match(r"^(\S+):(\S+):(\S+)$", s)
    if m !== nothing
        degrees, minutes, seconds = m.captures
        d = pyfloat(degrees)
        mm = pyfloat(minutes)
        ss = pyfloat(seconds)
        if startswith(degrees, "-")
            return d - mm / 60.0 - ss / 3600.0
        end
        return d + mm / 60.0 + ss / 3600.0
    end
    m = match(r"^(\S+):(\S+)$", s)
    if m !== nothing
        degrees, minutes = m.captures
        d = pyfloat(degrees)
        mm = pyfloat(minutes)
        if startswith(degrees, "-")
            return d - mm / 60.0
        end
        return d + mm / 60.0
    end
    return pyfloat(s)
end


# ---------------------------------------------------------------------------
# GetPageSize
# ---------------------------------------------------------------------------

Base.@kwdef mutable struct PageMargins
    space_top::Float64
    space_bottom::Float64
    space_left::Float64
    space_right::Float64
    width_max_landscape::Float64
    height_max_landscape::Float64
    width_max_portrait::Float64
    height_max_portrait::Float64
end


"""Port of sub GetPageSize.

scale_loc: 'l' (left), 'r' (right), 't' (top), or anything else
(bottom, the mbm_grdplot default).
"""
function get_page_size(pagesize::AbstractString, scale_loc::AbstractString = "b")::PageMargins
    page_h = PAGE_HEIGHT_IN[pagesize]
    page_w = PAGE_WIDTH_IN[pagesize]
    a_h = PAGE_HEIGHT_IN["a"]

    if scale_loc == "l"
        space_top = min(1.50 * page_h / a_h, 4.50)
        space_bottom = min(0.75 * page_h / a_h, 2.25)
        space_left = min(2.50 * page_h / a_h, 7.50)
        space_right = min(1.00 * page_h / a_h, 3.00)
    elseif scale_loc == "r"
        space_top = min(1.50 * page_h / a_h, 4.50)
        space_bottom = min(0.75 * page_h / a_h, 2.25)
        space_left = min(1.00 * page_h / a_h, 3.00)
        space_right = min(2.50 * page_h / a_h, 7.50)
    elseif scale_loc == "t"
        space_top = min(2.75 * page_h / a_h, 8.25)
        space_bottom = min(0.75 * page_h / a_h, 2.25)
        space_left = min(1.00 * page_h / a_h, 3.00)
        space_right = min(1.00 * page_h / a_h, 3.00)
    else
        space_top = min(1.50 * page_h / a_h, 4.50)
        space_bottom = min(2.00 * page_h / a_h, 6.00)
        space_left = min(1.00 * page_h / a_h, 3.00)
        space_right = min(1.00 * page_h / a_h, 3.00)
    end

    width_max_landscape = page_h - space_left - space_right
    height_max_landscape = page_w - space_bottom - space_top
    width_max_portrait = page_w - space_left - space_right
    height_max_portrait = page_h - space_bottom - space_top

    return PageMargins(
        space_top, space_bottom, space_left, space_right,
        width_max_landscape, height_max_landscape,
        width_max_portrait, height_max_portrait,
    )
end


# ---------------------------------------------------------------------------
# GetProjection: parse a GMT -J projection spec of the form used by
# mbm_grdplot's $map_scale (e.g. "m1.0", "M6i", "x0.01/0.01", "Q-122/6i").
# Returns (projection_letter, projection_pars, use_scale, use_width,
#          separator, trial_value, use_ratio, linear, plot_scale_or_width)
# ---------------------------------------------------------------------------

Base.@kwdef mutable struct ProjectionSpec
    projection::String
    projection_pars::String
    use_scale::Bool
    use_width::Bool
    use_ratio::Bool
    linear::Bool
    separator::String
    trial_value::String
    plot_scale::Union{Nothing,Float64} = nothing
    plot_width::Union{Nothing,Float64} = nothing
end


"""Port of sub GetProjection.

map_scale is the value of -J as mbm_grdplot expects it, e.g.
"m1.0", "M6i", "x0.02", "Xd", "q-122/1.0".
"""
function get_projection(map_scale::AbstractString)::ProjectionSpec
    map_scale = String(map_scale)
    m = match(r"^(\w)(.*)$", map_scale)
    if m === nothing
        throw(ArgumentError("unparseable map_scale: $(repr(map_scale))"))
    end
    projection, rest = String(m.captures[1]), m.captures[2]

    use_scale = false
    use_width = false
    use_ratio = false
    linear = false
    separator = "/"
    trial_value = "1.0"
    plot_scale = nothing
    plot_width = nothing

    function tail_number(pattern::Regex)
        mm = match(pattern, map_scale)
        return mm !== nothing ? pyfloat(mm.captures[1]) : nothing
    end

    if projection == "c"
        plot_scale = tail_number(r"^c\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "C"
        plot_width = tail_number(r"^C\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "m"
        plot_scale = tail_number(r"^m([\d.eE+-]+)$")
        use_scale = true
        separator = ""
    elseif projection == "M"
        plot_width = tail_number(r"^M([\d.eE+-]+)$")
        use_width = true
        separator = ""
    elseif projection == "o"
        if startswith(map_scale, "oa")
            plot_scale = tail_number(r"^oa\S+/\S+/\S+/([\d.eE+-]+)$")
        elseif startswith(map_scale, "ob")
            plot_scale = tail_number(r"^ob\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        elseif startswith(map_scale, "oc")
            plot_scale = tail_number(r"^oc\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        end
        use_scale = true
    elseif projection == "O"
        if startswith(map_scale, "Oa")
            plot_width = tail_number(r"^Oa\S+/\S+/\S+/([\d.eE+-]+)$")
        elseif startswith(map_scale, "Ob")
            plot_width = tail_number(r"^Ob\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        elseif startswith(map_scale, "Oc")
            plot_width = tail_number(r"^Oc\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        end
        use_width = true
    elseif projection == "q"
        plot_scale = tail_number(r"^q\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "Q"
        plot_width = tail_number(r"^Q\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "t"
        plot_scale = tail_number(r"^t\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "T"
        plot_width = tail_number(r"^T\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "u"
        plot_scale = tail_number(r"^u\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "U"
        plot_width = tail_number(r"^U\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "y"
        plot_scale = tail_number(r"^y\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "Y"
        plot_width = tail_number(r"^Y\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "a"
        plot_scale = tail_number(r"^a\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
        trial_value = "1:1"
        use_ratio = true
    elseif projection == "A"
        plot_width = tail_number(r"^A\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "e"
        plot_scale = tail_number(r"^e\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
        trial_value = "1:1"
        use_ratio = true
    elseif projection == "E"
        plot_width = tail_number(r"^E\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "g"
        plot_scale = tail_number(r"^g\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
        trial_value = "1:1"
        use_ratio = true
    elseif projection == "G"
        plot_width = tail_number(r"^G\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "s"
        plot_scale = tail_number(r"^s\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
        trial_value = "1:1"
        use_ratio = true
    elseif projection == "S"
        plot_width = tail_number(r"^S\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "b"
        plot_scale = tail_number(r"^b\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "B"
        plot_width = tail_number(r"^B\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "l"
        plot_scale = tail_number(r"^l\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "L"
        plot_width = tail_number(r"^L\S+/\S+/\S+/\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "h"
        plot_scale = tail_number(r"^h\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "H"
        plot_width = tail_number(r"^H\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "i"
        plot_scale = tail_number(r"^i\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "I"
        plot_width = tail_number(r"^I\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "k"
        plot_scale = tail_number(r"^k\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "K"
        plot_width = tail_number(r"^K\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "n"
        plot_scale = tail_number(r"^n\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "N"
        plot_width = tail_number(r"^N\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "r"
        plot_scale = tail_number(r"^r\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "R"
        plot_width = tail_number(r"^R\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "w"
        plot_scale = tail_number(r"^w\S+/([\d.eE+-]+)$")
        use_scale = true
    elseif projection == "W"
        plot_width = tail_number(r"^W\S+/([\d.eE+-]+)$")
        use_width = true
    elseif projection == "p"
        plot_scale = tail_number(r"^p([\d.eE+-]+)$")
        use_scale = true
        separator = ""
    elseif projection == "P"
        plot_width = tail_number(r"^P([\d.eE+-]+)$")
        use_width = true
        separator = ""
    elseif projection == "x"
        if map_scale == "xd"
            linear = true
            map_scale = map_scale[1:end-1]
        else
            linear = true
            plot_scale = tail_number(r"^x([\d.eE+-]+)$")
        end
        use_scale = true
        separator = ""
    elseif projection == "X"
        if map_scale == "Xd"
            linear = true
            map_scale = map_scale[1:end-1]
        else
            linear = true
            plot_width = tail_number(r"^X([\d.eE+-]+)$")
        end
        use_width = true
        separator = ""
    else
        throw(ArgumentError("unrecognized projection letter: $(repr(projection))"))
    end

    projection_pars = String(match(Regex("^\\Q" * projection * "\\E(\\S*)\$"), map_scale).captures[1])

    return ProjectionSpec(
        projection = projection,
        projection_pars = projection_pars,
        use_scale = use_scale,
        use_width = use_width,
        use_ratio = use_ratio,
        linear = linear,
        separator = separator,
        trial_value = trial_value,
        plot_scale = plot_scale,
        plot_width = plot_width,
    )
end


# ---------------------------------------------------------------------------
# GetBaseTick
# ---------------------------------------------------------------------------

Base.@kwdef mutable struct BaseTick
    tick_x::String
    tick_y::String
    gridline_y::Union{Nothing,String} = nothing
    xlabel::Union{Nothing,String} = nothing
    ylabel::Union{Nothing,String} = nothing
end


const _GEOGRAPHIC_TICK_TABLE = [
    (0.00006944444445, "0.25s"),
    (0.00013888888889, "0.5s"),
    (0.0002777777, "1s"),
    (0.0005555555, "2s"),
    (0.0013888889, "5s"),
    (0.0027777778, "10s"),
    (0.0041666667, "15s"),
    (0.0083333333, "30s"),
    (0.0166667, "1m"),
    (0.0333333, "2m"),
    (0.0833333, "5m"),
    (0.1666667, "10m"),
    (0.25, "15m"),
    (0.5, "30m"),
    (1.0, "1"),
    (2.0, "2"),
    (5.0, "5"),
    (10.0, "10"),
    (15.0, "15"),
    (30.0, "30"),
    (360.0, "60"),
]


"""Port of sub GetBaseTick.

gridprojected: 0 geographic, 1 UTM/projected-linear, 2 seismic
(time vs. trace), 3 generic linear.
"""
function get_base_tick(
    gridprojected::Integer,
    xmin::Real, xmax::Real, ymin::Real, ymax::Real,
    xunits::AbstractString = "", yunits::AbstractString = "",
)::BaseTick
    if gridprojected == 2
        base_tick_x = (xmax - xmin) > 200 ? 200.0 : (xmax - xmin)
        # exp10, not 10.0^x: Julia's ^ computes a negative integral power
        # as inv(10.0^n), which is off by an ulp (0.010000000000000002)
        # from the correctly-rounded pow() result Python/Perl produce
        base_tick_y = exp10(floor(log(abs(ymax - ymin) / 5) / log(10)))
    else
        base_tick_x = (xmax - xmin) / 3
        base_tick_y = (ymax - ymin) / 3
    end

    if gridprojected == 2
        base_gridline_y = base_tick_y / 2
        tick_y = "a$(pystr(base_tick_y))g$(pystr(base_gridline_y))"
        return BaseTick(
            tick_x = pystr(base_tick_x), tick_y = tick_y,
            gridline_y = pystr(base_gridline_y),
            xlabel = "\"Trace Number\"", ylabel = "\"Time (sec)\"",
        )
    end

    if gridprojected == 3
        return BaseTick(
            tick_x = pystr(base_tick_x), tick_y = pystr(base_tick_y),
            xlabel = "\"$xunits\"", ylabel = "\"$yunits\"",
        )
    end

    if gridprojected == 1
        base_tick = min(base_tick_x, base_tick_y)
        if base_tick >= 10.0
            base_tick = pytrunc(base_tick)
        elseif base_tick >= 1.0
            base_tick = 0.1 * pytrunc(10 * base_tick)
        elseif base_tick >= 0.1
            base_tick = 0.01 * pytrunc(100 * base_tick)
        elseif base_tick >= 0.01
            base_tick = 0.001 * pytrunc(1000 * base_tick)
        elseif base_tick >= 0.001
            base_tick = 0.0001 * pytrunc(10000 * base_tick)
        end
        return BaseTick(tick_x = pystr(base_tick), tick_y = pystr(base_tick))
    end

    # gridprojected == 0: geographic
    base_tick = min(base_tick_x, base_tick_y)
    tick_str = "60"
    for (threshold, label) in _GEOGRAPHIC_TICK_TABLE
        if base_tick < threshold
            tick_str = label
            break
        end
    end
    return BaseTick(tick_x = tick_str, tick_y = tick_str)
end


# ---------------------------------------------------------------------------
# Grid info (subset of mbm_grdplot's `gmt grdinfo` text-output parsing)
# ---------------------------------------------------------------------------

Base.@kwdef mutable struct GridInfo
    xmin::Float64
    xmax::Float64
    ymin::Float64
    ymax::Float64
    zmin::Float64
    zmax::Float64
    xunits::String = ""
    yunits::String = ""
    gridprojected::Int = 0
    utm_zone::Union{Nothing,String} = nothing
end
GridInfo(xmin, xmax, ymin, ymax, zmin, zmax) =
    GridInfo(xmin = xmin, xmax = xmax, ymin = ymin, ymax = ymax, zmin = zmin, zmax = zmax)


"""Run a command, capture stdout as text, and throw (like Python's
check=True) on a non-zero exit status."""
function _run_capture(cmd::Cmd)
    out = IOBuffer()
    err = IOBuffer()
    proc = run(pipeline(ignorestatus(cmd); stdout = out, stderr = err))
    if !success(proc)
        error("command $(cmd) failed with exit status $(proc.exitcode):\n" * String(take!(err)))
    end
    return String(take!(out)), String(take!(err))
end


function run_grdinfo(grid_path::AbstractString)::GridInfo
    # Invalid UTF-8 is tolerated: a grid's embedded NetCDF metadata (e.g.
    # a "Remark" line carrying whatever locale/encoding wrote it) can
    # contain bytes that are not valid UTF-8 -- confirmed on a real grid
    # in the wild, where grdinfo's own "Remark:" line embeds a non-UTF-8
    # byte from its creation-time hostname/timestamp string. None of the
    # numeric fields this function actually parses live on that line.
    # (The Python original decodes with errors="replace"; Julia Strings
    # hold arbitrary bytes and PCRE matching below runs on each line with
    # invalid sequences replaced first, which has the same effect.)
    out, _ = _run_capture(`gmt grdinfo $grid_path`)

    xmin = xmax = ymin = ymax = zmin = zmax = nothing
    xunits = yunits = ""
    gridprojected = 0
    utm_zone = nothing

    for rawline in split(out, r"\r\n|\n|\r")
        line = isvalid(rawline) ? rawline : String(map(c -> isvalid(c) ? c : '�', collect(rawline)))
        m = match(r"Projection: UTM Zone (\S+)", line)
        if m !== nothing
            utm_zone = String(m.captures[1])
            gridprojected = 1
        end
        if occursin("Projection: SeismicProfile", line)
            gridprojected = 2
        end
        if occursin("Projection: GenericLinear", line)
            gridprojected = 3
        end
        if occursin("Projection: Geographic", line)
            gridprojected = 0
        end

        m = match(
            r"x_min:\s+(\S+)\s+x_max:\s+(\S+)\s+x_inc:\s+\S+\s+(?:units|name):\s+(.+?)\s+n(?:x|_columns):\s+\S+",
            line,
        )
        if m !== nothing
            xmin, xmax, xunits = pyfloat(m.captures[1]), pyfloat(m.captures[2]), String(m.captures[3])
        else
            m = match(r"x_min:\s+(\S+)\s+x_max:\s+(\S+)\s+x_inc:", line)
            if m !== nothing
                xmin, xmax = pyfloat(m.captures[1]), pyfloat(m.captures[2])
            end
        end

        m = match(
            r"y_min:\s+(\S+)\s+y_max:\s+(\S+)\s+y_inc:\s+\S+\s+(?:units|name):\s+(.+?)\s+n(?:y|_rows):\s+\S+",
            line,
        )
        if m !== nothing
            ymin, ymax, yunits = pyfloat(m.captures[1]), pyfloat(m.captures[2]), String(m.captures[3])
        else
            m = match(r"y_min:\s+(\S+)\s+y_max:\s+(\S+)\s+y_inc:", line)
            if m !== nothing
                ymin, ymax = pyfloat(m.captures[1]), pyfloat(m.captures[2])
            end
        end

        m = match(r"[zv]_?min:\s+(\S+)\s+[zv]_?max:\s+(\S+)\s+(?:units|name):", line)
        if m !== nothing
            zmin, zmax = pyfloat(m.captures[1]), pyfloat(m.captures[2])
        end
    end

    if xmin === nothing || ymin === nothing
        error("could not parse grdinfo output for $grid_path")
    end
    if zmin === nothing
        zmin, zmax = 0.0, 1.0
    end
    if zmin >= zmax
        zmax = zmin + 1.0
    end

    return GridInfo(xmin, xmax, ymin, ymax, zmin, zmax, xunits, yunits, gridprojected, utm_zone)
end


"""Mirrors `(\$projection_pars) = \$map_scale =~ /^\$projection(\\S+)/;`
as used when mbm_grdplot recomputes projection_pars just before the
final sprintf. Returns "" when map_scale is falsy, exactly as the
Perl regex match against an undefined/empty string does."""
function _base_pars(projection::AbstractString, map_scale)::String
    if map_scale === nothing || isempty(map_scale)
        return ""
    end
    m = match(Regex("^\\Q" * projection * "\\E(\\S+)\$"), map_scale)
    return m !== nothing ? String(m.captures[1]) : ""
end


"""Mimic Perl's numeric coercion of a string: parse the longest
leading valid-number prefix, defaulting to 0.0 if none."""
function _perl_numify(s::AbstractString)::Float64
    m = match(r"^\s*([+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)", s)
    return m !== nothing ? pyfloat(m.captures[1]) : 0.0
end


"""Port of `sprintf("\$base\$separator%1.5g", \$value)`."""
function _sprintf_g5(base::AbstractString, separator::AbstractString, value)::String
    v = value isa AbstractString ? _perl_numify(value) : value
    return "$base$separator$(fmtg(v, 5))"
end


"""Run `gmt mapproject` on the four corner points of the region and
return (dxx, dyy) -- the plotted width/height in inches at the trial
scale/width used in projection_pars. Mirrors the corner-point logic
in mbm_grdplot's main body (around the `elsif` opposite the linear
fast path)."""
function _mapproject_bbox(xmin, xmax, ymin, ymax, projection, projection_pars, region)
    points = [(xmin, ymin), (xmax, ymin), (xmax, ymax), (xmin, ymax)]
    tmp_path, io = mktemp()
    tmp_dat = tmp_path * ".dat"
    try
        for (x, y) in points
            write(io, "$(pystr(x)) $(pystr(y))\n")
        end
        close(io)
        mv(tmp_path, tmp_dat; force = true)
        stdout_, stderr_ = _run_capture(`gmt mapproject $tmp_dat -J$(projection)$(projection_pars) -R$(region)`)
        xs, ys = Float64[], Float64[]
        for line in split(stdout_, r"\r\n|\n|\r")
            parts = split(line)
            if length(parts) >= 2
                push!(xs, pyfloat(parts[1]))
                push!(ys, pyfloat(parts[2]))
            end
        end
        if isempty(xs)
            error("gmt mapproject produced no output (stderr: $stderr_)")
        end
        return maximum(xs) - minimum(xs), maximum(ys) - minimum(ys)
    finally
        isopen(io) && close(io)
        rm(tmp_path; force = true)
        rm(tmp_dat; force = true)
    end
end


# ---------------------------------------------------------------------------
# Full layout computation (port of the auto page/scale/orientation-fit
# algorithm in mbm_grdplot's main body, roughly lines 1252-1675).
# ---------------------------------------------------------------------------

Base.@kwdef mutable struct Layout
    pagesize::String
    scale_loc::String
    projection::String
    projection_pars::String
    region::String
    landscape::Bool
    width::Float64          # chosen page width, inches (already oriented)
    height::Float64          # chosen page height, inches
    plot_width::Float64
    plot_height::Float64
    xoffset::Float64
    yoffset::Float64
    degree_format::String
    colorscale_length::Float64
    colorscale_thick::Float64
    colorscale_offx::Float64
    colorscale_offy::Float64
    colorscale_vh::String
    contour_int::Float64
    base_tick::BaseTick
    gridprojected::Int = 0   # resolved value (see compute_layout()'s docstring on
                             # the out-of-bounds fallback) -- callers building a
                             # GridInfo-consuming ScriptOptions from the same grid
                             # should use this instead of the raw GridInfo.gridprojected
end


_truthy(x) = x !== nothing && x != 0

function compute_layout(
    grid::GridInfo;
    pagesize::AbstractString = "a",
    scale_loc::AbstractString = "b",
    map_scale::Union{Nothing,AbstractString} = nothing,     # user-supplied -J value, if any
    orientation::Integer = 0,                # 0 auto, 1 portrait, 2 landscape
    region::Union{Nothing,AbstractString} = nothing,        # user -R override; else grid bounds
)::Layout
    pagesize = String(pagesize)
    xmin, xmax, ymin, ymax = grid.xmin, grid.xmax, grid.ymin, grid.ymax
    dzz = grid.zmax - grid.zmin
    map_scale = (map_scale === nothing || isempty(map_scale)) ? nothing : String(map_scale)
    region = (region === nothing || isempty(region)) ? nothing : String(region)

    # Port of mbm_grdplot lines ~1167-1191: a user-supplied -R overrides
    # not just the final plot region string but xmin/xmax/ymin/ymax
    # themselves, which drive every subsequent computation below (the
    # apparent plot size via mapproject, the projected-vs-geographic
    # fallback check, contour interval, tick spacing, ...). Confirmed as
    # a real gap in this port: without this, an explicit --region was
    # honored for the final -R string but silently ignored for the
    # actual page-fit sizing, which kept using the grid's own full
    # extent instead -- producing a wildly wrong plot scale/aspect
    # ratio whenever --region requested a sub-area of the grid.
    if region !== nothing
        xmin, xmax, ymin, ymax = (pyfloat(v) for v in split(region, "/")[1:4])
    end

    margins = get_page_size(pagesize, scale_loc)

    use_scale = use_width = use_ratio = linear = false
    # NOTE: mbm_grdplot's $separator is only ever assigned inside
    # GetProjection; when map_scale is not supplied that sub never runs,
    # so $separator stays Perl-undef, which stringifies to "" everywhere
    # it is later interpolated. Mirror that here rather than defaulting
    # to "/".
    separator = ""
    trial_value = "1.0"
    plot_scale = plot_width = nothing
    projection = projection_pars = nothing

    if map_scale !== nothing
        spec = get_projection(map_scale)
        projection = spec.projection
        projection_pars = spec.projection_pars
        use_scale, use_width = spec.use_scale, spec.use_width
        use_ratio, linear = spec.use_ratio, spec.linear
        separator, trial_value = spec.separator, spec.trial_value
        plot_scale, plot_width = spec.plot_scale, spec.plot_width
        # mbm_grdplot's GetProjection strips the trailing "d" of a bare
        # "xd"/"Xd" from the *global* $map_scale ("chop $map_scale"), so
        # every later use of $map_scale -- the linear x/y fast path and
        # the projection_pars re-derivation below -- sees just "x"/"X".
        # The Python port (mbpy_layout.py) only strips a local copy
        # inside get_projection(), so it emitted "-JXd6.5d", which GMT
        # rejects; Perl emits "-JX6.5d". Mirror Perl here.
        if map_scale == "xd" || map_scale == "Xd"
            map_scale = map_scale[1:1]
        end
    end

    # Port of mbm_grdplot lines ~1200-1206: even when the grid's own
    # metadata doesn't tag it as a recognized UTM projection (see
    # run_grdinfo() above -- it only checks for the literal string "UTM
    # Zone"), treat it as projected (not geographic) if its plot bounds
    # (xmin/xmax/ymin/ymax, already reflecting a -R override above if
    # given) fall outside plausible lon/lat ranges. This catches grids
    # in other projected coordinate systems MB-System supports -- e.g.
    # its own "LTM" (local tangent Mercator) grids -- which would
    # otherwise default to a Mercator projection and fail outright (gmt
    # mapproject errors out, or worse silently mis-projects) when handed
    # plot bounds in meters instead of degrees.
    gridprojected = grid.gridprojected
    if gridprojected == 0 && (xmin < -360.0 || xmax > 360.0 || ymin < -90.0 || ymax > 90.0)
        gridprojected = 1
    end

    if (use_scale && _truthy(plot_scale)) || (use_width && _truthy(plot_width))
        # projection/projection_pars as parsed above
    elseif use_scale || use_width
        projection_pars = "$(projection_pars)$(separator)$(trial_value)"
    elseif gridprojected > 0
        projection, projection_pars = "x", "1.0"
        use_scale, linear = true, true
    else
        projection, projection_pars = "m", "1.0"
        use_scale = true
    end

    bounds_plot = region !== nothing ? region :
        "$(fmtg(xmin, 11))/$(fmtg(xmax, 11))/$(fmtg(ymin, 11))/$(fmtg(ymax, 11))"

    if projection == "x" && map_scale !== nothing && occursin(r"^\S+/\S+", map_scale[2:end])
        xscale, yscale = (pyfloat(v) for v in match(r"^x(\S+)/(\S+)", map_scale).captures)
        dxx = abs(xmax * xscale - xmin * xscale)
        dyy = abs(ymax * yscale - ymin * yscale)
    else
        dxx, dyy = _mapproject_bbox(xmin, xmax, ymin, ymax, projection, projection_pars, bounds_plot)
        dxx, dyy = abs(dxx), abs(dyy)
    end

    landscape = portrait = false
    width = height = 0.0
    width_max = height_max = 0.0

    if (use_scale && _truthy(plot_scale)) || (use_width && _truthy(plot_width))
        plot_width_, plot_height_ = dxx, dyy
        if orientation == 1
            portrait = true
            width, height = PAGE_WIDTH_IN[pagesize], PAGE_HEIGHT_IN[pagesize]
            width_max, height_max = margins.width_max_portrait, margins.height_max_portrait
        elseif orientation == 2
            landscape = true
            width, height = PAGE_HEIGHT_IN[pagesize], PAGE_WIDTH_IN[pagesize]
            width_max, height_max = margins.width_max_landscape, margins.height_max_landscape
        elseif dxx > dyy
            landscape = true
            width, height = PAGE_HEIGHT_IN[pagesize], PAGE_WIDTH_IN[pagesize]
            width_max, height_max = margins.width_max_landscape, margins.height_max_landscape
        else
            portrait = true
            width, height = PAGE_WIDTH_IN[pagesize], PAGE_HEIGHT_IN[pagesize]
            width_max, height_max = margins.width_max_portrait, margins.height_max_portrait
        end

        if plot_width_ > width_max || plot_height_ > height_max
            good_page = nothing
            for elem in PAGE_SIZE_NAMES
                m2 = get_page_size(elem, scale_loc)
                wm = portrait ? m2.width_max_portrait : m2.width_max_landscape
                hm = portrait ? m2.height_max_portrait : m2.height_max_landscape
                if plot_width_ <= wm && plot_height_ <= hm
                    good_page = elem
                    break
                end
            end
            if good_page === nothing
                good_page = pagesize  # mbm_grdplot warns and keeps trying anyway
            end
            pagesize = good_page
            margins = get_page_size(pagesize, scale_loc)
            width = portrait ? PAGE_WIDTH_IN[pagesize] : PAGE_HEIGHT_IN[pagesize]
            height = portrait ? PAGE_HEIGHT_IN[pagesize] : PAGE_WIDTH_IN[pagesize]
        end

        plot_width, plot_height = plot_width_, plot_height_

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
            width, height = PAGE_WIDTH_IN[pagesize], PAGE_HEIGHT_IN[pagesize]
        elseif orientation == 2
            landscape = true
            plot_scale = plot_scale_landscape
            width, height = PAGE_HEIGHT_IN[pagesize], PAGE_WIDTH_IN[pagesize]
        elseif plot_scale_landscape > plot_scale_portrait
            landscape = true
            plot_scale = plot_scale_landscape
            width, height = PAGE_HEIGHT_IN[pagesize], PAGE_WIDTH_IN[pagesize]
        else
            portrait = true
            plot_scale = plot_scale_portrait
            width, height = PAGE_WIDTH_IN[pagesize], PAGE_HEIGHT_IN[pagesize]
        end

        plot_width = dxx * plot_scale
        plot_height = dyy * plot_scale

        # mbm_grdplot re-derives projection_pars from the *original*
        # map_scale string here (not from the trial-value-appended form
        # used for the mapproject probe above); with no user map_scale
        # at all, this is "".
        base = _base_pars(projection, map_scale)
        if use_ratio
            # Ratio-based azimuthal projections (a/e/g/s) format their
            # scale as "1:NNNN". mbm_grdplot used to run this string
            # through sprintf("%1.5g", ...) here, which numifies "1:NNNN"
            # down to just "1" (Perl's numeric coercion stops at the
            # first non-numeric character) -- fixed upstream by
            # appending the ratio string directly instead of formatting
            # it as %g.
            top = pytrunc(1 / plot_scale)
            projection_pars = "$(base)$(separator)1:$(top)"
        else
            projection_pars = _sprintf_g5(base, separator, plot_scale)
        end
        if linear && gridprojected == 0
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
            width, height = PAGE_HEIGHT_IN[pagesize], PAGE_WIDTH_IN[pagesize]
        else
            width, height = PAGE_WIDTH_IN[pagesize], PAGE_HEIGHT_IN[pagesize]
        end

        base = _base_pars(projection, map_scale)
        projection_pars = _sprintf_g5(base, separator, plot_width)
        if linear && gridprojected == 0
            projection_pars = "$(projection_pars)d"
        end
    end

    xoffset = (width - abs(plot_width) - margins.space_left - margins.space_right) / 2 + margins.space_left
    yoffset = (height - abs(plot_height) - margins.space_bottom - margins.space_top) / 2 + margins.space_bottom

    # degree annotation format
    degree_format = "ddd:mm"
    if gridprojected == 0
        xsize = (xmax - xmin) / 3
        ysize = (ymax - ymin) / 3
        size = min(xsize, ysize)
        if size > 4.0
            degree_format = "ddd"
        elseif size > (1.0 / 60.0)
            degree_format = "ddd:mm"
        else
            degree_format = "ddd:mm:ss"
        end
    end

    # color scale placement
    sl = lowercase(scale_loc)
    page_h = PAGE_HEIGHT_IN[pagesize]
    if sl == "l"
        colorscale_length = plot_height
        colorscale_thick = 0.013636364 * page_h
        colorscale_offx = -0.13636 * page_h
        colorscale_offy = 0.5 * plot_height
        colorscale_vh = "+v"
    elseif sl == "r"
        colorscale_length = plot_height
        colorscale_thick = 0.013636364 * page_h
        colorscale_offx = plot_width + 0.0909 * page_h
        colorscale_offy = 0.5 * plot_height
        colorscale_vh = "+v"
    elseif sl == "t"
        colorscale_length = plot_width
        colorscale_thick = 0.013636364 * page_h
        colorscale_offx = 0.5 * plot_width
        colorscale_offy = plot_height + 0.15 * page_h
        colorscale_vh = "+h"
    else
        colorscale_length = plot_width
        colorscale_thick = 0.013636364 * page_h
        colorscale_offx = 0.5 * plot_width
        colorscale_offy = -0.045454545 * page_h
        colorscale_vh = "+h"
    end
    colorscale_length = max(colorscale_length, 3.0)

    # contour interval heuristic
    contour_int = 0.0
    if dzz > 0
        base = pytrunc((log(dzz) / log(10.0)) + 0.5)
        contour_int = exp10(base) / 10.0   # exp10: see get_base_tick()
        if dzz / contour_int < 10
            contour_int = contour_int / 4
        elseif dzz / contour_int < 20
            contour_int = contour_int / 2
        end
    end

    base_tick = get_base_tick(gridprojected, xmin, xmax, ymin, ymax, grid.xunits, grid.yunits)

    return Layout(
        pagesize = pagesize,
        scale_loc = String(scale_loc),
        projection = projection,
        projection_pars = projection_pars,
        region = bounds_plot,
        landscape = landscape,
        width = width,
        height = height,
        plot_width = plot_width,
        plot_height = plot_height,
        xoffset = xoffset,
        yoffset = yoffset,
        degree_format = degree_format,
        colorscale_length = colorscale_length,
        colorscale_thick = colorscale_thick,
        colorscale_offx = colorscale_offx,
        colorscale_offy = colorscale_offy,
        colorscale_vh = colorscale_vh,
        contour_int = contour_int,
        gridprojected = gridprojected,
        base_tick = base_tick,
    )
end

end # module MbjlLayout
