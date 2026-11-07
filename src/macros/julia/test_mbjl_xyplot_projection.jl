#!/usr/bin/env julia
#=
Validate mbjl_xyplot's handling of user "x"/"X" -J specs against the
real Perl mbm_xyplot (Julia port of python/test_mbpy_xyplot_projection.py).

mbm_xyplot's (and mbm_histplot's) GetProjection treats only a bare
"xd"/"Xd" as a geographic (degree) axis; any other remainder, such as
"x3.0/-0.05" (separate x and y scales, y axis reversed), "X6/-8" or
"X6i", is a complete user scale/width. mbm_grdplot's GetProjection
instead marks every "x"/"X" spec as "linear", and reusing that for the
xy and histogram plots produced unusable -J values such as
"-Jx3.0/-0.050.2d" and dropped the axis labels.

For each -J spec this:
  1. runs the Perl `mbm_xyplot` macro to generate a `.cmd` shellscript,
  2. extracts MAP_PROJECTION / MAP_SCALE / X_OFFSET / Y_OFFSET and the
     -B axis spec from that script,
  3. runs MbjlXyplotScript.compute_layout_xy() on the same data,
  4. compares the two.
It also checks MbjlHistplotScript.compute_layout_hist(), which shares
the parser, for the same specs.

Requires `gmt` and `perl` on PATH and mbm_xyplot from this checkout.
Not wired into ctest -- run by hand, like the other test_mbjl_*.jl.
Paths handed to perl are written with forward slashes (see
test_mbjl_layout.jl).
=#
include(joinpath(@__DIR__, "mbjl_xyplot_script.jl"))
using .MbjlUtil
const layout = MbjlLayout
const xyplot = MbjlXyplotScript
const histplot = MbjlHistplotScript

const REPO = normpath(joinpath(@__DIR__, "..", "..", ".."))
const MBM_XYPLOT = joinpath(REPO, "src", "macros", "mbm_xyplot")

const TOLERANCE = 5e-3  # inches

# sound speed (m/s) vs depth (m), as made by `mbsvplist -S | awk '{print $2,$1}'`
const SVP_DATA = "1500 0\n1495 50\n1490 100\n1485 200\n"

const SPECS = ["x3.0/-0.05", "x0.3", "X6/-8", "X6i", "X8/6"]

fwd(p::AbstractString) = replace(p, '\\' => '/')


function run_perl(datafile::AbstractString, root::AbstractString, map_scale::AbstractString)
    args = ["perl", fwd(MBM_XYPLOT), "-ISc0.05:$(basename(datafile))", "-J$map_scale",
            "-LT:Sound Speed (m/s):Depth (m)", "-O$(basename(root))"]
    run(pipeline(Cmd(Cmd(args); dir = dirname(datafile), ignorestatus = true);
                 stdout = devnull, stderr = devnull))
    return read(joinpath(dirname(root), "$(basename(root)).cmd"), String)
end


function extract(cmd_text::AbstractString, var::AbstractString)
    m = match(Regex("^$(var)=(\\S+)\\r?\$", "m"), cmd_text)
    return m !== nothing ? String(m.captures[1]) : ""
end


function extract_axes(cmd_text::AbstractString)
    m = match(r"(-Bx\S+\+l\"[^\"]*\" -By\S+\+l\"[^\"]*\")", cmd_text)
    return m !== nothing ? String(m.captures[1]) : ""
end


_num(x::Real) = Float64(x)
_num(x::AbstractString) = pyfloat(x)
_num(x) = throw(ArgumentError("not a number"))


function check(name, expected, got)
    ok = try
        abs(_num(expected) - _num(got)) <= TOLERANCE
    catch
        string(expected) == string(got)
    end
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name: perl=$(expected) julia=$(got)")
    return ok
end


same_spec(a, b) = all(getfield(a, f) == getfield(b, f) for f in fieldnames(typeof(a)))


function main()
    tmp = mktempdir(; prefix = "mbjl_xyplot_projection_test_", cleanup = false)
    println("scratch dir: $tmp")
    datafile = joinpath(tmp, "svp.txt")
    write(datafile, SVP_DATA)
    all_ok = true

    # the parser itself
    println("\n=== get_projection_xy ===")
    spec = layout.get_projection_xy("x3.0/-0.05")
    all_ok &= check("x3.0/-0.05 linear", false, spec.linear)
    all_ok &= check("x3.0/-0.05 plot_scale", 3.0, spec.plot_scale)
    all_ok &= check("x3.0/-0.05 projection_pars", "3.0/-0.05", spec.projection_pars)
    spec = layout.get_projection_xy("X6i")
    all_ok &= check("X6i plot_width", 6.0, spec.plot_width)
    spec = layout.get_projection_xy("xd")
    all_ok &= check("xd linear", true, spec.linear)
    spec = layout.get_projection_xy("m1.0")
    all_ok &= check("m1.0 unchanged", true, same_spec(layout.get_projection("m1.0"), spec))

    for map_scale in SPECS
        println("\n=== xyplot -J$map_scale ===")
        root = joinpath(tmp, "perl_$(replace(map_scale, "/" => "_"))")
        cmd_text = run_perl(datafile, root, map_scale)
        result = xyplot.compute_layout_xy(
            1485.0, 1500.0, 0.0, 200.0; map_scale = map_scale,
            xlabel = "Sound Speed (m/s)", ylabel = "Depth (m)",
        )
        all_ok &= check("projection", extract(cmd_text, "MAP_PROJECTION"), result.projection)
        all_ok &= check("scale/pars", extract(cmd_text, "MAP_SCALE"), result.projection_pars)
        all_ok &= check("x_offset", extract(cmd_text, "X_OFFSET"), result.xoffset)
        all_ok &= check("y_offset", extract(cmd_text, "Y_OFFSET"), result.yoffset)
        all_ok &= check("axes", extract_axes(cmd_text), result.axes_interval)

        println("=== histplot -J$map_scale ===")
        hist = histplot.compute_layout_hist(
            1485.0, 1500.0, 0.0, 200.0; map_scale = map_scale,
            xlabel = "Sound Speed (m/s)", ylabel = "Frequency %",
        )
        all_ok &= check("scale/pars", map_scale[2:end], hist.projection_pars)
        all_ok &= check("labeled axes", true, occursin("+l\"Frequency %\"", hist.axes_interval))
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
