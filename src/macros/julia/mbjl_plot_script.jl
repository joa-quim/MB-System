#--------------------------------------------------------------------
#    The MB-system:  mbjl_plot_script.jl
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
#   Julia port of src/macros/python/mbpy_plot_script.py: data-bounds,
#   color-palette and script-generator logic for mbjl_plot, the port of
#   the Perl macro mbm_plot. mbm_plot plots swath data directly (not a
#   grid) with MB-System's GMT supplement modules: `gmt mbswath` for
#   color fill / shaded relief / amplitude / sidescan images
#   (color_mode 1-5) and `gmt mbcontour` for bathymetry contours and
#   annotated navigation. This module writes the same plot as a GMT
#   **modern mode** shellscript (`gmt begin`/`gmt end`, `gmt coast`,
#   `gmt plot`, `gmt colorbar`, `gmt text`, `gmt basemap`), the same way
#   mbjl_grdplot_script.jl does for mbm_grdplot.
#
#   Page layout and map projection fitting are shared with the other
#   mbjl_* macros (MbjlLayout.compute_layout(), fed the expanded swath
#   bounds); mbm_plot's GetBaseTick/GetPageSize/GetProjection are the
#   geographic subset of mbm_grdplot's. What differs from mbm_grdplot,
#   and so lives here:
#   - data bounds come from the swath files' .inf files or mbinfo, and
#     are padded and given a reasonable aspect ratio (mbm_plot lines
#     ~917-1290);
#   - the color interval is computed from the range of the plotted
#     quantity (depth, amplitude or sidescan), and the default palette
#     order puts low values (shallow depths) at the start of the base
#     palette, the reverse of mbm_grdplot, because depth is positive
#     down (mbm_plot lines ~1673-2200);
#   - the histogram color stretch comes from mbhistogram, run on the
#     swath data;
#   - contour and shading controls follow mbswath/mbcontour's own -A
#     and -G syntax.
#
#   Fixes relative to mbm_plot as it was before 3 November 2026 (the
#   same fixes were then made in mbm_plot itself, and are in
#   mbpy_plot_script.py too):
#   - default amplitude shading for color_mode 3 (-G3 without -A):
#     mbm_plot tested a bareword (`!stretch_shade`, always false), so it
#     always used magnitude 1 / zero level 191 even without -S../1,
#     which shades the whole swath black for dB amplitudes. Here the
#     amplitude-range-derived default is used unless the shade is
#     stretched, as mbm_plot intended;
#   - -Amagnitude without "/azimuth" left magnitude unset in mbm_plot;
#     here the other field keeps its default;
#   - -D../1 (shade flip) for color_mode 2 was undone when no -A was
#     given; here it always turns the illumination azimuth by 180 deg;
#   - a six-field -C contour control kept its label height;
#   - "center" in -MGL is replaced by the map center lon/lat;
#   - the -MGQ resolution is passed to mbswath as -E<dpi> (not -Q);
#   - the reversed discrete palette no longer drops its first segment;
#   - the stretched-shade CPT always has the same number of entries as
#     the grayscale palette, whatever -W ncolors is.
#
isdefined(@__MODULE__, :MbjlUtil) || include(joinpath(@__DIR__, "mbjl_util.jl"))
isdefined(@__MODULE__, :MbjlLayout) || include(joinpath(@__DIR__, "mbjl_layout.jl"))
isdefined(@__MODULE__, :MbjlColor) || include(joinpath(@__DIR__, "mbjl_color.jl"))
isdefined(@__MODULE__, :MbjlMisc) || include(joinpath(@__DIR__, "mbjl_misc.jl"))
isdefined(@__MODULE__, :MbjlGrdplotScript) || include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))

module MbjlPlotScript

using ..MbjlUtil
import ..MbjlColor
const color = MbjlColor
using ..MbjlColor: CptLine, RGB
using ..MbjlLayout: Layout
using ..MbjlMisc: CoastOptions, NavigationOptions, XYOverlayEntry
using ..MbjlGrdplotScript: baseline_gmt_defaults, write_cpt_file
import ..MbjlGrdplotScript

export NCPT, COLOR_MODE_NAMES, DEFAULT_SCALE_LABELS, SwathBounds, parse_mbinfo,
       list_swath_files, apply_lonflip, get_swath_bounds, expand_bounds, nice_interval,
       compute_color_interval_plot, run_mbhistogram, build_cpt_plot, build_shade_cpt,
       resolve_contour_control, ShadeControl, as_arg, resolve_shade_control,
       SwathOptions, ScriptOptionsPlot, cpt_path, shade_cpt_path, script_path,
       generate_script_plot, generate_and_write_plot

