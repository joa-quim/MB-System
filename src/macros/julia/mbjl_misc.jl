#--------------------------------------------------------------------
#    The MB-system:  mbjl_misc.jl
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
#   Julia port of src/macros/python/mbpy_misc.py, itself a port of
#   mbm_grdplot's -M<misc> sub-option grammar (mbm_grdplot
#   lines ~384-759) -- coastline (pscoast), xy overlay (psxy), contour
#   (grdcontour) tuning, image rendering, and swath-navigation/ping-tick
#   annotation controls, plus a handful of general options (GMT default
#   overrides, color-scale placement, a length/map scale bar, a custom
#   map origin, data rescaling, text labels, and a Unix time stamp).
#
#   -M itself packs ~28 unrelated sub-flags behind a single letter and
#   a ":"-separated mini-language (e.g. "-MGFl:MTDf:MXIfile.xy"); this
#   module models the *information* each sub-flag carries as plain
#   structs (MiscOptions and its per-category members below),
#   independent of how it is spelled on a command line. parse_misc()
#   reads the legacy "-M" string form (for validating against real
#   mbm_grdplot output, and as a migration aid for old scripts); the
#   long-option Julia CLI this is meant to serve should populate the
#   same structs directly from descriptive flags (--coast-resolution,
#   --xy-file, --ping-number-ticks, ...) rather than re-exposing "-M".
#
# Status:
#   All sub-options read and modeled below. Cross-checked against real
#   mbm_grdplot .cmd output for a representative sample covering every
#   category (general, contour, image, navigation, coast, xy) -- see
#   test_mbjl_misc.jl -- rather than exhaustively for all ~28
#   sub-flags; the remaining ones are straightforward analogues of the
#   validated ones (same regex-extract-and-store shape) but have not
#   individually been run against Perl.
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))

module MbjlMisc

using ..MbjlUtil

export GeneralOptions, ContourOptions, ImageOptions, NavigationOptions,
       resolve_navigation_control, resolve_swath_format, CoastOptions,
       XYOverlayEntry, XYOptions, MiscOptions, apply_misc_command!, parse_misc

const OptStr = Union{Nothing,String}
const OptNum = Union{Nothing,Float64,Int}


Base.@kwdef mutable struct GeneralOptions
    gmt_defs::Vector{String} = String[]          # -MGD, repeatable
    scale_loc::OptStr = nothing                  # -MGF
    length_scale::OptStr = nothing               # -MGL
    length_scale_frame::Bool = false             # -MGLF
    xorigin::Union{Nothing,Float64} = nothing    # -MGO (first field)
    yorigin::Union{Nothing,Float64} = nothing    # -MGO (second field)
    originset::Bool = false
    dpi::OptStr = nothing                        # -MGQ
    data_scale::OptStr = nothing                 # -MGS
    text_labels::Vector{String} = String[]       # -MGT, repeatable
    unix_stamp::OptStr = nothing                 # -MGU<label>
    unix_stamp_on::Bool = false                  # -MGU
end


Base.@kwdef mutable struct ContourOptions
    contour_anot_int::OptStr = nothing    # -MCA
    contour_gap::OptStr = nothing         # -MCG
    contour_cut::OptStr = nothing         # -MCQ
    contour_tick_on::Bool = false         # -MCT
    contour_tick::OptStr = nothing        # -MCT<val>
    contour_pen::OptStr = nothing         # -MCW
end


Base.@kwdef mutable struct ImageOptions
    image_render::Bool = false
    image_resolution::OptStr = nothing    # -MIE
    image_type::OptStr = nothing          # -MIT
end


Base.@kwdef mutable struct NavigationOptions
    name_mode::Bool = false               # -MNA...
    name_perp::Bool = false
    nav_name_hgt::Union{Nothing,Float64} = nothing
    swathformat::OptStr = nothing                 # -MNF
    swathnavdatalist::OptStr = nothing             # -MNI
    navigation_mode::Bool = false                  # -MNN
    navigation_control::OptStr = nothing           # -MNN<val>
    pingnumber_mode::Bool = false                  # -MNP
    pingnumber_control::OptStr = nothing
    pingnumber_tick::OptNum = nothing
    pingnumber_annot::OptNum = nothing
    pingnumber_tick_len::OptNum = nothing
    nav_pen::OptStr = nothing                      # -MNW
