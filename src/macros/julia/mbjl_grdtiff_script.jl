#--------------------------------------------------------------------
#    The MB-system:  mbjl_grdtiff_script.jl
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
#   Julia port of src/macros/python/mbpy_grdtiff_script.py, the script
#   generator for mbpy_grdtiff (the Python port of the Perl macro
#   mbm_grdtiff). Builds the color palette (via mbjl_color.jl) and
#   writes a GMT **modern mode** shellscript that runs MB-System's own
#   `gmt mbgrdtiff` supplement module to make a georeferenced TIFF
#   (GeoTIFF plus .tfw world file) of a grid, colored by a CPT and
#   optionally shaded (color_mode 1-5, the same modes as mbm_grdtiff).
#
#   mbgrdtiff writes its image file directly rather than adding a layer
#   to a figure, so the script opens a figure-less session (`gmt begin`
#   with no figure name) only so that `gmt set` defaults stay inside the
#   session rather than in a gmt.conf in the current directory (which
#   mbm_grdtiff had to delete before and after).
#
#   The shading pipelines (color_mode 2/3/4/5) are the same as
#   mbm_grdplot's and mbm_grdtiff's, so they are reused from
#   mbjl_grdplot_script.jl rather than duplicated here.
#
#   Differences from mbm_grdtiff (the same as mbpy_grdtiff's):
#   - A list of grids given to -I (one per line) is not supported; run
#     mbjl_grdtiff once per grid.
#   - mbm_grdtiff's -MGW (world file) is not offered: mbgrdtiff always
#     writes the .tfw world file.
#   - Discrete palettes (-W2/...) and the histogram color stretch (-S)
#     follow the current mbm_grdplot and mbjl_grdplot, so they differ
#     slightly from mbm_grdtiff, which never received mbm_grdplot's
#     fixes there (mbm_grdplot samples ncolors + 1 palette colors for
#     discrete palettes and runs grdhisteq with ncolors bins).
#   - The sealevel palettes 8 and 9 of mbjl_grdplot are available too.
#   - The image is not displayed after it is made (as in the other
#     mbjl_* macros).
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))
isdefined(@__MODULE__, :MbjlLayout) || include(joinpath(@__DIR__, "mbjl_layout.jl"))
isdefined(@__MODULE__, :MbjlColor) || include(joinpath(@__DIR__, "mbjl_color.jl"))
isdefined(@__MODULE__, :MbjlMisc) || include(joinpath(@__DIR__, "mbjl_misc.jl"))
isdefined(@__MODULE__, :MbjlGrdplotScript) || include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))

module MbjlGrdtiffScript

using ..MbjlUtil
import ..MbjlLayout
import ..MbjlColor
const color = MbjlColor
using ..MbjlColor: CptLine, ColorControl
using ..MbjlGrdplotScript: ShadeOptions, resolve_shade_defaults, write_cpt_file
import ..MbjlGrdplotScript
const grdplot = MbjlGrdplotScript

export COLOR_MODE_NAMES, grid_zrange, compute_contour_int, build_cpt, ScriptOptionsTiff,
       tiff_file, script_path, generate_script_tiff, generate_and_write_tiff,
       ShadeOptions, resolve_shade_defaults

const COLOR_MODE_NAMES = Dict(
    1 => "Color Fill",
    2 => "Color Shaded Relief",
    3 => "Color Shaded by Intensity File",
    4 => "Color Fill of Slope Magnitude",
    5 => "Color Shaded by Slope Magnitude",
)


"""z minimum and maximum of `grid` inside `region` ("W/E/S/N"), as
mbm_grdtiff gets them from mbm_grdinfo -R when -R is given, so the
color stretch spans the imaged part of the grid only."""
function grid_zrange(grid::AbstractString, region::AbstractString)::Tuple{Float64,Float64}
    out, _ = MbjlLayout._run_capture(`gmt grdinfo -C $grid -R$region`)
    # -C columns: name w e s n z0 z1 dx dy nx ny ... (0-based; 6th/7th here)
    cols = split(out)
    return pyfloat(cols[6]), pyfloat(cols[7])
