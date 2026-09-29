#--------------------------------------------------------------------
#    The MB-system:  mbjl_color.jl
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
#   Julia port of src/macros/python/mbpy_color.py, itself a port of the
#   color-palette / color-interval / CPT-generation logic from
#   src/macros/mbm_grdplot (the built-in color tables
#   @cptbr1..10/@cptbg1..10/@cptbb1..10, the "nice interval" color_int/
#   color_start/color_end calculation, the palette-interpolation loop,
#   and the CPT-line-writing loop for continuous (color_style == 1)
#   palettes).
#
# Status (see NOT YET PORTED below):
#   Ported and cross-checked byte-for-byte against `mbm_grdplot` (Perl)
#   generated .cpt lines for: continuous style (color_style == 1) and
#   discrete style (color_style != 1), both flip and no-flip, default
#   and non-default palettes/ncolors (including the sealevel dual-
#   colormap palettes 8 and 9), the "nice interval" and direct-linear
#   (-Y) color stretch, the real grdhisteq-driven histogram-equalized
#   stretch (-S, i.e. $stretch_color), and the color_mode == 4 (slope
#   magnitude) linear-ramp boundary construction, and the
#   -W<color_control> option parser itself, including its existing-file
#   bypass (-W<file>: skip all color generation and use that file
#   directly as the CPT -- see resolve_color_control()). See
#   test_mbjl_color.jl, test_mbjl_histeq.jl,
#   test_mbjl_discrete.jl, test_mbjl_sealevel.jl, and
#   test_mbjl_colorcontrol.jl.
#
#   Four upstream bugs turned up while porting and cross-checking this
#   logic and have since been fixed both here and in
#   src/macros/mbm_grdplot itself:
#     1. the ratio-based azimuthal projections (a/e/g/s) had their
#        auto-fit "1:NNNN" scale string numified down to "1" by a
#        stray sprintf("%g", ...);
#     2. the continuous-style histogram stretch always dropped the
#        true (padded) data-maximum boundary, leaving the final CPT
#        segment short of the actual data range;
#     3. discrete-style CPTs (both the regular single-palette path and
#        the sealevel dual-colormap path) read one interpolated color
#        sample past the end of the array (an ncolors vs. ncolors_use
#        mixup), which rendered one extreme segment solid black.
#   See the docstrings/comments at get_projection()'s use_ratio
#   handling (mbjl_layout.jl), build_histogram_boundaries(),
#   and interpolate_palette()/interpolate_palette_sealevel()/
#   build_cpt_discrete() below for the fixed behavior.
#
#   One more oddity was noticed (not confirmed by a failing real-world
#   case, and NOT fixed, in either mbm_grdplot or here) while reading
#   the sealevel $izero calculation: its clamp-to-zero guard checks a
#   variable ($iszero, with an extra "s") that is never otherwise used
#   -- see compute_sealevel_izero()'s docstring.
#
#   Indexing note: the Python original is 0-based; every loop below
#   keeps the Python loop variable's 0-based value (so the arithmetic
#   and every comment read exactly as upstream) and adds 1 only at the
#   point of array access.
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))

module MbjlColor

using Printf
using ..MbjlUtil

export NCPT, PALETTE_NAMES, SEALEVEL_PALETTES, ColorControl, resolve_color_control,
       get_ncolors_use, ColorInterval, compute_color_interval, interpolate_palette,
       interpolate_palette_sealevel, compute_sealevel_izero, CptLine, as_written,
       format_echo, build_cpt_continuous, build_cpt_continuous_sealevel, run_grdhisteq,
       HistBoundaries, build_histogram_boundaries, build_slope_boundaries,
       build_cpt_continuous_from_boundaries, build_cpt_discrete,
       build_cpt_discrete_sealevel, build_cpt_discrete_from_boundaries, RGB

const RGB = NTuple{3,Float64}


# ---------------------------------------------------------------------------
# Built-in color palettes (verbatim from mbm_grdplot). Each is 11 RGB
# stops (ncpt = 11), sampled/interpolated down (or up) to `ncolors`
# colors. Only the first 8 are named in mbm_grdplot's own
# @color_palette_names; 9 and 10 are extra palettes it supports but
# never named (that gap is in the original, not introduced here).
# ---------------------------------------------------------------------------

const NCPT = 11

const PALETTE_NAMES = Dict(
    1 => "Haxby Colors",
    2 => "High Intensity Colors",
    3 => "Low Intensity Colors",
    4 => "Grayscale",
    5 => "Uniform Gray",
    6 => "Uniform Black",
    7 => "Uniform White",
    8 => "Sealevel",
)