const NCPT = color.NCPT

const COLOR_MODE_NAMES = Dict(
    1 => "Bathymetry Color Fill",
    2 => "Bathymetry Color Shaded Relief",
    3 => "Bathymetry Color Shaded by Amplitude",
    4 => "Amplitude Fill",
    5 => "Sidescan Fill",
)

# mbm_plot's default color scale labels per color_mode
const DEFAULT_SCALE_LABELS = Dict(
    1 => "Depth (meters)", 2 => "Depth (meters)", 3 => "Depth (meters)",
    4 => "Beam Amplitude Values", 5 => "Sidescan Pixel Values",
)

const OptF = Union{Nothing,Float64}
const OptS = Union{Nothing,String}

_given(x) = x !== nothing && !(x isa AbstractString && isempty(x))

"""Run a command, capture stdout as text; throw on a non-zero exit
status when `check` (Python's subprocess.run(check=True)).

stdout is read straight from the process (open + read), not through
run(pipeline(...; stdout = IOBuffer())): on Windows, the first such
IOBuffer-captured run of an MB-System program (mbinfo, mbformat, ...)
in a fresh Julia process came back with empty output."""
function _run_capture(cmd::Cmd; check::Bool = true)::String
    err = IOBuffer()
    proc = open(pipeline(ignorestatus(cmd); stderr = err), "r")
    out = read(proc, String)
    wait(proc)
    if check && !success(proc)
        error("command $(cmd) failed with exit status $(proc.exitcode):\n" * String(take!(err)))
    end
    return out
end


# ---------------------------------------------------------------------------
# Data bounds
# ---------------------------------------------------------------------------

Base.@kwdef mutable struct SwathBounds
    xmin::Float64
    xmax::Float64
    ymin::Float64
    ymax::Float64
    zmin::OptF = nothing    # depth
    zmax::OptF = nothing
    amin::OptF = nothing    # beam amplitude
    amax::OptF = nothing
    smin::OptF = nothing    # sidescan
    smax::OptF = nothing
    nrec::Int = 0
end


const _INFO_PATTERNS = [
    "nrec" => r"^Number of Records:\s+(\S+)",
    "lon" => r"^Minimum Longitude:\s+(\S+)\s+Maximum Longitude:\s+(\S+)",
    "lat" => r"^Minimum Latitude:\s+(\S+)\s+Maximum Latitude:\s+(\S+)",
    "depth" => r"^Minimum Depth:\s+(\S+)\s+Maximum Depth:\s+(\S+)",
    "amp" => r"^Minimum Amplitude:\s+(\S+)\s+Maximum Amplitude:\s+(\S+)",
    "ss" => r"^Minimum Sidescan:\s+(\S+)\s+Maximum Sidescan:\s+(\S+)",
]


"""Parse mbinfo (or .inf file) text; nothing if it has no lon/lat bounds."""
function parse_mbinfo(text::AbstractString)::Union{Nothing,SwathBounds}
    vals = Dict{String,Vector{Float64}}()
    for line in split(text, r"\r\n|\n|\r")
        line = isvalid(line) ? line : String(map(c -> isvalid(c) ? c : '�', collect(line)))
        for (key, pat) in _INFO_PATTERNS
            m = match(pat, line)
            if m !== nothing
                vals[key] = [pyfloat(g) for g in m.captures]
            end
        end
    end
    if !haskey(vals, "lon") || !haskey(vals, "lat")
        return nothing
    end
    b = SwathBounds(xmin = vals["lon"][1], xmax = vals["lon"][2], ymin = vals["lat"][1], ymax = vals["lat"][2],
                    nrec = pytrunc(get(vals, "nrec", [0.0])[1]))
    if haskey(vals, "depth")
        b.zmin, b.zmax = vals["depth"]
    end
    if haskey(vals, "amp")
        b.amin, b.amax = vals["amp"]
    end
    if haskey(vals, "ss")
        b.smin, b.smax = vals["ss"]
    end
    return b
end