end


"""Port of mbm_grdtiff's "reasonable color intervals" base step
(the contour interval mbm_grdplot uses too), from which
MbjlColor.compute_color_interval() derives the color interval."""
function compute_contour_int(zmin::Real, zmax::Real)::Float64
    dzz = zmax - zmin
    contour_int = 0.0
    if dzz > 0
        base = pytrunc((log(dzz) / log(10.0)) + 0.5)
        # exp10, not 10.0^x (see MbjlLayout.get_base_tick())
        contour_int = exp10(base) / 10.0
        if dzz / contour_int < 10
            contour_int = contour_int / 4
        elseif dzz / contour_int < 20
            contour_int = contour_int / 2
        end
    end
    return contour_int
end


"""Build the CPT for mbgrdtiff, following mbm_grdtiff's -W/-S/-Y/-Z
dispatch (mbm_grdtiff lines ~700-1150) with the shared MbjlColor
helpers. `zmin`/`zmax` are the (possibly -Z overridden, already
data_scale-rescaled) z bounds used for the color stretch;
`zmin_t`/`zmax_t` are the data's own extremes, which zmode 1 uses
to extend the end segments so no data value falls outside the CPT.
`cc` must not name an existing CPT file (the caller uses that file
directly instead).
"""
function build_cpt(
    grid::AbstractString,
    color_mode::Integer,
    cc::ColorControl,
    zmin::Real,
    zmax::Real;
    zmode::Integer = 0,
    zmin_t::Union{Nothing,Real} = nothing,
    zmax_t::Union{Nothing,Real} = nothing,
    color_flip::Bool = false,
    no_nice_interval::Bool = false,
    stretch_color::Bool = false,
    magnitude::Union{Nothing,Real} = nothing,
    data_scale::Union{Nothing,Real} = nothing,
)::Vector{CptLine}
    dzz = zmax - zmin
    ncolors_use = color.get_ncolors_use(cc.ncolors, cc.color_style)
    is_sealevel = cc.color_palette in color.SEALEVEL_PALETTES

    if color_mode == 4
        mag = magnitude !== nothing ? magnitude : 1.0
        boundaries = color.build_slope_boundaries(mag, ncolors_use, cc.color_style)
        colors = color.interpolate_palette(cc.color_palette, ncolors_use)
        if cc.color_style == 1
            return color.build_cpt_continuous_from_boundaries(colors, boundaries; color_flip = color_flip)
        end
        return color.build_cpt_discrete_from_boundaries(colors, cc.ncolors, boundaries; color_flip = color_flip)
    end

    if stretch_color && is_sealevel
        nbins = cc.color_style == 1 ? ncolors_use - 1 : cc.ncolors
        rows_below, rows_above = color.run_grdhisteq_sealevel(grid, nbins, zmin, zmax)
        below, above = color.build_sealevel_histogram_boundaries(rows_below, rows_above, zmin, zmax, dzz)
        return color.build_cpt_sealevel_from_boundaries(cc.color_palette, below, above; color_flip = color_flip)
    end

    if stretch_color
        # grdhisteq runs on the unscaled input grid, so its boundaries
        # are rescaled to the data_scale'd z units the image uses
        rows = color.run_grdhisteq(grid, cc.ncolors)
        if data_scale !== nothing
            scaled = [(lo * data_scale, hi * data_scale) for (lo, hi) in rows]
            rows = sort([(min(lo, hi), max(lo, hi)) for (lo, hi) in scaled])
        end
        hb = color.build_histogram_boundaries(rows, cc.ncolors, zmin, zmax, dzz; color_style = cc.color_style)
        nsamples = cc.color_style == 1 ? hb.ncolors : hb.ncolors + 1
        colors = color.interpolate_palette(cc.color_palette, nsamples)
        if cc.color_style == 1
            return color.build_cpt_continuous_from_boundaries(colors, hb.hist; color_flip = color_flip)
        end
        return color.build_cpt_discrete_from_boundaries(colors, hb.ncolors, hb.hist; color_flip = color_flip)
    end

    contour_int = compute_contour_int(zmin, zmax)
    interval = color.compute_color_interval(dzz, zmin, zmax, ncolors_use, contour_int, no_nice_interval)
    if is_sealevel
        izero = color.compute_sealevel_izero(
            ncolors_use, interval.color_int, interval.color_start, interval.color_end, color_flip
        )
        cptub, cptue = color.interpolate_palette_sealevel(cc.color_palette, ncolors_use, izero)
        if cc.color_style == 1
            return color.build_cpt_continuous_sealevel(
                cptub, cptue, interval.color_start, interval.color_int;
                color_flip = color_flip, zmode = zmode, zmin_t = zmin_t, zmax_t = zmax_t)
        end
        return color.build_cpt_discrete_sealevel(
            cptub, cptue, cc.ncolors, interval.color_start, interval.color_int;
            color_flip = color_flip, zmode = zmode, zmin_t = zmin_t, zmax_t = zmax_t)
    end
    colors = color.interpolate_palette(cc.color_palette, ncolors_use)
    if cc.color_style == 1
        return color.build_cpt_continuous(
            colors, interval.color_start, interval.color_int;
            color_flip = color_flip, zmode = zmode, zmin_t = zmin_t, zmax_t = zmax_t)
    end
    return color.build_cpt_discrete(
        colors, cc.ncolors, interval.color_start, interval.color_int;
        color_flip = color_flip, zmode = zmode, zmin_t = zmin_t, zmax_t = zmax_t)