const _PALETTES_RGB = Dict{Int,NTuple{3,NTuple{11,Int}}}(
    1 => ((255, 255, 255, 255, 240, 205, 138, 106,  50,  40,  37),
          (255, 186, 161, 189, 236, 255, 236, 235, 190, 127,  57),
          (255, 133,  68,  87, 121, 162, 174, 255, 255, 251, 175)),
    2 => ((255, 255, 255, 255, 128,   0,   0,   0,   0, 128, 255),
          (  0,  64, 128, 255, 255, 255, 255, 128,   0,   0,   0),
          (  0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255)),
    3 => ((200, 194, 179, 141,  90,   0,   0,   0,   0,  90, 141),
          (  0,  49,  90, 141, 179, 200, 141,  90,   0,   0,   0),
          (  0,   0,   0,   0,   0,   0, 141, 179, 200, 179, 141)),
    4 => ((255, 230, 204, 179, 153, 128, 102,  77,  51,  26,   0),
          (255, 230, 204, 179, 153, 128, 102,  77,  51,  26,   0),
          (255, 230, 204, 179, 153, 128, 102,  77,  51,  26,   0)),
    5 => ((128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128),
          (128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128),
          (128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128)),
    6 => ((  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),
          (  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),
          (  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0)),
    7 => ((255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255),
          (255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255),
          (255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255)),
    8 => ((250, 245, 240, 235, 230, 221, 212, 211, 210, 205, 200),
          (250, 240, 230, 221, 212, 201, 190, 180, 170, 160, 150),
          (120, 112, 104,  96,  88,  80,  72,  64,  56,  48,  40)),
    9 => ((255, 210, 170, 145, 120, 120, 104,  67,  33,   0,   0),
          (255, 200, 160, 145, 130, 100, 107, 123, 140, 160, 230),
          (100,  75,  50,  45,  40,  30,  24,   7,   0,   0,   0)),
    10 => ((  0,  51, 102, 153, 204, 255, 255, 255, 255, 255, 255),
           (  0,  51, 102, 153, 204, 255, 204, 153, 102,  51,   0),
           (255, 255, 255, 255, 255, 255, 204, 153, 102,  51,   0)),
)

# palettes 8 and 9 are the "sealevel" dual-colormap ones in mbm_grdplot
# and are NOT handled by interpolate_palette() below (see NOT YET
# PORTED); listed here only so palette-number validation can reference
# a single source of truth.
const SEALEVEL_PALETTES = (8, 9)


# ---------------------------------------------------------------------------
# -W<color_control> parsing (mbm_grdplot lines ~820-866): this is the very
# first thing mbm_grdplot does with -W, before any of the ncolors_use /
# color-interval / palette-interpolation logic above even runs.
# ---------------------------------------------------------------------------

"""Result of parsing -W<color_control>.

`file_cpt` set (non-nothing) means: the user pointed -W at an existing
CPT file on disk. In that case mbm_grdplot uses the file directly
as the CPT -- `color_style`/`color_palette`/`ncolors` are set to
harmless defaults here (matching mbm_grdplot's own dead-code
assignments in this case) but are never actually consulted, and
none of interpolate_palette() / build_cpt_continuous() / etc. above
are called. A caller that gets `file_cpt` back should skip straight
to using that path as the CPT and skip color generation entirely.
"""
Base.@kwdef mutable struct ColorControl
    file_cpt::Union{Nothing,String} = nothing
    color_style::Int = 1
    color_palette::Int = 1
    ncolors::Int = NCPT
end