"""The (file, format) pairs to plot: the file itself, or the
datalist's entries as reported by mbdatalist (mbm_plot ~925-947)."""
function list_swath_files(file_data::AbstractString, fmt::Integer, region::OptS = nothing)::Vector{Tuple{String,Int}}
    if fmt >= 0
        return [(String(file_data), Int(fmt))]
    end
    cmd = ["mbdatalist", "-F-1", "-I", file_data]
    if _given(region)
        push!(cmd, "-R$region")
    end
    out = _run_capture(Cmd(cmd))
    files = Tuple{String,Int}[]
    for line in split(out, r"\r\n|\n|\r")
        parts = split(line)
        if length(parts) >= 2
            push!(files, (String(parts[1]), pyint(parts[2])))
        end
    end
    return files
end


"""Port of mbm_plot's per-file longitude reset (lines ~1071-1112)."""
function apply_lonflip(xmin::Real, xmax::Real, lonflip::Integer)::Tuple{Float64,Float64}
    xmin, xmax = Float64(xmin), Float64(xmax)
    if lonflip == 0 && ((xmin < -180.0 < xmax) || (xmin < 180.0 < xmax))
        return -180.0, 180.0
    end
    if lonflip == 0 && xmin < -180.0
        return xmin + 360.0, xmax + 360.0
    end
    if lonflip == 0 && xmax > 180.0
        return xmin - 360.0, xmax - 360.0
    end
    if lonflip == -1 && xmin < 0.0 < xmax
        return -360.0, 0.0
    end
    if lonflip == -1 && xmax > 0.0
        return xmin - 360.0, xmax - 360.0
    end
    if lonflip == 1 && xmin < 0.0 < xmax
        return 0.0, 360.0
    end
    if lonflip == 1 && xmin < 0.0
        return xmin + 360.0, xmax + 360.0
    end
    return xmin, xmax
end


_combine(a, b, fn) = a === nothing ? b : (b === nothing ? a : fn(a, b))


"""Port of mbm_plot's data-bounds loop (lines ~948-1135): use each
file's .inf file when it is readable, no time window is applied and
(with a region) the file does not straddle the region while a color
image is wanted; otherwise run `mbinfo -G` on the file. Files with
no records are skipped."""
function get_swath_bounds(
    files::AbstractVector;
    region_bounds = nothing,
    region::OptS = nothing,
    btime::OptS = nothing,
    etime::OptS = nothing,
    lonflip::Integer = 0,
    color_mode::Integer = 0,
    verbose::Bool = false,
)::SwathBounds
    total = nothing
    for (path, fmt) in files
        info = nothing
        inf = "$(path).inf"
        if isfile(inf) && !_given(btime) && !_given(etime)
            b = parse_mbinfo(read(inf, String))
            if b !== nothing
                use = true
                if region_bounds !== nothing && b.nrec > 0 && color_mode != 0
                    xmin, xmax, ymin, ymax = region_bounds
                    inside = (xmin <= b.xmin <= xmax && xmin <= b.xmax <= xmax &&
                              ymin <= b.ymin <= ymax && ymin <= b.ymax <= ymax)
                    outside = b.xmax < xmin || b.xmin > xmax || b.ymax < ymin || b.ymin > ymax
                    use = inside || outside
                end
                if use
                    info = b
                    if verbose
                        println("Reading mbinfo output from file $(inf)...")
                    end
                end
            end
        end
        if info === nothing
            cmd = ["mbinfo", "-F$fmt", "-I", path, "-G"]
            _given(btime) && push!(cmd, "-B$btime")
            _given(etime) && push!(cmd, "-E$etime")
            _given(region) && push!(cmd, "-R$region")
            if verbose
                println("Running " * join(shquote.(cmd), " "))
            end
            out = _run_capture(Cmd(cmd); check = false)
            info = parse_mbinfo(out)
        end
        if info === nothing || info.nrec <= 0
            continue
        end
        info.xmin, info.xmax = apply_lonflip(info.xmin, info.xmax, lonflip)
        if total === nothing
            total = info
        else
            total = SwathBounds(
                xmin = min(total.xmin, info.xmin), xmax = max(total.xmax, info.xmax),
                ymin = min(total.ymin, info.ymin), ymax = max(total.ymax, info.ymax),
                zmin = _combine(total.zmin, info.zmin, min), zmax = _combine(total.zmax, info.zmax, max),
                amin = _combine(total.amin, info.amin, min), amax = _combine(total.amax, info.amax, max),
                smin = _combine(total.smin, info.smin, min), smax = _combine(total.smax, info.smax, max),
                nrec = total.nrec + info.nrec,
            )
        end
    end
    if total === nothing
        error("does not appear to be any data in the input")
    end
    return total