end


_nonempty(s) = s !== nothing && !isempty(s)


"""Port of the navigation_control/pingnumber_control normalization at
mbm_grdplot lines ~1953-2037: rebuilds `navigation_control` into the
final "tick/annot/dateannot/ticksize" form the `-D` flag of `gmt
mbcontour` expects (see mbjl_grdplot_script.jl), and `pingnumber_control`
into "tick/annot/ticklen" for `-M`. Returns an updated copy of `nav`.

NOTE: mbm_grdplot's own first check here (already->=4-field input)
uses a regex that is also satisfied by 5- and 6-field input (any
string with 3 or more "/" characters matches it), which makes the
"elsif" branches further down that specifically look for 5- or
6-field forms (to pull a trailing name-height/perpendicular-flag
pair out of `-MNN`) unreachable dead code upstream -- any input
with that many fields is caught by the first check and passed
through unparsed instead. Reproduced by only implementing the
reachable (1-3 field) forms below rather than porting dead code.
"""
function resolve_navigation_control(nav::NavigationOptions, has_swathnavdatalist::Bool)::NavigationOptions
    navigation_mode = nav.navigation_mode
    navigation_control = nav.navigation_control
    name_mode = nav.name_mode
    name_perp = nav.name_perp
    nav_name_hgt = nav.nav_name_hgt

    if has_swathnavdatalist && !_nonempty(navigation_control) && !navigation_mode
        navigation_mode = true
        navigation_control = "100000/100000/100000/0.15"
    end

    if _nonempty(navigation_control) && count(==('/'), navigation_control) >= 3
        navigation_mode = true
    elseif _nonempty(navigation_control) && (occursin("FP", navigation_control) || occursin("fp", navigation_control))
        navigation_mode = true
        name_mode = true
        name_perp = true
        navigation_control = "0.25/1/4/0.15"
        nav_name_hgt = 0.15
    elseif _nonempty(navigation_control) && (occursin("F", navigation_control) || occursin("f", navigation_control))
        navigation_mode = true
        name_mode = true
        name_perp = false
        navigation_control = "0.25/1/4/0.15"
        nav_name_hgt = 0.15
    elseif _nonempty(navigation_control)
        parts = split(navigation_control, "/")
        if length(parts) == 3
            nav_time_tick, nav_time_annot, nav_date_annot = parts
            nav_tick_size = "0.15"
        elseif length(parts) == 2
            nav_time_tick, nav_time_annot = parts
            nav_date_annot = "100000"
            nav_tick_size = "0.15"
        else
            nav_time_tick = navigation_control
            nav_time_annot = "100000"
            nav_date_annot = "100000"
            nav_tick_size = "0.15"
        end
        navigation_mode = true
        navigation_control = "$nav_time_tick/$nav_time_annot/$nav_date_annot/$nav_tick_size"
    elseif navigation_mode
        navigation_control = "0.25/1/4/0.15"
    end

    pingnumber_control = nav.pingnumber_control
    if nav.pingnumber_mode
        pingnumber_control = "$(pystr(nav.pingnumber_tick))/$(pystr(nav.pingnumber_annot))/$(pystr(nav.pingnumber_tick_len))"
    end

    return pyreplace(
        nav;
        navigation_mode = navigation_mode,
        navigation_control = navigation_control,
        name_mode = name_mode,
        name_perp = name_perp,
        nav_name_hgt = nav_name_hgt,
        pingnumber_control = pingnumber_control,
    )
end


"""Port of the swathformat auto-detection at mbm_grdplot lines
~2903-2911: if not explicitly given, ask `mbformat -I <datalist> -L`;
if it reports 0 (unrecognized), fall back to -1 (MB-System's own
datalist format code).
"""
function resolve_swath_format(swathnavdatalist::AbstractString, swathformat::Union{Nothing,AbstractString} = nothing)::String
    if _nonempty(swathformat)
        return String(swathformat)
    end
    out = IOBuffer()
    err = IOBuffer()
    proc = run(pipeline(ignorestatus(`mbformat -I $swathnavdatalist -L`); stdout = out, stderr = err))
    success(proc) || error("mbformat failed with exit status $(proc.exitcode):\n" * String(take!(err)))
    detected = String(split(String(take!(out)))[1])
    if detected == "0"
        detected = "-1"
    end
    return detected