"""Port of the -W<color_control> parsing (mbm_grdplot lines ~820-866).

Four forms of `color_control`, checked in this order:
  - falsy (no -W given): color_style=1, color_palette=1, ncolors=ncpt;
  - an existing file path: use it directly as the CPT (see
    ColorControl.file_cpt);
  - "style/palette/ncolors": all three explicit, with palette
    clamped to [1, 10] and ncolors clamped to >= 2 if out of range;
  - "style/palette": ncolors defaults to `ncpt`, palette clamped
    as above;
  - a bare style ("style"): palette defaults to 1, ncolors to `ncpt`.
"""
function resolve_color_control(color_control::Union{Nothing,AbstractString}, ncpt::Integer = NCPT)::ColorControl
    if color_control === nothing || isempty(color_control)
        return ColorControl(color_style = 1, color_palette = 1, ncolors = ncpt)
    end

    if ispath(color_control)
        return ColorControl(file_cpt = String(color_control), color_style = 1, color_palette = 1, ncolors = ncpt)
    end

    parts = split(color_control, "/")
    if length(parts) >= 3
        color_style, color_palette, ncolors = pyint(parts[1]), pyint(parts[2]), pyint(parts[3])
        if color_palette < 1 || color_palette > 10
            color_palette = 1
        end
        if ncolors < 2
            ncolors = 2
        end
        return ColorControl(color_style = color_style, color_palette = color_palette, ncolors = ncolors)
    end
    if length(parts) == 2
        color_style, color_palette = pyint(parts[1]), pyint(parts[2])
        if color_palette < 1 || color_palette > 10
            color_palette = 1
        end
        return ColorControl(color_style = color_style, color_palette = color_palette, ncolors = ncpt)
    end

    return ColorControl(color_style = pyint(color_control), color_palette = 1, ncolors = ncpt)
end


"""Port of the \$ncolors_use assignment (mbm_grdplot lines ~1676-1683)."""
get_ncolors_use(ncolors::Integer, color_style::Integer)::Int = color_style == 1 ? ncolors : ncolors + 1


Base.@kwdef mutable struct ColorInterval
    color_int::Float64
    color_start::Float64
    color_end::Float64
end


"""Port of the color_int/color_start/color_end block, mbm_grdplot
lines ~1684-1717.

contour_int is the value computed by MbjlLayout.compute_layout
(same heuristic mbm_grdplot itself derives it with, before this
block runs).
"""
function compute_color_interval(
    dzz::Real,
    zmin::Real,
    zmax::Real,
    ncolors_use::Integer,
    contour_int::Real,
    no_nice_color_int::Bool = false,
)::ColorInterval
    if !no_nice_color_int && dzz > 0
        start_int = contour_int / 2
        multiplier = pytrunc(dzz / (ncolors_use - 1) / start_int) + 1
        color_int = multiplier * start_int
        color_start = zmin < 0.0 ?
            color_int * (pytrunc(zmin / color_int) - 1) :
            color_int * pytrunc(zmin / color_int)
        color_end = color_start + color_int * (ncolors_use - 1)
        if color_end < zmax
            multiplier += 1
            color_int = multiplier * start_int
        end
        color_start = zmin < 0.0 ?
            color_int * (pytrunc(zmin / color_int) - 1) :
            color_int * pytrunc(zmin / color_int)
        color_end = color_start + color_int * (ncolors_use - 1)
    else
        color_int = (zmax - zmin) / (ncolors_use - 1)
        color_start = zmin
        color_end = color_start + color_int * (ncolors_use - 1)
    end

    return ColorInterval(color_int, color_start, color_end)
end


"""Port of the single-colormap interpolation loop (mbm_grdplot
lines ~1724-1769, as amended -- see NOTE below): resample the
11-stop base palette to `nsamples` colors by linear interpolation
on the stop index.

NOTE on `nsamples` vs. `ncolors`: pass `ncolors_use`
(get_ncolors_use()) here, not the raw requested `ncolors`. For
continuous-style CPTs (color_style == 1) these are equal, so
nothing changes there. For discrete-style CPTs (color_style != 1)
ncolors_use == ncolors + 1: mbm_grdplot used to sample only
`ncolors` colors there too, which was one short of what
build_cpt_discrete()'s extra segment needs and made it read past
the end of the resulting array -- silently rendering one segment
solid black in Perl (undef read as 0). Fixed upstream (and here)
by sampling `ncolors_use` colors always.

Does not handle the sealevel dual-colormap palettes (8, 9) -- see
NOT YET PORTED at the top of this module.
"""
function interpolate_palette(color_palette::Integer, nsamples::Integer)::Vector{RGB}
    if color_palette in SEALEVEL_PALETTES
        error("palette $color_palette is a sealevel dual-colormap palette; " *
              "use interpolate_palette_sealevel() instead")
    end
    if !haskey(_PALETTES_RGB, color_palette)
        throw(ArgumentError("unknown color palette number: $color_palette"))
    end
    if nsamples < 2
        throw(ArgumentError("nsamples must be >= 2"))
    end

    cptbr, cptbg, cptbb = _PALETTES_RGB[color_palette]
    return [_interp_stop(cptbr, cptbg, cptbb, (NCPT - 1) * i / (nsamples - 1))
            for i in 0:nsamples-1]