end


"""Port of mbm_plot lines ~1220-1290: pad the data bounds by 5% and
widen the shorter side (in approximate meters) so the map's aspect
ratio is at most 2:1; latitudes are kept inside +/-89 degrees."""
function expand_bounds(xmin::Real, xmax::Real, ymin::Real, ymax::Real)::NTuple{4,Float64}
    xmin, xmax, ymin, ymax = Float64(xmin), Float64(xmax), Float64(ymin), Float64(ymax)
    delx = 0.05 * (xmax - xmin)
    dely = 0.05 * (ymax - ymin)
    if (xmax - xmin) < 0.95 * 360.0
        xmin, xmax = xmin - delx, xmax + delx
    end
    ymin, ymax = max(ymin - dely, -89.0), min(ymax + dely, 89.0)

    c1, c2, c3 = 111412.84, -93.5, 0.118
    c4, c5, c6, c7 = 111132.92, -559.82, 1.175, 0.0023
    radlat = 0.5 * (ymax + ymin) * pi / 180.0
    mtodeglat = 1.0 / abs(c4 + c5 * cos(2 * radlat) + c6 * cos(4 * radlat) + c7 * cos(6 * radlat))
    mtodeglon = 1.0 / abs(c1 * cos(radlat) + c2 * cos(3 * radlat) + c3 * cos(5 * radlat))
    dx = (xmax - xmin) / mtodeglon
    dy = (ymax - ymin) / mtodeglat
    if dy / dx > 2.0
        delx = (0.5 * (0.5 * dy - dx)) * mtodeglon
        xmin, xmax = xmin - delx, xmax + delx
    elseif dx / dy > 2.0
        dely = (0.5 * (0.5 * dx - dy)) * mtodeglat
        ymin, ymax = max(ymin - dely, -89.0), min(ymax + dely, 89.0)
    end
    return xmin, xmax, ymin, ymax
end


# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------

"""The 1/2.5/5 x 10^n "reasonable interval" heuristic shared by
mbm_plot's contour and color intervals."""
function nice_interval(dd::Real)::Float64
    if dd <= 0
        return 0.0
    end
    base = pytrunc((log(dd) / log(10.0)) + 0.5)
    # exp10, not 10.0^x (see MbjlLayout.get_base_tick())
    interval = exp10(base) / 10.0
    if dd / interval < 10
        interval = interval / 4
    elseif dd / interval < 20
        interval = interval / 2
    end
    return interval
end


"""Port of mbm_plot lines ~1696-1720: returns (color_start,
color_int). Note that, as in mbm_plot, the "does the last color
reach the top" test compares against the depth maximum `zmax` even
when the colored quantity is amplitude or sidescan."""
function compute_color_interval_plot(
    dmin::Real, dmax::Real, zmax::Real, ncolors_use::Integer, no_nice::Bool = false,
)::Tuple{Float64,Float64}
    dd = dmax - dmin
    if !no_nice && dd > 0
        start_int = nice_interval(dd) / 2
        multiplier = pytrunc(dd / (ncolors_use - 1) / start_int) + 1
        color_int = multiplier * start_int
        if color_int * pytrunc(dmin / color_int) + color_int * (ncolors_use - 1) < zmax
            multiplier += 1
            color_int = multiplier * start_int
        end
        return color_int * pytrunc(dmin / color_int), color_int
    end
    color_int = 1.02 * dd / (ncolors_use - 1)
    return dmin - 0.01 * dd, color_int
end


"""Run mbhistogram for an equalized color (or Gaussian shade) stretch;
returns its interval boundaries (nintervals + 1 values, or
nintervals with -G). data_type: 0 bathymetry, 1 amplitude, 2 sidescan."""
function run_mbhistogram(file_data::AbstractString, fmt::Integer, data_type::Integer, dmin::Real, dmax::Real,
                         nintervals::Integer; gaussian::Bool = false)::Vector{Float64}
    cmd = ["mbhistogram", "-F$fmt", "-I", file_data, "-A$data_type",
           "-D$(fmtg(dmin, 10))/$(fmtg(dmax, 10))", "-M$nintervals", "-N1000"]
    if gaussian
        push!(cmd, "-G")
    end
    out = _run_capture(Cmd(cmd))
    return [pyfloat(v) for v in split(out)]
end


