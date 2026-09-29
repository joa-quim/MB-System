#--------------------------------------------------------------------
#    The MB-system:  mbjl_grd3dplot_script.jl
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
#   Julia port of src/macros/python/mbpy_grd3dplot_script.py, itself a
#   port of mbm_grd3dplot's page-layout and script-generation
#   logic, targeting GMT **modern mode** (`gmt begin`/`gmt end`,
#   `gmt grdview`/`gmt coast`/`gmt colorbar`/`gmt basemap`/`gmt text`)
#   instead of mbm_grd3dplot's own classic-mode <root>.cmd generator.
#
#   mbm_grd3dplot shares most of its page-size/projection/color-palette
#   machinery verbatim with mbm_grdplot (confirmed by direct diff of the
#   two Perl sources): GetPageSize, GetProjection, color palette tables,
#   color-interval and CPT-interpolation math, and the color_mode 2/3/4/5
#   shading pipelines are all identical. Those pieces are imported here
#   from mbjl_layout.jl / mbjl_color.jl / mbjl_grdplot_script.jl rather
#   than re-implemented. What genuinely differs for a 3D perspective
#   plot is ported fresh below:
#
#     - a view azimuth/elevation (mbm_grd3dplot's -E, "azimuth/elevation",
#       default "240/30") that both rotates the apparent plot footprint
#       (mbm_grd3dplot lines ~1004-1241: an extra x_axis_rot/y_axis_rot
#       correction folded into the page-fit scale/width search) and
#       selects which two sides of the frame get axis ticks (lines
#       ~1952-1967: NEZ/SEZ/WSZ/WNZ depending on which 90-degree
#       quadrant the azimuth falls in);
#     - a z-axis scale/exaggeration (mbm_grd3dplot's -F/z-scale-via-J,
#       lines ~1243-1286): computed from a geodetic meters-per-degree
#       conversion (the same C1..C7 constants mbm_grd3dplot uses) so
#       that a requested vertical exaggeration comes out correct in the
#       final plot regardless of whether the grid is geographic;
#     - a z-axis tick interval (mbm_grd3dplot's own GetBaseTick, lines
#       ~3031-3120): notably a *reduced* version of mbjl_layout's -- it
#       always uses the geographic degree/minute/second tick table for
#       x/y regardless of the grid's actual projection type (unlike
#       MbjlLayout.get_base_tick(), which branches on gridprojected),
#       plus a new z tick fixed at (zmax-zmin)/5 with no "nice-number"
#       snapping at all. Reproduced as-is (not "fixed") to match
#       mbm_grd3dplot's real behavior -- this looks like an
#       under-developed corner of the original macro (it never received
#       mbm_grdplot's later UTM/seismic/generic-linear handling), but a
#       port should not silently improve on it without being asked.
#     - grdview itself (color_mode 1-5: -Qi<dpi> image rendering, same
#       CPT/-I<shade or slope file> plumbing as 2D grdimage; color_mode
#       6/7: -Qm mesh rendering, with mode 7 adding a contour pen -W
#       directly on the same grdview call -- there is no separate
#       grdcontour step for 3D plots at all).
#
#   NOTE on a real, confirmed-dead option in mbm_grd3dplot itself: the
#   macro defines a -C<contour_control> option and a $contour_mode/
#   $contour_control pair of variables, but -- unlike mbm_grdplot's -C,
#   which drives an actual grdcontour overlay -- grep across the whole
#   source shows $contour_control is only ever read to print a verbose
#   summary line; it is never passed to any GMT module. $contour_mode
#   only affects one thing: whether color_mode defaults to 1 when
#   otherwise unset. In other words, mbm_grd3dplot's "contour" option
#   does not draw contours. This port does not expose a --contour flag
#   at all rather than port a no-op long option; color_mode 7's
#   contour-on-mesh (-MVW / --mesh-contour-pen below) is the only real
#   contour capability mbm_grd3dplot has.
#
#   A known GMT-installation limitation, not a bug in this port or in
#   mbm_grd3dplot: `gmt grdview -Qm -W<pen>` (color_mode 7 -- mesh with
#   contour lines, i.e. --mesh-contour-pen) reliably segfaults inside
#   GMT_grdview under a 3-D perspective (-p) with -Jz active, on at
#   least GMT 6.7.0, regardless of the pen syntax used. Plain -Qm
#   (color_mode 6, no contour pen) works fine. This looks like an
#   upstream GMT bug triggered by the -Qm/-W/-p/-Jz combination; there
#   is no script-side workaround that preserves the actual requested
#   mesh+contour rendering, so it is simply documented here rather than
#   silently avoided.
#
#   A second real, confirmed bug in mbm_grd3dplot itself (not just a
#   dead option): its -M sub-option parser splits the concatenated
#   -M<sub1>:<sub2>:... string on the literal 7-character separator
#   ":::::::" (mbm_grd3dplot line ~355: `split(/:::::::/, $misc)`)
#   while MBGetopts (its own option-concatenation helper) joins repeated
#   -M invocations with a *single* colon. Since a normal -M string never
#   contains 7 consecutive colons, this split never actually splits
#   anything: every -M sub-option after the first on any command line
#   using more than one gets silently absorbed into the first
#   sub-option's own greedy \S+ value instead of being parsed on its own
#   (confirmed against mbm_grd3dplot's real "GLfx.../GQ300/IE300/ITg"
#   -M chain, which is exactly the shape real usage takes -- see
#   src/macros/mbm_grd3dplot's own process.cmd-style callers). This port
#   sidesteps the whole issue by construction: like mbjl_grdplot, it
#   exposes each concept as its own descriptive long option and never
#   builds or re-parses a colon-joined -M string at all.
#
# Status:
#   Covers color_mode 1-5 (color fill / shaded relief / intensity-file
#   shading / slope-magnitude fill / slope-magnitude shading, identical
#   math to mbjl_grdplot_script.jl), 6-7 (mesh plot, with or without
#   contour lines), an optional coastline overlay, drape file, null
#   plane, and z-level. Not ported: swath navigation overlay and xy
#   overlay -- mbm_grd3dplot itself has no such options (confirmed: no
#   -MN or -MX sub-option dispatch anywhere in its source), unlike
#   mbm_grdplot. Verified by running the generated script through a
#   real GMT 6 installation; not exhaustively line-by-line diffed
#   against Perl output the way mbjl_grdplot_script.jl was (see that
#   module's own Status note for the difference in rigor). The Julia
#   port itself was verified by diffing its generated scripts/CPTs
#   against the Python original's for the same inputs.
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))
isdefined(@__MODULE__, :MbjlLayout) || include(joinpath(@__DIR__, "mbjl_layout.jl"))
isdefined(@__MODULE__, :MbjlColor) || include(joinpath(@__DIR__, "mbjl_color.jl"))
isdefined(@__MODULE__, :MbjlMisc) || include(joinpath(@__DIR__, "mbjl_misc.jl"))
isdefined(@__MODULE__, :MbjlGrdplotScript) || include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))