end


"""Linearly interpolate one color at fractional stop index `xx`
into an 11-stop base palette. `xx == NCPT - 1` exactly (the last
sample) reads one past the last stop with a zero weight -- safe here
(the fallback `i2 = i1` avoids a BoundsError) the same way it is
harmlessly safe in Perl (an out-of-range array read there returns
undef, numeric 0, multiplied by a zero fraction). `i1`/`i2` are
0-based stop indices, as in the Python original."""
function _interp_stop(cptbr, cptbg, cptbb, xx::Real)::RGB
    i1 = pytrunc(xx)
    i2 = i1 + 1 < NCPT ? i1 + 1 : i1
    frac = i2 != i1 ? (xx - i1) / (i2 - i1) : 0.0
    red = cptbr[i1+1] + (cptbr[i2+1] - cptbr[i1+1]) * frac
    green = cptbg[i1+1] + (cptbg[i2+1] - cptbg[i1+1]) * frac
    blue = cptbb[i1+1] + (cptbb[i2+1] - cptbb[i1+1]) * frac
    return (Float64(red), Float64(green), Float64(blue))
end


"""Port of the sealevel dual-colormap interpolation loops
(mbm_grdplot lines ~1780-1859): `color_palette` (a "land" palette,
e.g. 8 "Sealevel") fills samples 0..izero-2, and palette 1 (Haxby,
always -- hardcoded upstream) fills samples izero-1..ncolors-2, on
either side of the sea-level (z == 0) crossing at sample index
`izero - 1`.

Unlike interpolate_palette(), this returns the (cptub, cptue)
arrays directly rather than one `colors` list -- at the crossing
sample the two loops write genuinely different colors into cptub
vs. cptue (the land palette's darkest stop on one side, Haxby's
stop 1 on the other), so cptue is not simply cptub shifted by one
the way it is for a single continuous palette.

`izero` is computed by the caller exactly as mbm_grdplot does:
`ncolors - int(color_end / color_int)` when color_flip, else
`ncolors - int(-color_start / color_int)` -- see
compute_sealevel_izero().
"""
function interpolate_palette_sealevel(color_palette::Integer, ncolors::Integer, izero::Integer)
    if !(color_palette in SEALEVEL_PALETTES)
        throw(ArgumentError("$color_palette is not a sealevel palette (must be 8 or 9)"))
    end
    land_r, land_g, land_b = _PALETTES_RGB[color_palette]
    haxby_r, haxby_g, haxby_b = _PALETTES_RGB[1]

    cptub = RGB[]
    cptue = RGB[]

    # land colors above sea level: sample indices 0 .. izero - 1
    for i in 0:izero-1
        color = _interp_stop(land_r, land_g, land_b, (NCPT - 1) * i / (izero - 1))
        if i < izero - 1
            push!(cptub, color)
        end
        if i > 0
            push!(cptue, color)
        end
    end

    # Haxby colors below sea level: sample indices izero - 1 .. ncolors - 1
    for i in izero-1:ncolors-1
        xx = (NCPT - 2) * (i - izero + 1) / (ncolors - izero) + 1
        color = _interp_stop(haxby_r, haxby_g, haxby_b, xx)
        if i < ncolors - 1
            push!(cptub, color)
        end
        if i > izero - 1
            push!(cptue, color)
        end
    end

    return cptub, cptue
end


"""Port of the \$izero calculation (mbm_grdplot lines ~1790-1801):
the sample index of the sea-level (z == 0) crossing.

NOTE: mbm_grdplot's own clamp-to-zero guard right after this
("if (\$iszero < 0) { \$iszero = 0; }") checks a *different*,
always-undefined variable (\$iszero, with an extra "s") than the
one actually used everywhere else (\$izero) -- so a negative izero
(possible whenever the data's sea-level crossing falls outside the
ncolors_use color stops, e.g. very little data on one side of
zero) is never actually clamped despite the code appearing to
guard for it. This looks like a genuine (separate, newly-noticed)
upstream typo, not yet confirmed by a failing real-world case or
fixed here -- flagged rather than silently either replicated as a
no-op guard or "fixed" without checking real impact first.
"""
function compute_sealevel_izero(
    ncolors::Integer, color_int::Real, color_start::Real, color_end::Real, color_flip::Bool,
)::Int
    if color_flip
        return ncolors - pytrunc(color_end / color_int)
    end
    return ncolors - pytrunc(-color_start / color_int)
end