end


Base.@kwdef mutable struct ScriptOptionsTiff
    grid::String
    root::String
    region::String                                   # -R for mbgrdtiff (grid bounds, or the user's -R)
    geographic::Bool = true                          # grid is in lon/lat: grdgradient gets -fg
    color_mode::Int = 1                              # 1-5, as in mbm_grdtiff -G
    shade::Union{Nothing,ShadeOptions} = nothing     # color_mode 2-5
    cpt_lines::Union{Nothing,Vector{CptLine}} = nothing   # generated CPT, written to <root>.cpt
    cpt_file::Union{Nothing,String} = nothing        # OR: an existing CPT used as-is
    color_flip::Bool = false                         # swaps COLOR_BACKGROUND/COLOR_FOREGROUND
    data_scale::Union{Nothing,Float64} = nothing     # rescale the grid before coloring/shading
    nudge::Union{Nothing,String} = nothing           # "dx/dy" meters east/north (mbgrdtiff -N)
    gmt_defs::Vector{String} = String[]              # user "PARAM/value" overrides
end

tiff_file(opts::ScriptOptionsTiff) = "$(opts.root).tif"
script_path(opts::ScriptOptionsTiff) = "$(opts.root)_tiff.sh"

_given(x) = x !== nothing && !(x isa AbstractString && isempty(x))