end


Base.@kwdef mutable struct CoastOptions
    coast_control::Bool = false
    coast_lakefill::OptStr = nothing      # -MTC
    coast_resolution::OptStr = nothing    # -MTD
    coast_dryfill::OptStr = nothing       # -MTG
    coast_river::OptStr = nothing         # -MTI
    coast_boundaries::Vector{String} = String[]  # -MTN, repeatable
    coast_wetfill::OptStr = nothing       # -MTS
    coast_pen::OptStr = nothing           # -MTW
end


Base.@kwdef mutable struct XYOverlayEntry
    file::String
    symbol::String
    fill::String
    segment::String
    segchar::String
    pen::String
end


Base.@kwdef mutable struct XYOptions
    entries::Vector{XYOverlayEntry} = XYOverlayEntry[]
    # "pending" values as last set by -MXG/-MXS/-MXW/-MXM; once
    # defaulted at a -MXI (no explicit value yet given), the default
    # itself becomes the new pending value, exactly as mbm_grdplot's
    # persistent globals behave -- see apply_misc_command!()'s -MXI
    # handling below.
    _pending_fill::OptStr = nothing
    _pending_symbol::OptStr = nothing
    _pending_segment::OptStr = nothing
    _pending_segchar::OptStr = nothing
    _pending_pen::OptStr = nothing
end


Base.@kwdef mutable struct MiscOptions
    general::GeneralOptions = GeneralOptions()
    contour::ContourOptions = ContourOptions()
    image::ImageOptions = ImageOptions()
    navigation::NavigationOptions = NavigationOptions()
    coast::CoastOptions = CoastOptions()
    xy::XYOptions = XYOptions()
end


_cap(m, i) = String(m.captures[i])