Base.@kwdef mutable struct CptLine
    d1::Float64
    r1::Float64
    g1::Float64
    b1::Float64
    d2::Float64
    r2::Float64
    g2::Float64
    b2::Float64
end

CptLine(d1::Real, c1::RGB, d2::Real, c2::RGB) = CptLine(d1, c1..., d2, c2...)


"""The values as they actually land in the .cpt file: RGB
components truncated toward zero, exactly like Perl's %d."""
as_written(l::CptLine) = (l.d1, pytrunc(l.r1), pytrunc(l.g1), pytrunc(l.b1),
                          l.d2, pytrunc(l.r2), pytrunc(l.g2), pytrunc(l.b2))


"""Render exactly as mbm_grdplot's `printf "%6g %3d %3d %3d %6g %3d %3d %3d"`.

Perl's %d conversion truncates toward zero rather than rounding
(interpolated RGB components are always >= 0 here, so plain
truncation matches).
"""
format_echo(l::CptLine) =
    "$(fmtg(l.d1, 6)) $(@sprintf("%3d", pytrunc(l.r1))) $(@sprintf("%3d", pytrunc(l.g1))) $(@sprintf("%3d", pytrunc(l.b1))) " *
    "$(fmtg(l.d2, 6)) $(@sprintf("%3d", pytrunc(l.r2))) $(@sprintf("%3d", pytrunc(l.g2))) $(@sprintf("%3d", pytrunc(l.b2)))"


"""Port of the color_style == 1 CPT-writing loops (mbm_grdplot
lines ~2228-2317) for the arithmetic (non-histogram) case: z
boundaries stepped by a fixed `color_int` from `color_start`. For
the grdhisteq-driven / slope-magnitude case (an explicit list of
z boundaries instead of a fixed step), see
build_cpt_continuous_from_boundaries() below.

`colors` is the ncolors-length list from interpolate_palette();
index 1 is the start of the base palette array (e.g. white for
Haxby), index end its end (e.g. dark blue for Haxby).

zmode/zmin_t/zmax_t reproduce mbm_grdplot's optional clamping of
the first/last segment's outer z bound to the data's actual
min/max (its -Z...{/2|/3} "zmode 1" behavior); pass zmode=0 (the
default) to skip that.
"""
function build_cpt_continuous(
    colors::AbstractVector,
    color_start::Real,
    color_int::Real;
    color_flip::Bool = false,
    zmode::Integer = 0,
    zmin_t::Union{Nothing,Real} = nothing,
    zmax_t::Union{Nothing,Real} = nothing,
)::Vector{CptLine}
    cptub, cptue = colors[1:end-1], colors[2:end]
    return _write_cpt_continuous_arithmetic(
        cptub, cptue, color_start, color_int, color_flip, zmode, zmin_t, zmax_t
    )
end


"""Shared core of build_cpt_continuous(): both cptub and cptue must
already be ncolors - 1 entries long. The single-palette case builds
them as a shifted view of one `colors` list (colors[1:end-1]/colors[2:end]);
the sealevel dual-colormap case builds them directly since they are
not simply shifted views of each other there -- see
interpolate_palette_sealevel().
"""
function _write_cpt_continuous_arithmetic(
    cptub, cptue, color_start, color_int, color_flip, zmode, zmin_t, zmax_t,
)::Vector{CptLine}
    ncolors = length(cptub) + 1
    lines = CptLine[]
    d1 = Float64(color_start)

    if color_flip
        # natural palette order: z increases together with color index
        for i in 0:ncolors-2
            d2 = d1 + color_int
            lo_color, hi_color = cptub[i+1], cptue[i+1]
            if zmode == 1
                if i == 0 && zmin_t !== nothing && zmin_t < d1
                    d1 = zmin_t
                end
                if i == ncolors - 2 && zmax_t !== nothing && zmax_t > d2
                    d2 = zmax_t
                end
            end
            push!(lines, CptLine(d1, lo_color, d2, hi_color))
            d1 = d2
        end
    else
        # default: reversed relative to the palette's stored order
        for i in ncolors-2:-1:0
            d2 = d1 + color_int
            lo_color, hi_color = cptue[i+1], cptub[i+1]
            if zmode == 1
                if i == ncolors - 2 && zmin_t !== nothing && zmin_t < d1
                    d1 = zmin_t
                end
                if i == 0 && zmax_t !== nothing && zmax_t > d2
                    d2 = zmax_t
                end
            end
            push!(lines, CptLine(d1, lo_color, d2, hi_color))
            d1 = d2
        end
    end

    return lines