"""Port of mbm_plot's four CPT-writing loops (lines ~2085-2200).
`colors` holds ncolors samples of the base palette. Continuous style
(1) makes ncolors - 1 graded segments, discrete style ncolors flat
ones. Unflipped, low z gets colors[1] (the start of the palette,
e.g. white/red for Haxby: shallow depths); flipped reverses that.
z boundaries step by color_int from color_start, or are consumed in
order from `boundaries` (the mbhistogram stretch) when given."""
function build_cpt_plot(
    colors::AbstractVector,
    color_style::Integer,
    color_flip::Bool,
    color_start::Real = 0.0,
    color_int::Real = 1.0;
    boundaries::Union{Nothing,AbstractVector} = nothing,
)::Vector{CptLine}
    ncolors = length(colors)
    nseg = color_style == 1 ? ncolors - 1 : ncolors
    if boundaries !== nothing
        if length(boundaries) < nseg + 1
            throw(ArgumentError("need $(nseg + 1) stretch boundaries, got $(length(boundaries))"))
        end
        z = collect(Float64, boundaries[1:nseg+1])
    else
        z = [color_start + i * color_int for i in 0:nseg]
    end
    lines = CptLine[]
    # k and i are 0-based, as in the Python original
    for k in 0:nseg-1
        if color_style == 1
            i = !color_flip ? k : ncolors - 2 - k
            c1, c2 = !color_flip ? (colors[i+1], colors[i+2]) : (colors[i+2], colors[i+1])
        else
            i = !color_flip ? k : ncolors - 1 - k
            c1 = c2 = colors[i+1]
        end
        push!(lines, CptLine(z[k+1], c1, z[k+2], c2))
    end
    return lines
end


"""Port of mbm_plot's amplitude-shading CPT for -G3 with a stretched
shade (lines ~2210-2270): grayscale palette 4 over the Gaussian
mbhistogram intervals, used by `mbswath -N`."""
function build_shade_cpt(boundaries::AbstractVector, shade_flip::Bool)::Vector{CptLine}
    gray = color.interpolate_palette(4, NCPT)
    return build_cpt_plot(gray, 1, shade_flip; boundaries = boundaries)
end


# ---------------------------------------------------------------------------
# Controls
# ---------------------------------------------------------------------------

"""Port of mbm_plot lines ~1761-1845: build mbcontour's
-A<cont_int>/<color_int>/<tick_int>/<label_int>/<tick_len>/<label_hgt>.
The default label/tick intervals are 4 x contour interval, or (when
a color image is drawn beneath) effectively never (100000)."""
function resolve_contour_control(
    contour_control::OptS, contour_int_auto::Real, color_mode::Integer, page_height::Real,
)::String
    color_default(ci) = color_mode != 0 ? 100000 : 4 * ci
    local parts::Vector{Any}
    if !_given(contour_control)
        ci = contour_int_auto
        cci = color_default(ci)
        tick_len = 0.001 * page_height
        label_hgt = 0.01 * page_height
        if tick_len < 0.01 || label_hgt < 0.1
            tick_len, label_hgt = 0.01, 0.1
        end
        parts = Any[ci, cci, cci, cci, tick_len, label_hgt]
    else
        f = Any[String(s) for s in split(contour_control, "/")]
        if length(f) >= 6
            parts = f[1:6]
        elseif length(f) == 5
            parts = vcat(f, Any[0.1])
        elseif length(f) == 4
            parts = vcat(f, Any[0.01, 0.1])
        elseif length(f) == 3
            parts = vcat(f, Any[f[2], 0.01, 0.1])
        elseif length(f) == 2
            parts = vcat(f, Any[f[2], f[2], 0.01, 0.1])
        else
            cci = color_default(pyfloat(f[1]))
            parts = Any[f[1], cci, cci, cci, 0.01, 0.1]
        end
    end
    return join([v isa AbstractString ? v : fmtg(v, 6) for v in parts], "/")
end


"""mbswath -G<magnitude>/<azimuth> (color_mode 2), or
-G<magnitude>/<zero_level> (color_mode 3)."""
Base.@kwdef mutable struct ShadeControl
    magnitude::Float64
    second::Float64
end

as_arg(sc::ShadeControl) = "$(fmtg(sc.magnitude, 4))/$(fmtg(sc.second, 4))"