module MbjlGrd3dplotScript

using ..MbjlUtil
using ..MbjlLayout
using ..MbjlColor: CptLine
using ..MbjlMisc: CoastOptions
using ..MbjlGrdplotScript
using ..MbjlGrdplotScript: ShadeOptions, resolve_shade_defaults, _illumination_commands,
                           _intensity_commands_mode3, _slope_commands,
                           baseline_gmt_defaults, write_cpt_file

const layout = MbjlLayout

export get_base_tick_z, Layout3D, compute_layout_3d, ScriptOptions3D,
       generate_script_3d, generate_and_write_3d, ShadeOptions

const _DTR = 3.1415926 / 180.0

# Geodetic meters-per-degree conversion constants (mbm_grd3dplot lines
# ~1244-1256), used only to convert a requested vertical --exaggeration
# into a map z-scale (or vice versa) for a geographic grid.
const _C1, _C2, _C3 = 111412.84, -93.5, 0.118
const _C4, _C5, _C6, _C7 = 111132.92, -559.82, 1.175, 0.0023


"""Port of the z-tick half of mbm_grd3dplot's own GetBaseTick
(line ~3119): a plain (zmax-zmin)/5, with no "nice number" rounding
at all -- unlike every other tick interval in either macro."""
get_base_tick_z(zmin::Real, zmax::Real)::String = pystr((zmax - zmin) / 5.0)