end


"""Public entry point for the continuous-style sealevel dual-colormap
case: pass the (cptub, cptue) arrays from interpolate_palette_sealevel()
directly (they are not a single shifted `colors` list the way the
single-palette case's are, so build_cpt_continuous() itself doesn't
apply here)."""
function build_cpt_continuous_sealevel(
    cptub::AbstractVector,
    cptue::AbstractVector,
    color_start::Real,
    color_int::Real;
    color_flip::Bool = false,
    zmode::Integer = 0,
    zmin_t::Union{Nothing,Real} = nothing,
    zmax_t::Union{Nothing,Real} = nothing,
)::Vector{CptLine}
    return _write_cpt_continuous_arithmetic(
        cptub, cptue, color_start, color_int, color_flip, zmode, zmin_t, zmax_t
    )
end


# ---------------------------------------------------------------------------
# Histogram-equalized color stretch (-S / $stretch_color) and the
# color_mode == 4 (slope magnitude) linear-ramp boundaries. Both feed
# an explicit list of z boundaries into the same continuous CPT-line
# layout used by build_cpt_continuous() above, instead of stepping by
# a fixed color_int.
# ---------------------------------------------------------------------------

"""Run `gmt grdhisteq <grid> -C<ncolors> -D` and return its
(low, high) rows in order. Port of the shell-out at mbm_grdplot
line ~2169: `` `gmt grdhisteq \$files_data[0] -C\$ncolors -D` ``.
"""
function run_grdhisteq(grid_path::AbstractString, ncolors::Integer)::Vector{Tuple{Float64,Float64}}
    out = IOBuffer()
    err = IOBuffer()
    proc = run(pipeline(ignorestatus(`gmt grdhisteq $grid_path -C$ncolors -D`); stdout = out, stderr = err))
    success(proc) || error("gmt grdhisteq failed with exit status $(proc.exitcode):\n" * String(take!(err)))
    rows = Tuple{Float64,Float64}[]
    for line in split(String(take!(out)), r"\r\n|\n|\r")
        parts = split(line)
        if length(parts) >= 2
            push!(rows, (pyfloat(parts[1]), pyfloat(parts[2])))
        end
    end
    return rows
end


Base.@kwdef mutable struct HistBoundaries
    hist::Vector{Float64}
    ncolors::Int  # possibly reduced from the requested value
end


"""Port of the @hist construction from `gmt grdhisteq -D` output
(mbm_grdplot lines ~2169-2233, as amended -- see NOTE below).

Reproduces one upstream quirk rather than fixing it: degenerate
(zero-width, non-increasing) bin edges from grdhisteq are silently
skipped, which can shrink `ncolors` below what was requested.

NOTE on the continuous-style (color_style == 1) case: this
function can produce `ncolors + 1` boundaries, but the CPT-line
builder for continuous style only ever consumes `ncolors` of them
(one initial + ncolors - 1 in its loop). mbm_grdplot used to just
drop the extra one -- always the last, i.e. the true padded data
maximum -- leaving the final CPT segment short of the actual data
range. Fixed upstream (and here, when color_style == 1) by folding
that last boundary into the last slot that IS consumed, which
merges what would have been the last two histogram-equalized bins
into one final segment instead of just discarding one end.
Discrete-style CPTs (color_style != 1) consume the full
`ncolors + 1` boundaries already and are unaffected either way.

`hist` indices in the comments are 0-based, as in the Python
original (hist[ncolors] there is hist[ncolors + 1] here).
"""
function build_histogram_boundaries(
    rows::AbstractVector,
    ncolors::Integer,
    zmin::Real,
    zmax::Real,
    dzz::Real;
    data_scale::Union{Nothing,Real} = nothing,
    color_style::Integer = 1,
)::HistBoundaries
    hist = Float64[]
    dlast = 0.0
    d1 = d2 = nothing
    hist_first = true
    for (r1, r2) in rows
        d1, d2 = r1, r2
        if hist_first || d1 > dlast
            hist_first = false
            dlast = d1
            push!(hist, d1)
        end
    end
    if d2 !== nothing && d1 !== nothing && d2 > d1
        push!(hist, d2)
    end

    if length(hist) < ncolors + 1
        ncolors = length(hist) - 1
    end

    if data_scale !== nothing && data_scale != 0
        hist = [data_scale * v for v in hist]
    end

    if zmin < hist[1]
        hist[1] = zmin
    end
    if zmax > hist[ncolors+1]
        hist[ncolors+1] = zmax
    end
    hist[1] = hist[1] - 0.01 * dzz
    hist[ncolors+1] = hist[ncolors+1] + 0.01 * dzz

    if color_style == 1
        hist[ncolors] = hist[ncolors+1]
        pop!(hist)
    end

    return HistBoundaries(hist = hist, ncolors = ncolors)