"""Port of mbm_plot lines ~1921-1945 (see the module header for the
fixes): `second` is the azimuth for color_mode 2 and the amplitude
zero level for color_mode 3."""
function resolve_shade_control(
    color_mode::Integer;
    magnitude::Union{Nothing,Real} = nothing,
    second::Union{Nothing,Real} = nothing,
    amin::Union{Nothing,Real} = nothing,
    amax::Union{Nothing,Real} = nothing,
    stretch_shade::Bool = false,
    shade_flip::Bool = false,
)::ShadeControl
    if color_mode == 3
        if stretch_shade
            dmag, dsec = 1.0, 191.0
        elseif amin !== nothing && amax !== nothing && amax > amin
            dmag = 2.0 / (amax - amin)
            dsec = amin + 0.2 * (amax - amin)
        else
            dmag, dsec = 1.0, 191.0
        end
    else
        dmag, dsec = 2.5, 0.0
    end
    sc = ShadeControl(magnitude === nothing ? dmag : Float64(magnitude), second === nothing ? dsec : Float64(second))
    if color_mode == 2 && shade_flip
        sc.second = sc.second > 180.0 ? sc.second - 180.0 : sc.second + 180.0
    end
    return sc
end


# ---------------------------------------------------------------------------
# Script generation
# ---------------------------------------------------------------------------

"""MB-System data-selection options passed to mbswath/mbcontour
(mbm_plot -MM...)."""
Base.@kwdef mutable struct SwathOptions
    footprint::OptS = nothing          # -MMA -> mbswath -A (default 1)
    btime::OptS = nothing              # -MMB -> -b
    etime::OptS = nothing              # -MME -> -E
    swath_scale::OptS = nothing        # -MMD -> mbswath -D
    contour_nplot::OptS = nothing      # -MMN -> mbcontour -N
    speedmin::OptS = nothing           # -MMS -> -S
    timegap::OptS = nothing            # -MMT -> -T
    contour_algorithm::OptS = nothing  # -MMZ -> mbcontour -Z
    lonflip::Union{Nothing,Int} = nothing   # -MML -> -L
end


Base.@kwdef mutable struct ScriptOptionsPlot
    file_data::String
    fmt::Int
    root::String
    layout::Layout
    output_format::String = "pdf"
    title::OptS = nothing
    tick_info::OptS = nothing
    colorbar_label::OptS = nothing
    length_scale::OptS = nothing
    unix_stamp::OptS = nothing           # "" for a plain -U
    dpi::Union{Nothing,Int} = nothing

    color_mode::Int = 0                  # 0 none, 1-5 mbswath -Z mode
    use_filtered::Bool = false           # mbswath -Z<mode>F
    color_palette::Int = 1
    color_flip::Bool = false
    cpt_lines::Union{Nothing,Vector{CptLine}} = nothing
    cpt_file::OptS = nothing
    shade::Union{Nothing,ShadeControl} = nothing
    shade_cpt_lines::Union{Nothing,Vector{CptLine}} = nothing

    contour_control::OptS = nothing                      # resolved mbcontour -A string, or nothing
    navigation::Union{Nothing,NavigationOptions} = nothing   # resolved, or nothing
    swath::SwathOptions = SwathOptions()

    coast::Union{Nothing,CoastOptions} = nothing
    xy_entries::Vector{XYOverlayEntry} = XYOverlayEntry[]
    text_labels::Vector{String} = String[]
    gmt_defs::Vector{String} = String[]
end

cpt_path(opts::ScriptOptionsPlot) = _given(opts.cpt_file) ? opts.cpt_file : "$(opts.root).cpt"
shade_cpt_path(opts::ScriptOptionsPlot) = "$(opts.root).shade.cpt"
script_path(opts::ScriptOptionsPlot) = "$(opts.root).sh"


function _swath_common(opts::ScriptOptionsPlot)::Vector{String}
    s = opts.swath
    parts = String[]
    _given(s.btime) && push!(parts, "-b$(s.btime)")
    _given(s.etime) && push!(parts, "-E$(s.etime)")
    _given(s.speedmin) && push!(parts, "-S$(s.speedmin)")
    _given(s.timegap) && push!(parts, "-T$(s.timegap)")
    s.lonflip !== nothing && push!(parts, "-L$(s.lonflip)")
    return parts
end


"""-BWESN (all four sides annotated, as in mbm_plot's classic-mode
plots; see MbjlGrdplotScript._basemap_axes()) plus the title, then the
tick intervals."""
function _basemap_axes(opts::ScriptOptionsPlot)::String
    frame = "-BWESN" * (_given(opts.title) ? "+t$(shquote(opts.title))" : "")
    if _given(opts.tick_info)
        return "$frame -B$(opts.tick_info)"
    end
    tick = opts.layout.base_tick
    if tick.tick_x == tick.tick_y
        return "$frame -B$(tick.tick_x)"
    end
    return "$frame -Bx$(tick.tick_x) -By$(tick.tick_y)"