Base.@kwdef mutable struct Layout3D
    pagesize::String
    scale_loc::String
    projection::String
    projection_pars::String
    region::String
    zmin::Float64
    zmax::Float64
    view_azimuth::Float64
    view_elevation::Float64
    map_zscale::Float64
    exaggeration::Float64
    landscape::Bool
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
    base_tick_xy::String
    base_tick_z::String
    gridprojected::Int = 0   # resolved value (see compute_layout_3d()'s docstring
                             # on the out-of-bounds fallback) -- callers building a
                             # GridInfo-consuming ScriptOptions3D from the same grid
                             # should use this instead of the raw GridInfo.gridprojected
end


_truthy(x) = x !== nothing && x != 0

"""Port of mbm_grd3dplot's page/scale-fit algorithm (lines ~700-1292):
the same GetPageSize/GetProjection heuristics MbjlLayout.compute_layout
uses, plus the view-azimuth footprint-rotation correction and the
z-scale/exaggeration calculation that are specific to a 3D plot.
"""
function compute_layout_3d(
    grid::GridInfo;
    view_control::AbstractString = "240/30",
    pagesize::AbstractString = "a",
    scale_loc::AbstractString = "b",
    map_scale::Union{Nothing,AbstractString} = nothing,
    orientation::Integer = 0,
    region::Union{Nothing,AbstractString} = nothing,
    exaggeration::Union{Nothing,Real} = nothing,
    map_zscale::Union{Nothing,Real} = nothing,
)::Layout3D
    pagesize = String(pagesize)
    map_scale = (map_scale === nothing || isempty(map_scale)) ? nothing : String(map_scale)
    region = (region === nothing || isempty(region)) ? nothing : String(region)
    xmin, xmax, ymin, ymax = grid.xmin, grid.xmax, grid.ymin, grid.ymax
    zmin, zmax = grid.zmin, grid.zmax

    # Port of mbm_grd3dplot's own -R handling (mirrors mbm_grdplot's
    # equivalent -- see MbjlLayout.compute_layout()'s docstring on this
    # gap): a user-supplied -R overrides xmin/xmax/ymin/ymax themselves,
    # not just the final plot region string, since they drive the
    # apparent-plot-size/page-fit computation below.
    if region !== nothing
        xmin, xmax, ymin, ymax = (pyfloat(v) for v in split(region, "/")[1:4])
    end

    view_azimuth, view_elevation = (pyfloat(v) for v in split(view_control, "/"))

    margins = layout.get_page_size(pagesize, scale_loc)

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
        use_ratio, linear = spec.use_ratio, spec.linear
        separator, trial_value = spec.separator, spec.trial_value
        plot_scale, plot_width = spec.plot_scale, spec.plot_width
    end

    # Port of mbm_grd3dplot line ~867 (same fallback as mbm_grdplot's own
    # copy -- see MbjlLayout.compute_layout()'s docstring on this): treat
    # the grid as projected, not geographic, if its plot bounds
    # (xmin/xmax/ymin/ymax, already reflecting a -R override above if
    # given) fall outside plausible lon/lat ranges, even when
    # run_grdinfo() didn't recognize its projection metadata as a tagged
    # "UTM Zone".
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
    dxx, dyy = layout._mapproject_bbox(xmin, xmax, ymin, ymax, projection, projection_pars, bounds_plot)
    dxx, dyy = abs(dxx), abs(dyy)

    # view-azimuth footprint rotation, common to all three scale-fit
    # branches below (mbm_grd3dplot's own "(kluge)" comment on this,
    # line ~1225, is the original author's, not this port's)
    x_axis_rot = abs(dxx * cos(_DTR * view_azimuth))
    y_axis_rot = abs(dyy * sin(_DTR * view_azimuth))

    landscape = portrait = false
    width = height = 0.0
    width_max = height_max = 0.0

    if (use_scale && _truthy(plot_scale)) || (use_width && _truthy(plot_width))
        plot_width_factor = (x_axis_rot + y_axis_rot) / dxx
        plot_width_ = plot_width_factor * dxx
        plot_height_ = plot_width_factor * dyy

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
                m2 = layout.get_page_size(elem, scale_loc)
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
            margins = layout.get_page_size(pagesize, scale_loc)
            width = portrait ? layout.PAGE_WIDTH_IN[pagesize] : layout.PAGE_HEIGHT_IN[pagesize]
            height = portrait ? layout.PAGE_HEIGHT_IN[pagesize] : layout.PAGE_WIDTH_IN[pagesize]
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

        plot_width_raw = dxx * plot_scale
        plot_height_raw = dyy * plot_scale

        plot_scale_factor = dxx / (x_axis_rot + y_axis_rot)
        plot_scale = plot_scale_factor * plot_scale
        plot_width = plot_scale_factor * plot_width_raw
        plot_height = plot_scale_factor * plot_height_raw

        if use_ratio
            top = pytrunc(1 / plot_scale)
            projection_pars = "$(layout._base_pars(projection, map_scale))$(separator)1:$(top)"
        else
            projection_pars = layout._sprintf_g5(layout._base_pars(projection, map_scale), separator, plot_scale)
        end
        if linear && gridprojected == 0
            projection_pars = "$(projection_pars)d"
        end

    elseif use_width
        plot_width_landscape = min(margins.height_max_landscape * dxx / dyy, margins.width_max_landscape)
        plot_width_portrait = min(margins.height_max_portrait * dxx / dyy, margins.width_max_portrait)

        if orientation == 1
            portrait = true
            plot_width_raw = plot_width_portrait
        elseif orientation == 2
            landscape = true
            plot_width_raw = plot_width_landscape
        elseif plot_width_landscape > plot_width_portrait
            landscape = true
            plot_width_raw = plot_width_landscape
        else
            portrait = true
            plot_width_raw = plot_width_portrait
        end

        plot_height_raw = plot_width_raw * dyy / dxx
        if landscape
            width, height = layout.PAGE_HEIGHT_IN[pagesize], layout.PAGE_WIDTH_IN[pagesize]
        else
            width, height = layout.PAGE_WIDTH_IN[pagesize], layout.PAGE_HEIGHT_IN[pagesize]
        end

        # NOTE: mbm_grd3dplot's own use_width branch (line ~1225-1230)
        # computes "$plots_width_factor" here (with an s) but then goes
        # on to *use* "$plot_width_factor" (without the s) -- a variable
        # from the unrelated use_scale branch above that is undefined on
        # this code path (Perl treats it as 0), which would zero out
        # plot_width_xaxis/plot_height_yaxis entirely. This looks like a
        # genuine bug in mbm_grd3dplot itself, not intentional behavior
        # to reproduce; this port uses the correctly-computed factor.
        plot_width_factor = dxx / (x_axis_rot + y_axis_rot)
        plot_width = plot_width_factor * plot_width_raw
        plot_height = plot_width_factor * plot_height_raw

        projection_pars = layout._sprintf_g5(layout._base_pars(projection, map_scale), separator, plot_width)
        if linear && gridprojected == 0
            projection_pars = "$(projection_pars)d"
        end
    end

    # z-scale / vertical exaggeration (mbm_grd3dplot lines ~1243-1286)
    radlat = 0.5 * (ymax + ymin) * _DTR
    mtodeglat = 1.0 / abs(_C4 + _C5 * cos(2 * radlat) + _C6 * cos(4 * radlat) + _C7 * cos(6 * radlat))
    mtodeglon = 1.0 / abs(_C1 * cos(radlat) + _C2 * cos(3 * radlat) + _C3 * cos(5 * radlat))
    if map_zscale === nothing && exaggeration !== nothing
        if gridprojected != 0
            map_zscale = exaggeration * plot_width / (xmax - xmin)
        else
            map_zscale = exaggeration * plot_width * mtodeglon / (xmax - xmin)
        end
    elseif map_zscale === nothing
        map_zscale = 2.0 / (zmax - zmin)
    end
    if exaggeration === nothing
        if gridprojected != 0
            exaggeration = map_zscale * (xmax - xmin) / plot_width
        else
            exaggeration = map_zscale * (xmax - xmin) / plot_width / mtodeglon
        end
    end

    xoffset = (width - abs(plot_width) - margins.space_left - margins.space_right) / 2 + margins.space_left
    yoffset = (height - abs(plot_height) - margins.space_bottom - margins.space_top) / 2 + margins.space_bottom

    # degree annotation format -- NOTE: threshold is 1.0 here, not the
    # 4.0 MbjlLayout.compute_layout() uses for 2D plots (mbm_grd3dplot
    # line ~1308 vs. mbm_grdplot line ~1615): confirmed as a genuine
    # difference between the two macros, not a transcription slip.
    degree_format = "ddd:mm"
    if gridprojected == 0
        xsize = (xmax - xmin) / 3
        ysize = (ymax - ymin) / 3
        size = min(xsize, ysize)
        if size > 1.0
            degree_format = "ddd"
        elseif size > (1.0 / 60.0)
            degree_format = "ddd:mm"
        else
            degree_format = "ddd:mm:ss"
        end
    end

    sl = lowercase(scale_loc)
    page_h = layout.PAGE_HEIGHT_IN[pagesize]
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

    dzz = zmax - zmin
    contour_int = 0.0
    if dzz > 0
        base = pytrunc((log(dzz) / log(10.0)) + 0.5)
        contour_int = (10.0^base) / 10.0
        if dzz / contour_int < 10
            contour_int = contour_int / 4
        elseif dzz / contour_int < 20
            contour_int = contour_int / 2
        end
    end

    base_tick_xy = layout.get_base_tick(0, xmin, xmax, ymin, ymax).tick_x
    base_tick_z = get_base_tick_z(zmin, zmax)

    return Layout3D(
        pagesize = pagesize, scale_loc = String(scale_loc),
        projection = projection, projection_pars = projection_pars, region = bounds_plot,
        zmin = zmin, zmax = zmax,
        view_azimuth = view_azimuth, view_elevation = view_elevation,
        map_zscale = map_zscale, exaggeration = exaggeration,
        landscape = landscape, plot_width = plot_width, plot_height = plot_height,
        xoffset = xoffset, yoffset = yoffset, degree_format = degree_format,
        colorscale_length = colorscale_length, colorscale_thick = colorscale_thick,
        colorscale_offx = colorscale_offx, colorscale_offy = colorscale_offy, colorscale_vh = colorscale_vh,
        contour_int = contour_int, base_tick_xy = base_tick_xy, base_tick_z = base_tick_z,
        gridprojected = gridprojected,
    )