end


"""Port of the color_mode == 4 (slope magnitude) @hist construction
(mbm_grdplot lines ~2142-2158): an evenly-spaced ramp from 0 to
`magnitude`, not data-driven. `color_style == 1` (continuous, the
only style this module ports) yields `ncolors` boundary points.
"""
function build_slope_boundaries(magnitude::Real, ncolors::Integer, color_style::Integer = 1)::Vector{Float64}
    if color_style == 1
        return [magnitude * i / (ncolors - 1) for i in 0:ncolors-1]
    end
    return [magnitude * i / ncolors for i in 0:ncolors]
end


"""Port of the color_style == 1 CPT-writing loops (mbm_grdplot
lines ~2228-2317) for the case where z boundaries come from an
explicit list (`@hist`, built by build_histogram_boundaries() or
build_slope_boundaries()) rather than a fixed color_int step.

Boundaries are consumed strictly front-to-back regardless of
`color_flip` (mirroring Perl's `shift @hist`, which pops from the
front no matter which direction the color-index loop below it
runs) -- only which base-palette samples pair with which z
boundary depends on `color_flip`, exactly as in
build_cpt_continuous(). Only the first `length(colors)` boundaries
are ever consumed; see build_histogram_boundaries() for why that
can leave the true data maximum boundary unused.

NOTE: this only covers color_mode == 4 (via build_slope_boundaries)
and the real histogram stretch (via build_histogram_boundaries).
The zmode==1 z-bound clamping mbm_grdplot also applies here is not
reproduced (it additionally special-cases color_mode == 4 in a way
this module does not yet model) -- pass zmode 0-equivalent input.
"""
function build_cpt_continuous_from_boundaries(
    colors::AbstractVector,
    boundaries::AbstractVector;
    color_flip::Bool = false,
)::Vector{CptLine}
    ncolors = length(colors)
    remaining = collect(Float64, boundaries)
    lines = CptLine[]
    d1 = popfirst!(remaining)

    if color_flip
        for i in 0:ncolors-2
            d2 = popfirst!(remaining)
            push!(lines, CptLine(d1, colors[i+1], d2, colors[i+2]))
            d1 = d2
        end
    else
        for i in ncolors-2:-1:0
            d2 = popfirst!(remaining)
            push!(lines, CptLine(d1, colors[i+2], d2, colors[i+1]))
            d1 = d2
        end
    end

    return lines
end


# ---------------------------------------------------------------------------
# Discrete CPT style (color_style != 1): one flat color per segment
# instead of a smooth gradient across it, and one more segment than
# the continuous style (ncolors segments instead of ncolors - 1) --
# see get_ncolors_use().
#
# mbm_grdplot used to index the same cptub/cptue interpolated-color
# arrays used by the continuous style, but over one more segment than
# those arrays held entries for (they were always sized off `ncolors`
# rather than `ncolors_use`). In Perl, the resulting out-of-range
# array read silently returned undef, which a numeric printf treats
# as 0 -- so one extreme segment (z_min end without flip, z_max end
# with flip) always rendered as a flat black/black segment. Fixed
# upstream and here by sizing the interpolated-color list off
# `ncolors_use` (see interpolate_palette()); confirmed against real
# mbm_grdplot -W2 output. See test_mbjl_discrete.jl.
# ---------------------------------------------------------------------------

"""Port of the color_style != 1 CPT-writing loops (mbm_grdplot
lines ~2318-2409), arithmetic (non-histogram) case: z boundaries
stepped by a fixed `color_int` from `color_start`.

`ncolors` is the requested discrete color count (number of CPT
segments produced); `colors` must be the `ncolors + 1`-length
(== get_ncolors_use(ncolors, 2)) list from
interpolate_palette() -- one more sample than `ncolors` colors, so
every segment has two real, distinct samples either side of it.
"""
function build_cpt_discrete(
    colors::AbstractVector,
    ncolors::Integer,
    color_start::Real,
    color_int::Real;
    color_flip::Bool = false,
    zmode::Integer = 0,
    zmin_t::Union{Nothing,Real} = nothing,
    zmax_t::Union{Nothing,Real} = nothing,
)::Vector{CptLine}
    if length(colors) != ncolors + 1
        throw(ArgumentError(
            "colors must have ncolors + 1 = $(ncolors + 1) entries " *
            "(got $(length(colors))); pass interpolate_palette(palette, " *
            "get_ncolors_use(ncolors, 2))"
        ))
    end
    cptub, cptue = colors[1:end-1], colors[2:end]
    return _write_cpt_discrete_arithmetic(
        cptub, cptue, ncolors, color_start, color_int, color_flip, zmode, zmin_t, zmax_t
    )