end


function generate_script_plot(opts::ScriptOptionsPlot, command_line::OptS = nothing)::String
    lay = opts.layout
    q = shquote
    j = "-J$(lay.projection)$(lay.projection_pars)"
    r = "-R$(lay.region)"

    lines = String[
        "#!/usr/bin/env bash",
        "#",
        "# GMT modern-mode script to plot swath data from $(opts.file_data)",
        "# Generated by mbjl_plot (Julia port of mbm_plot)",
    ]
    if _given(command_line)
        push!(lines, "#   $(command_line)")
    end
    append!(lines, ["#", "set -e", ""])

    # a single swath file is plotted through a one-line datalist, as in
    # mbm_plot; a datalist is used directly
    if opts.fmt >= 0
        file_list = "$(opts.root)_datalist.mb-1"
        push!(lines, "# Make datalist file")
        push!(lines, "echo $(q(opts.file_data)) $(opts.fmt) > $(q(file_list))")
        push!(lines, "")
    else
        file_list = opts.file_data
    end
    file_list_q = q(file_list)

    push!(lines, "gmt begin $(q(opts.root)) $(opts.output_format)")
    push!(lines, "")
    if opts.dpi !== nothing && opts.dpi != 0
        push!(lines, "gmt figure $(q(opts.root)) $(opts.output_format) E$(opts.dpi)")
        push!(lines, "")
    end

    push!(lines, "# Set temporary GMT defaults")
    pairs = String[]
    # mbm_plot does not swap the background/foreground colors for -D
    for gmt_def in baseline_gmt_defaults(lay.pagesize, false, lay.degree_format)
        param, value = MbjlGrdplotScript._partition(gmt_def, " ")
        push!(pairs, "$param $value")
    end
    push!(lines, "gmt set $(join(pairs, " "))")
    push!(lines, "")
    if !isempty(opts.gmt_defs)
        push!(lines, "# User-defined GMT parameter overrides")
        user_pairs = String[]
        for gmt_def in opts.gmt_defs
            param, value = MbjlGrdplotScript._partition(gmt_def, "/")
            push!(user_pairs, "$param $value")
        end
        push!(lines, "gmt set $(join(user_pairs, " "))")
        push!(lines, "")
    end

    if opts.color_mode != 0
        zmode = opts.use_filtered ? "$(opts.color_mode)F" : "$(opts.color_mode)"
        parts = ["gmt mbswath -F-1 -I$(file_list_q) $j $r -C$(q(cpt_path(opts))) -Z$(zmode)",
                 "-A$(_given(opts.swath.footprint) ? opts.swath.footprint : "1")"]
        if opts.color_mode in (2, 3) && opts.shade !== nothing
            push!(parts, "-G$(as_arg(opts.shade))")
        end
        if opts.color_mode == 3 && opts.shade_cpt_lines !== nothing
            push!(parts, "-N$(q(shade_cpt_path(opts)))")
        end
        if opts.dpi !== nothing && opts.dpi != 0
            push!(parts, "-E$(opts.dpi)")
        end
        if _given(opts.swath.swath_scale)
            push!(parts, "-D$(opts.swath.swath_scale)")
        end
        append!(parts, _swath_common(opts))
        push!(lines, "# Plot the swath data")
        push!(lines, join(parts, " "))
        push!(lines, "")
    end

    nav = opts.navigation
    if _given(opts.contour_control) || nav !== nothing
        parts = ["gmt mbcontour -F-1 -I$(file_list_q) $j $r"]
        if _given(opts.contour_control)
            if _given(opts.swath.contour_algorithm)
                push!(parts, "-Z$(opts.swath.contour_algorithm)")
            elseif opts.fmt in (41, 101, 102)
                push!(parts, "-Z1")
            end
            push!(parts, "-A$(opts.contour_control)")
        end
        if nav !== nothing
            push!(parts, "-D$(nav.navigation_control)")
            if nav.name_mode
                push!(parts, nav.name_perp ? "-G$(pystr(nav.nav_name_hgt))/1" : "-G$(pystr(nav.nav_name_hgt))")
            end
            if nav.pingnumber_mode
                push!(parts, "-M$(nav.pingnumber_control)")
            end
        end
        append!(parts, _swath_common(opts))
        if _given(opts.swath.contour_nplot)
            push!(parts, "-N$(opts.swath.contour_nplot)")
        end
        push!(lines, "# Plot contours and/or navigation")
        push!(lines, join(parts, " "))
        push!(lines, "")
    end

    if opts.coast !== nothing
        co = opts.coast
        parts = ["gmt coast $j $r"]
        _given(co.coast_lakefill) && push!(parts, "-C$(co.coast_lakefill)")
        _given(co.coast_resolution) && push!(parts, "-D$(co.coast_resolution)")
        _given(co.coast_dryfill) && push!(parts, "-G$(co.coast_dryfill)")
        _given(co.coast_river) && push!(parts, "-I$(co.coast_river)")
        for boundary in co.coast_boundaries
            push!(parts, "-N$(boundary)")
        end
        _given(co.coast_wetfill) && push!(parts, "-S$(co.coast_wetfill)")
        _given(co.coast_pen) && push!(parts, "-W$(co.coast_pen)")
        push!(lines, "# Plot coastline")
        push!(lines, join(parts, " "))
        push!(lines, "")
    end

    for entry in opts.xy_entries
        parts = ["gmt plot $(q(entry.file)) $j $r"]
        entry.fill != "N" && push!(parts, "-G$(entry.fill)")
        entry.symbol != "N" && push!(parts, "-S$(entry.symbol)")
        entry.pen != "N" && push!(parts, "-W$(entry.pen)")
        push!(lines, join(parts, " "))
    end
    if !isempty(opts.xy_entries)
        push!(lines, "")
    end

    if opts.color_mode != 0 && opts.color_palette < 5
        # same placement as mbjl_grdplot (see its colorbar comment)
        label = opts.colorbar_label !== nothing ? opts.colorbar_label : DEFAULT_SCALE_LABELS[opts.color_mode]
        if lay.colorscale_vh == "+v"
            d_xy = "$(fmtg(lay.colorscale_offx, 5))/0"
        else
            d_xy = "0/$(fmtg(lay.colorscale_offy, 5))"
        end
        push!(lines, "# Plot color scale")
        push!(lines,
            "gmt colorbar -C$(q(cpt_path(opts))) -Dx$(d_xy)$(lay.colorscale_vh)" *
            "+w$(fmtg(lay.colorscale_length, 5))/$(fmtg(lay.colorscale_thick, 5)) -B+l$(q(label))"
        )
        push!(lines, "")
    end

    if !isempty(opts.text_labels)
        # see mbjl_grdplot_script.jl for the modern-mode -F+f+a+j record layout
        push!(lines, "gmt text $j $r -F+f+a+j <<EOF")
        for raw in opts.text_labels
            tx, ty, tsize, tangle, font, just, txt = split(raw, "/"; limit = 7)
            push!(lines, "$tx $ty $tsize,$font $tangle $just $txt")
        end
        push!(lines, "EOF")
        push!(lines, "")
    end

    parts = ["gmt basemap $j $r $(_basemap_axes(opts))"]
    if _given(opts.length_scale)
        push!(parts, "-L$(opts.length_scale)")
    end
    if opts.unix_stamp !== nothing
        push!(parts, isempty(opts.unix_stamp) ? "-U" : "-U$(q(opts.unix_stamp))")
    end
    push!(lines, join(parts, " "))
    push!(lines, "")
    push!(lines, "gmt end")
    push!(lines, "")

    cleanup = String[]
    if opts.fmt >= 0
        push!(cleanup, file_list_q)
    end
    if !isempty(cleanup)
        push!(lines, "# Delete surplus files")
        push!(lines, "rm -f " * join(cleanup, " "))
        push!(lines, "")
    end
    return join(lines, "\n")
end


"""Write the CPT files (when generated) and the shellscript; make
the script executable and return its path."""
function generate_and_write_plot(opts::ScriptOptionsPlot, command_line::OptS = nothing)::String
    if opts.cpt_lines !== nothing && opts.cpt_file === nothing
        write_cpt_file(cpt_path(opts), opts.cpt_lines)
    end
    if opts.shade_cpt_lines !== nothing
        write_cpt_file(shade_cpt_path(opts), opts.shade_cpt_lines)
    end
    path = script_path(opts)
    write(path, generate_script_plot(opts, command_line))
    chmod(path, 0o755)
    return path
end

end # module MbjlPlotScript