"""Apply one ":"-separated -M sub-command to `opts` in place.
Port of the per-\$cmd dispatch inside mbm_grdplot's main -M loop
(lines ~389-758); branch order and regexes mirror the Perl source.
"""
function apply_misc_command!(cmd::AbstractString, opts::MiscOptions)
    g, c, im, nav, coast, xy = (
        opts.general, opts.contour, opts.image, opts.navigation, opts.coast, opts.xy
    )

    # --- general options ---
    m = match(r"^[Gg][Dd](\S+)", cmd)
    if m !== nothing
        push!(g.gmt_defs, _cap(m, 1))
    end

    m = match(r"^[Gg][Ff](\S+)", cmd)
    if m !== nothing
        g.scale_loc = _cap(m, 1)
    end

    if occursin(r"^[Gg][Ll][Ff]", cmd)
        g.length_scale_frame = true
    else
        m = match(r"^[Gg][Ll](\S+)", cmd)
        if m !== nothing
            g.length_scale = _cap(m, 1)
        end
    end

    m = match(r"^[Gg][Oo](\S+)/(\S+)", cmd)
    if m !== nothing
        g.xorigin, g.yorigin = pyfloat(m.captures[1]), pyfloat(m.captures[2])
        g.originset = true
    end

    m = match(r"^[GG][Qq](.+)", cmd)
    if m !== nothing
        g.dpi = _cap(m, 1)
    end

    m = match(r"^[Gg][Ss](\S+)", cmd)
    if m !== nothing
        g.data_scale = _cap(m, 1)
    end

    m = match(r"^[Gg][Tt](\S+)/(\S+)/(\S+)/(\S+)/(\S+)/(\S+)/(.+)", cmd)
    if m !== nothing && !isempty(m.captures[7])
        raw = _cap(match(r"^[Gg][Tt](.*)", cmd), 1)
        push!(g.text_labels, raw)
    elseif occursin(r"^[Gg][Tt].", cmd)
        println("\nInvalid text label ignored: $cmd")
    end

    m = match(r"^[Gg][Uu](\S+)", cmd)
    if m !== nothing
        g.unix_stamp = _cap(m, 1)
        g.unix_stamp_on = true
    elseif occursin(r"^[Gg][Uu]", cmd)
        g.unix_stamp_on = true
    end

    # --- grdcontour options ---
    m = match(r"^[Cc][Aa](\S+)", cmd)
    if m !== nothing
        c.contour_anot_int = _cap(m, 1)
    end

    m = match(r"^[Cc][Gg](\S+)", cmd)
    if m !== nothing
        c.contour_gap = _cap(m, 1)
    end

    m = match(r"^[Cc][Qq](\S+)", cmd)
    if m !== nothing
        c.contour_cut = _cap(m, 1)
    end

    if occursin(r"^[Cc][Tt](\S)", cmd)
        c.contour_tick = _cap(match(r"^[Cc][Tt](\S+)", cmd), 1)
        c.contour_tick_on = true
    elseif occursin(r"^[Cc][Tt]", cmd)
        c.contour_tick_on = true
    end

    m = match(r"^[Cc][Ww](\S+)", cmd)
    if m !== nothing
        c.contour_pen = _cap(m, 1)
    end

    # --- image rendering options ---
    m = match(r"^[Ii][Ee](\S+)", cmd)
    if m !== nothing
        im.image_resolution = _cap(m, 1)
        im.image_render = true
        if !_nonempty(im.image_type)
            im.image_type = "j"
        end
    end

    m = match(r"^[Ii][Tt](\S+)", cmd)
    if m !== nothing
        im.image_type = _cap(m, 1)
        im.image_render = true
        if !_nonempty(im.image_resolution)
            im.image_resolution = "300"
        end
    end

    # --- swath navigation options ---
    m = match(r"^[Nn][Aa](\S+)/P", cmd)
    if m !== nothing
        nav.nav_name_hgt = pyfloat(m.captures[1])
        nav.name_mode = true
        nav.name_perp = true
    elseif occursin(r"^[Nn][Aa]P", cmd)
        nav.name_mode = true
        nav.name_perp = true
        nav.nav_name_hgt = 0.15
    else
        m = match(r"^[Nn][Aa](\S+)", cmd)
        if m !== nothing
            nav.nav_name_hgt = pyfloat(m.captures[1])
            nav.name_mode = true
            nav.name_perp = false
        elseif occursin(r"^[Nn][Aa]", cmd)
            nav.name_mode = true
            nav.name_perp = false
            nav.nav_name_hgt = 0.15
        end
    end

    m = match(r"^[Nn][Ff](\S+)", cmd)
    if m !== nothing
        nav.swathformat = _cap(m, 1)
    end

    m = match(r"^[Nn][Ii](\S+)", cmd)
    if m !== nothing
        nav.swathnavdatalist = _cap(m, 1)
    end

    m = match(r"^[Nn][Nn](\S+)", cmd)
    if m !== nothing
        nav.navigation_control = _cap(m, 1)
        nav.navigation_mode = true
    elseif occursin(r"^[Nn][Nn]", cmd)
        nav.navigation_mode = true
    end

    m = match(r"^[Nn][Pp](.+)", cmd)
    if m !== nothing
        nav.pingnumber_control = _cap(m, 1)
        nav.pingnumber_mode = true
        pc = nav.pingnumber_control
        m3 = match(r"^(\S+)/(\S+)/(\S+)", pc)
        m2 = match(r"^(\S+)/(\S+)", pc)
        m1 = match(r"^(\S+)", pc)
        if m3 !== nothing
            nav.pingnumber_tick, nav.pingnumber_annot, nav.pingnumber_tick_len = (
                pyfloat(m3.captures[1]), pyfloat(m3.captures[2]), pyfloat(m3.captures[3])
            )
        elseif m2 !== nothing
            nav.pingnumber_tick, nav.pingnumber_annot = pyfloat(m2.captures[1]), pyfloat(m2.captures[2])
            nav.pingnumber_tick_len = 0.10
        elseif m1 !== nothing
            nav.pingnumber_tick = pyfloat(m1.captures[1])
            nav.pingnumber_tick_len = 0.10
            nav.pingnumber_annot = 100
        end
    elseif occursin(r"^[Nn][Pp]", cmd)
        nav.pingnumber_mode = true
        nav.pingnumber_tick_len = 0.10
        nav.pingnumber_annot = 100
        nav.pingnumber_tick = 50
    end

    m = match(r"^[Nn][Ww](\S+)", cmd)
    if m !== nothing
        nav.nav_pen = _cap(m, 1)
    end

    # --- pscoast options ---
    m = match(r"^[Tt][Cc](.+)", cmd)
    if m !== nothing
        coast.coast_lakefill = _cap(m, 1)
        coast.coast_control = true
    end

    m = match(r"^[Tt][Dd](.+)", cmd)
    if m !== nothing
        coast.coast_resolution = _cap(m, 1)
        coast.coast_control = true
    end

    m = match(r"^[Tt][Gg](.+)", cmd)
    if m !== nothing
        coast.coast_dryfill = _cap(m, 1)
        coast.coast_control = true
    end

    m = match(r"^[Tt][Ii](.+)", cmd)
    if m !== nothing
        coast.coast_river = _cap(m, 1)
        coast.coast_control = true
    end

    m = match(r"^[Tt][Nn](.+)", cmd)
    if m !== nothing
        push!(coast.coast_boundaries, _cap(m, 1))
        coast.coast_control = true
    end

    m = match(r"^[Tt][Ss](.+)", cmd)
    if m !== nothing
        coast.coast_wetfill = _cap(m, 1)
        coast.coast_control = true
    end

    m = match(r"^[Tt][Ww](.+)", cmd)
    if m !== nothing
        coast.coast_pen = _cap(m, 1)
        coast.coast_control = true
    end

    # --- psxy options ---
    m = match(r"^[Xx][Gg](.+)", cmd)
    if m !== nothing
        xy._pending_fill = _cap(m, 1)
    end

    m = match(r"^[Xx][Ii](.+)", cmd)
    if m !== nothing
        xyfile = _cap(m, 1)
        if xy._pending_symbol === nothing
            xy._pending_symbol = "N"
        end
        if xy._pending_fill === nothing
            xy._pending_fill = "N"
        end
        if xy._pending_segment === nothing
            xy._pending_segment = "N"
        end
        if xy._pending_segchar === nothing
            xy._pending_segchar = ">"
        end
        if xy._pending_pen === nothing
            xy._pending_pen = "N"
        end
        push!(xy.entries, XYOverlayEntry(
            file = xyfile,
            symbol = xy._pending_symbol,
            fill = xy._pending_fill,
            segment = xy._pending_segment,
            segchar = xy._pending_segchar,
            pen = xy._pending_pen,
        ))
    end

    if occursin(r"^[Xx][Mm]", cmd)
        m = match(r"^[Xx][Mm](\S)", cmd)
        if m !== nothing
            xy._pending_segchar = _cap(m, 1)
        else
            xy._pending_segchar = ">"
        end
        if xy._pending_segment === nothing
            xy._pending_segment = "Y"
        elseif xy._pending_segment != "N"
            xy._pending_segment = "N"
        else
            xy._pending_segment = "Y"
        end
    end

    m = match(r"^[Xx][Ss](.+)", cmd)
    if m !== nothing
        xy._pending_symbol = _cap(m, 1)
    end

    m = match(r"^[Xx][Ww](.+)", cmd)
    if m !== nothing
        xy._pending_pen = _cap(m, 1)
    end
    return opts
end


"""Port of the legacy "-M<misc>" string form: split on ":" and apply
each piece via apply_misc_command!(). Provided for validating the
structs above against real mbm_grdplot output, and as a
migration aid for translating old command lines -- the long-option
Julia CLI itself should populate MiscOptions directly rather than
building and re-parsing this string.
"""
function parse_misc(misc::Union{Nothing,AbstractString})::MiscOptions
    opts = MiscOptions()
    if misc === nothing || isempty(misc)
        return opts
    end
    for cmd in split(misc, ":")
        apply_misc_command!(cmd, opts)
    end
    return opts
end

end # module MbjlMisc