end


"""Shared core of build_cpt_discrete(): both cptub and cptue must
already be `ncolors` entries long (see
_write_cpt_continuous_arithmetic() for why this split exists)."""
function _write_cpt_discrete_arithmetic(
    cptub, cptue, ncolors, color_start, color_int, color_flip, zmode, zmin_t, zmax_t,
)::Vector{CptLine}
    lines = CptLine[]
    d1 = Float64(color_start)

    if color_flip
        for i in 0:ncolors-1
            d2 = d1 + color_int
            lo_color, hi_color = cptub[i+1], cptue[i+1]
            if zmode == 1
                if i == 0 && zmin_t !== nothing && zmin_t < d1
                    d1 = zmin_t
                end
                if i == ncolors - 1 && zmax_t !== nothing && zmax_t > d2
                    d2 = zmax_t
                end
            end
            push!(lines, CptLine(d1, lo_color, d2, hi_color))
            d1 = d2
        end
    else
        for i in ncolors-1:-1:0
            d2 = d1 + color_int
            lo_color, hi_color = cptue[i+1], cptub[i+1]
            if zmode == 1
                if i == ncolors - 1 && zmin_t !== nothing && zmin_t < d1
                    d1 = zmin_t
                end
                if i == 0 && zmax_t !== nothing && zmax_t > d2
                    d2 = zmax_t
                end
            end
            push!(lines, CptLine(d1, lo_color, d2, hi_color))
            d1 = d2
        end
    end

    return lines
end


"""Public entry point for the discrete-style sealevel dual-colormap
case: pass the (cptub, cptue) arrays from interpolate_palette_sealevel()
directly -- see build_cpt_continuous_sealevel()'s docstring."""
function build_cpt_discrete_sealevel(
    cptub::AbstractVector,
    cptue::AbstractVector,
    ncolors::Integer,
    color_start::Real,
    color_int::Real;
    color_flip::Bool = false,
    zmode::Integer = 0,
    zmin_t::Union{Nothing,Real} = nothing,
    zmax_t::Union{Nothing,Real} = nothing,
)::Vector{CptLine}
    return _write_cpt_discrete_arithmetic(
        cptub, cptue, ncolors, color_start, color_int, color_flip, zmode, zmin_t, zmax_t
    )
end


"""Port of the color_style != 1 CPT-writing loops (mbm_grdplot
lines ~2318-2409) for the histogram/slope-magnitude case: z
boundaries popped off an explicit list rather than stepped by a
fixed color_int. Discrete style consumes the *entire* boundary
list (one initial pop plus `ncolors` more, matching a list of
`ncolors + 1` entries exactly) -- so the true padded data maximum
from build_histogram_boundaries() is used here regardless of
color_style.

`colors` must have `ncolors + 1` entries -- see build_cpt_discrete().
"""
function build_cpt_discrete_from_boundaries(
    colors::AbstractVector,
    ncolors::Integer,
    boundaries::AbstractVector;
    color_flip::Bool = false,
)::Vector{CptLine}
    if length(colors) != ncolors + 1
        throw(ArgumentError(
            "colors must have ncolors + 1 = $(ncolors + 1) entries " *
            "(got $(length(colors))); pass interpolate_palette(palette, " *
            "get_ncolors_use(ncolors, 2))"
        ))
    end
    remaining = collect(Float64, boundaries)
    lines = CptLine[]
    d1 = popfirst!(remaining)

    if color_flip
        for i in 0:ncolors-1
            d2 = popfirst!(remaining)
            push!(lines, CptLine(d1, colors[i+1], d2, colors[i+2]))
            d1 = d2
        end
    else
        for i in ncolors-1:-1:0
            d2 = popfirst!(remaining)
            push!(lines, CptLine(d1, colors[i+2], d2, colors[i+1]))
            d1 = d2
        end
    end

    return lines
end

end # module MbjlColor