end


_nonempty(s) = s !== nothing && !isempty(s)

"""Port of the -B assembly at mbm_grd3dplot lines ~1941-1971: an x/y/z
tick spec plus a view-azimuth-dependent choice of which two frame
sides (of the four NSEW verticals) get ticks/annotations, since a 3D
view can only usefully show the two walls facing the viewer."""
function _basemap_axes_3d(lay::Layout3D, tick_info, title)::String
    if _nonempty(tick_info)
        interval = "-B$(tick_info)"
    else
        interval = "-Bxy$(lay.base_tick_xy) -Bz$(lay.base_tick_z)"
    end

    # mod() rather than rem(): Python's % takes the sign of the divisor
    az = mod(lay.view_azimuth, 360.0)
    if 0.0 <= az < 90.0
        sides = "NEZ"
    elseif 90.0 <= az < 180.0
        sides = "SEZ"
    elseif 180.0 <= az < 270.0
        sides = "WSZ"
    else
        sides = "WNZ"
    end
    frame = "-B$(sides)" * (_nonempty(title) ? "+t\"$(title)\"" : "")

    return "$frame $interval"
end


Base.@kwdef mutable struct ScriptOptions3D
    grid::String
    root::String
    layout::Layout3D
    grid_info::GridInfo
    output_format::String = "pdf"
    title::Union{Nothing,String} = nothing
    tick_info::Union{Nothing,String} = nothing
    colorbar_label::Union{Nothing,String} = nothing
    length_scale::Union{Nothing,String} = nothing
    dpi::Union{Nothing,Int} = nothing

    shade::Union{Nothing,ShadeOptions} = nothing    # nothing => plain color_mode 1 fill
    mesh::Bool = false                       # color_mode 6/7 (-Qm) instead of -Qi<dpi>
    mesh_contour_pen::Union{Nothing,String} = nothing   # color_mode 7's grdview -W (mbm_grd3dplot's -MVW)
    color_palette::Int = 1
    color_flip::Bool = false
    data_scale::Union{Nothing,Float64} = nothing
    cpt_lines::Union{Nothing,Vector{CptLine}} = nothing
    cpt_file::Union{Nothing,String} = nothing

    drape_file::Union{Nothing,String} = nothing         # grdview -G (mbm_grd3dplot's -N)
    mesh_null::Union{Nothing,String} = nothing          # grdview -N (mbm_grd3dplot's -MVN)
    mesh_zlevel::Union{Nothing,String} = nothing        # grdview/basemap -Z (mbm_grd3dplot's -MVZ)

    coast::Union{Nothing,CoastOptions} = nothing
    text_labels::Vector{String} = String[]

    gmt_defs::Vector{String} = String[]