"""Build the GMT modern-mode shellscript text. Does not touch the
filesystem; see generate_and_write_tiff()."""
function generate_script_tiff(opts::ScriptOptionsTiff, command_line::Union{Nothing,AbstractString} = nothing)::String
    cptfile = _given(opts.cpt_file) ? opts.cpt_file : "$(opts.root).cpt"
    q = shquote

    lines = String[
        "#!/usr/bin/env bash",
        "#",
        "# GMT modern-mode script to create a GeoTIFF image of $(opts.grid)",
        "# Generated by mbjl_grdtiff (Julia port of mbm_grdtiff)",
    ]
    if _given(command_line)
        push!(lines, "#   $(command_line)")
    end
    append!(lines, [
        "#",
        "set -e",
        "",
        "# mbgrdtiff writes the image itself; the figure-less session only",
        "# keeps the GMT defaults below out of ./gmt.conf",
        "gmt begin",
        "",
        "# Set temporary GMT defaults",
    ])
    if opts.color_flip
        push!(lines, "gmt set COLOR_BACKGROUND white COLOR_FOREGROUND black COLOR_NAN white")
    else
        push!(lines, "gmt set COLOR_BACKGROUND black COLOR_FOREGROUND white COLOR_NAN white")
    end
    push!(lines, "")

    if !isempty(opts.gmt_defs)
        push!(lines, "# User-defined GMT parameter overrides")
        pairs = String[]
        for gmt_def in opts.gmt_defs
            param, value = grdplot._partition(gmt_def, "/")
            push!(pairs, "$param $value")
        end
        push!(lines, "gmt set $(join(pairs, " "))")
        push!(lines, "")
    end

    file_use = q(opts.grid)
    cleanup = String[]
    if opts.data_scale !== nothing
        file_use = "$(q(opts.root))_scaled.grd"
        push!(lines, "# Rescale data")
        push!(lines, "gmt grdmath $(q(opts.grid)) $(fmtg(opts.data_scale, 6)) MUL = $file_use")
        push!(lines, "")
        push!(cleanup, file_use)
    end

    grid_for_image = file_use
    intensity_file = nothing
    shade = opts.shade !== nothing ? resolve_shade_defaults(opts.shade) : nothing
    root = q(opts.root)

    if opts.color_mode == 2
        cmds, intensity_file = grdplot._illumination_commands(
            file_use, root, shade.magnitude, shade.azimuth, shade.elevation, opts.geographic)
        push!(lines, "# Get shading array")
        append!(lines, cmds)
        push!(lines, "")
        push!(cleanup, intensity_file)
    elseif opts.color_mode == 3
        if shade === nothing || !_given(shade.file_intensity)
            throw(ArgumentError("color_mode 3 requires an intensity file"))
        end
        cmds, intensity_file = grdplot._intensity_commands_mode3(
            q(shade.file_intensity), root, shade.stretch_shade, shade.magnitude)
        if !isempty(cmds)
            push!(lines, "# Get shading array")
            append!(lines, cmds)
            push!(lines, "")
            push!(cleanup, intensity_file)
        end
    elseif opts.color_mode == 4
        cmds, grid_for_image = grdplot._slope_commands(file_use, root, opts.geographic, nothing)
        push!(lines, "# Get slope array")
        append!(lines, cmds)
        push!(lines, "")
        push!(cleanup, grid_for_image)
    elseif opts.color_mode == 5
        cmds, intensity_file = grdplot._slope_commands(file_use, root, opts.geographic, shade.magnitude)
        push!(lines, "# Get slope array")
        append!(lines, cmds)
        push!(lines, "")
        push!(cleanup, intensity_file)
    elseif opts.color_mode != 1
        throw(ArgumentError("unsupported color_mode: $(opts.color_mode)"))
    end

    parts = String[
        "gmt mbgrdtiff -I$(grid_for_image)",
        "-O$(q(tiff_file(opts)))",
        "-R$(opts.region) -Jx1.0",
    ]
    if _given(opts.nudge)
        push!(parts, "-N$(opts.nudge)")
    end
    push!(parts, "-C$(q(cptfile))")
    if _given(intensity_file)
        # mbgrdtiff takes the intensity grid as its second -I
        push!(parts, "-I$(intensity_file)")
    end
    push!(lines, "# Make the GeoTIFF image (and its .tfw world file)")
    push!(lines, join(parts, " "))
    push!(lines, "")

    if !isempty(cleanup)
        push!(lines, "# Delete surplus files")
        push!(lines, "rm -f " * join(cleanup, " "))
        push!(lines, "")
    end

    push!(lines, "gmt end")
    push!(lines, "")
    return join(lines, "\n")
end


"""Write the CPT (when generated) and the shellscript; make the
script executable and return its path."""
function generate_and_write_tiff(opts::ScriptOptionsTiff, command_line::Union{Nothing,AbstractString} = nothing)::String
    if opts.cpt_lines !== nothing && opts.cpt_file === nothing
        write_cpt_file("$(opts.root).cpt", opts.cpt_lines)
    end
    path = script_path(opts)
    write(path, generate_script_tiff(opts, command_line))
    chmod(path, 0o755)
    return path
end

end # module MbjlGrdtiffScript