end


"""Port of the psscale gate at mbm_grd3dplot line ~2093:
(\$color_mode && \$color_mode < 7 && \$color_palette < 5). NOTE this
includes color_mode 6 (plain mesh) even though grdview itself never
gets a -C on that mode (mbm_grd3dplot line ~1989: "if (color_mode !=
6) -C..."). Reproduced as-is: the CPT is still built and written
for mode 6, so showing a scale bar for it, while not obviously
useful, is exactly what the real macro does."""
_show_colorscale_3d(opts::ScriptOptions3D)::Bool = opts.color_palette < 5


"""Python's `str.partition(sep)`: (head, sep, tail)."""
function _partition(s::AbstractString, sep::AbstractString)
    r = findfirst(sep, s)
    r === nothing && return (String(s), "", "")
    return (String(s[1:prevind(s, first(r))]), String(sep), String(s[nextind(s, last(r)):end]))
end


"""Build the full GMT modern-mode shellscript text for `opts`.
Does not touch the filesystem (except that the caller is expected
to have already written opts.cpt_lines via write_cpt_file(), or to
do so before running the returned script) -- see generate_and_write_3d()."""
function generate_script_3d(opts::ScriptOptions3D)::String
    lay = opts.layout
    j = "-J$(lay.projection)$(lay.projection_pars) -Jz$(fmtg(lay.map_zscale, 5))"
    p = "-p$(fmtg(lay.view_azimuth, 5))/$(fmtg(lay.view_elevation, 5))"
    r = "-R$(lay.region)/$(fmtg(lay.zmin, 10))/$(fmtg(lay.zmax, 10))"
    cptfile = _nonempty(opts.cpt_file) ? opts.cpt_file : "$(opts.root).cpt"

    lines = String[
        "#!/usr/bin/env bash",
        "#",
        "# GMT modern-mode 3D plotting script for $(opts.grid)",
        "# Generated by mbjl_grd3dplot_script.jl (Julia port of mbm_grd3dplot)",
        "#",
        "set -e",
        "",
        "gmt begin $(opts.root) $(opts.output_format)",
        "",
    ]

    if _truthy(opts.dpi)
        push!(lines, "gmt figure $(opts.root) $(opts.output_format) E$(opts.dpi)")
        push!(lines, "")
    end

    push!(lines, "# Set temporary GMT defaults")
    baseline_pairs = String[]
    for gmt_def in baseline_gmt_defaults(lay.pagesize, opts.color_flip, lay.degree_format)
        param, _, value = _partition(gmt_def, " ")
        push!(baseline_pairs, "$param $value")
    end
    push!(lines, "gmt set $(join(baseline_pairs, " "))")
    push!(lines, "")

    if !isempty(opts.gmt_defs)
        push!(lines, "# User-defined GMT parameter overrides")
        user_pairs = String[]
        for gmt_def in opts.gmt_defs
            param, _, value = _partition(gmt_def, "/")
            push!(user_pairs, "$param $value")
        end
        push!(lines, "gmt set $(join(user_pairs, " "))")
        push!(lines, "")
    end

    file_use = opts.grid
    if opts.data_scale !== nothing
        file_use = "$(opts.root)_scaled.grd"
        push!(lines, "# Rescale data")
        push!(lines, "gmt grdmath $(opts.grid) $(fmtg(opts.data_scale, 6)) MUL = $file_use")
        push!(lines, "")
    end

    temp_file_cleanup = nothing
    grid_for_view = file_use
    intensity_file = nothing

    shade = opts.shade !== nothing ? resolve_shade_defaults(opts.shade) : nothing
    geographic = opts.grid_info.gridprojected == 0

    if shade === nothing || shade.color_mode == 1 || opts.mesh
        # nothing to precompute
    elseif shade.color_mode == 2
        cmds, intensity_file = _illumination_commands(
            file_use, opts.root, shade.magnitude, shade.azimuth, shade.elevation, geographic
        )
        push!(lines, "# Get shading array")
        append!(lines, cmds)
        push!(lines, "")
    elseif shade.color_mode == 3
        if !_nonempty(shade.file_intensity)
            throw(ArgumentError("color_mode 3 requires shade.file_intensity"))
        end
        cmds, intensity_file = _intensity_commands_mode3(
            shade.file_intensity, opts.root, shade.stretch_shade, shade.magnitude
        )
        if !isempty(cmds)
            push!(lines, "# Get shading array")
            append!(lines, cmds)
            push!(lines, "")
        end
    elseif shade.color_mode == 4
        cmds, grid_for_view = _slope_commands(file_use, opts.root, geographic, nothing)
        temp_file_cleanup = grid_for_view
        push!(lines, "# Get slope array")
        append!(lines, cmds)
        push!(lines, "")
    elseif shade.color_mode == 5
        cmds, intensity_file = _slope_commands(file_use, opts.root, geographic, shade.magnitude)
        temp_file_cleanup = intensity_file
        push!(lines, "# Get slope array")
        append!(lines, cmds)
        push!(lines, "")
    end

    gv = ["gmt grdview $grid_for_view $j $p $r"]
    if !opts.mesh
        push!(gv, "-C$cptfile")
    end
    if _nonempty(opts.mesh_null)
        push!(gv, "-N$(opts.mesh_null)")
    end
    if _nonempty(opts.mesh_zlevel)
        push!(gv, "-Z$(opts.mesh_zlevel)")
    end
    if shade !== nothing && shade.color_mode in (2, 3)
        push!(gv, "-I$(pystr(intensity_file))")
    elseif shade !== nothing && shade.color_mode == 5
        push!(gv, "-I$(pystr(intensity_file))")
    end
    if _nonempty(opts.drape_file)
        push!(gv, "-G$(opts.drape_file)")
    end
    if opts.mesh
        push!(gv, "-Qm")
        if _nonempty(opts.mesh_contour_pen)
            push!(gv, "-W$(opts.mesh_contour_pen)")
        end
    else
        push!(gv, _truthy(opts.dpi) ? "-Qi$(opts.dpi)" : "-Qi")
    end
    push!(lines, join(gv, " "))
    push!(lines, "")

    if opts.coast !== nothing
        co = opts.coast
        cparts = ["gmt coast $j $p $r"]
        if _nonempty(co.coast_resolution)
            push!(cparts, "-D$(co.coast_resolution)")
        end
        if _nonempty(co.coast_dryfill)
            push!(cparts, "-G$(co.coast_dryfill)")
        end
        if _nonempty(co.coast_wetfill)
            push!(cparts, "-S$(co.coast_wetfill)")
        end
        if _nonempty(co.coast_lakefill)
            push!(cparts, "-C$(co.coast_lakefill)")
        end
        if _nonempty(co.coast_river)
            push!(cparts, "-I$(co.coast_river)")
        end
        for boundary in co.coast_boundaries
            push!(cparts, "-N$boundary")
        end
        if _nonempty(co.coast_pen)
            push!(cparts, "-W$(co.coast_pen)")
        end
        push!(lines, join(cparts, " "))
        push!(lines, "")
    end

    if _show_colorscale_3d(opts)
        label = opts.colorbar_label !== nothing ? opts.colorbar_label : "z"
        if lay.colorscale_vh == "+v"
            d_xy = "$(fmtg(lay.colorscale_offx, 5))/0"
        else
            d_xy = "0/$(fmtg(lay.colorscale_offy, 5))"
        end
        push!(lines,
            "gmt colorbar -C$cptfile " *
            "-Dx$d_xy" *
            "$(lay.colorscale_vh)+w$(fmtg(lay.colorscale_length, 5))/$(fmtg(lay.colorscale_thick, 5)) " *
            "-B+l\"$label\""
        )
        push!(lines, "")
    end

    if !isempty(opts.text_labels)
        push!(lines, "gmt text $j $p $r -F+f+a+j <<EOF")
        for raw in opts.text_labels
            # Python's raw.split("/", 6): at most 7 fields
            tx, ty, tsize, tangle, font, just, txt = split(raw, "/"; limit = 7)
            push!(lines, "$tx $ty $tsize,$font $tangle $just $txt")
        end
        push!(lines, "EOF")
        push!(lines, "")
    end

    basemap_parts = ["gmt basemap $j $p $r $(_basemap_axes_3d(lay, opts.tick_info, opts.title))"]
    if _nonempty(opts.mesh_zlevel)
        push!(basemap_parts, "-Z$(opts.mesh_zlevel)")
    end
    if _nonempty(opts.length_scale)
        push!(basemap_parts, "-L$(opts.length_scale)")
    end
    push!(lines, join(basemap_parts, " "))
    push!(lines, "")

    if _nonempty(temp_file_cleanup)
        push!(lines, "# Delete surplus files")
        push!(lines, "rm -f $temp_file_cleanup")
        push!(lines, "")
    end

    push!(lines, "gmt end")
    push!(lines, "")
    return join(lines, "\n")
end


"""Write both the CPT file (if opts.cpt_lines is set) and the
generated shellscript to disk, and make the script executable."""
function generate_and_write_3d(opts::ScriptOptions3D, script_path::AbstractString)
    if opts.cpt_lines !== nothing && opts.cpt_file === nothing
        write_cpt_file("$(opts.root).cpt", opts.cpt_lines)
    end
    write(script_path, generate_script_3d(opts))
    chmod(script_path, 0o755)
    return nothing
end

end # module MbjlGrd3dplotScript
