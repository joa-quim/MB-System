#!/usr/bin/env julia
# Validate the discrete (color_style != 1) CPT logic in
# mbjl_color.jl against the real Perl mbm_grdplot.
#
# Same pattern as the other test_mbjl_*.jl scripts: generate a
# grid, run the real Perl macro with -W2 (discrete style) in various
# combinations, extract the CPT echo lines it writes, and compare
# against the Julia port line-for-line -- including the black-segment
# quirk from the out-of-range array read (see mbjl_color.jl).
#
# Julia port of python/test_mbpy_discrete.py (paths handed to perl use
# forward slashes -- see test_mbjl_layout.jl).

include(joinpath(@__DIR__, "mbjl_layout.jl"))
include(joinpath(@__DIR__, "mbjl_color.jl"))
using .MbjlLayout
using .MbjlColor
using .MbjlUtil

const REPO = normpath(joinpath(@__DIR__, "..", "..", ".."))
const MBM_GRDPLOT = joinpath(REPO, "src", "macros", "mbm_grdplot")

const CPT_LINE_RE =
    r"^echo\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+>>?\s+\$CPT_FILE\s*$"

fwd(p::AbstractString) = replace(p, '\\' => '/')


function make_grid(path::AbstractString, region::AbstractString, inc::AbstractString = "0.01")
    run(pipeline(`gmt grdmath -R$region -I$inc X Y MUL 1000 MUL = $(fwd(path))`;
                 stdout = devnull, stderr = devnull))
end


function run_perl(grid::AbstractString, root::AbstractString, extra_args = String[])
    args = ["perl", fwd(MBM_GRDPLOT), "-I$(fwd(grid))", "-O$(fwd(root))", extra_args...]
    out, err = IOBuffer(), IOBuffer()
    run(pipeline(Cmd(Cmd(args); dir = dirname(grid), ignorestatus = true); stdout = out, stderr = err))
    cmdfile = root * ".cmd"
    if !isfile(cmdfile)
        error("mbm_grdplot did not produce $cmdfile\nstdout:\n$(String(take!(out)))\nstderr:\n$(String(take!(err)))")
    end
    return read(cmdfile, String)
end


function extract_cpt_lines(cmd_text::AbstractString)
    lines = Vector{NTuple{8,Float64}}()
    for line in split(cmd_text, r"\r\n|\n|\r")
        m = match(CPT_LINE_RE, strip(line))
        if m !== nothing
            push!(lines, Tuple(pyfloat(x) for x in m.captures))
        end
    end
    return lines
end


close_(a, b, tol = 1e-3) = abs(a - b) <= tol * max(1.0, abs(a))


function compare(name, perl_lines, py_lines)
    ok = length(perl_lines) == length(py_lines)
    if ok
        for (p, y) in zip(perl_lines, py_lines)
            if !all(close_(pp, yy) for (pp, yy) in zip(p, y))
                ok = false
                break
            end
        end
    end
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name: $(length(perl_lines)) perl lines, $(length(py_lines)) julia lines")
    if !ok
        for (i, (p, y)) in enumerate(zip(perl_lines, py_lines))
            marker = all(close_(pp, yy) for (pp, yy) in zip(p, y)) ? "" : "  <-- MISMATCH"
            println("      [$(i-1)] perl=$p julia=$y$marker")
        end
        if length(perl_lines) != length(py_lines)
            println("      perl:   $perl_lines")
            println("      julia:  $py_lines")
        end
    end
    return ok
end


function main()
    tmp = mktempdir(; prefix = "mbm_grdplot_discrete_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    grids = [
        ("geo_wide", "-122.5/-121.5/36.5/37.0", "0.01"),
        ("geo_tall", "-122.2/-122.0/36.0/37.5", "0.01"),
    ]

    arithmetic_cases = [
        # (label, extra perl args, palette, ncolors, flip, no_nice)
        ("discrete_default",      ["-W2"],              1, 11, false, false),
        ("discrete_flip",         ["-W2", "-D1"],       1, 11, true,  false),
        ("discrete_palette2_n6",  ["-W2/2/6"],          2, 6,  false, false),
        ("discrete_palette4_n20", ["-W2/4/20"],         4, 20, false, false),
        ("discrete_no_nice_int",  ["-W2", "-Y"],        1, 11, false, true),
    ]

    for (grid_name, region, inc) in grids
        grid = joinpath(tmp, "$grid_name.grd")
        make_grid(grid, region, inc)
        grid_info = run_grdinfo(grid)
        lay = compute_layout(grid_info; pagesize = "a", scale_loc = "b")
        zmin, zmax = grid_info.zmin, grid_info.zmax
        dzz = zmax - zmin

        for (name, extra, palette, ncolors, flip, no_nice) in arithmetic_cases
            case_name = "$grid_name/$name"
            println("\n=== case $case_name ===")
            root = joinpath(tmp, "discrete_$(grid_name)_$(name)")
            cmd_text = run_perl(grid, root, extra)
            perl_lines = extract_cpt_lines(cmd_text)

            ncolors_use = get_ncolors_use(ncolors, 2)
            interval = compute_color_interval(
                dzz, zmin, zmax, ncolors_use, lay.contour_int, no_nice
            )
            colors = interpolate_palette(palette, ncolors_use)
            py_lines_obj = build_cpt_discrete(
                colors, ncolors, interval.color_start, interval.color_int; color_flip = flip
            )
            py_lines = [as_written(l) for l in py_lines_obj]

            all_ok &= compare(case_name, perl_lines, py_lines)
        end

        # discrete + real histogram-equalized stretch
        for (ncolors, palette, flip, label) in [
            (11, 1, false, "discrete_stretch_default"),
            (11, 1, true, "discrete_stretch_flip"),
            (6, 2, false, "discrete_stretch_palette2_n6"),
        ]
            case_name = "$grid_name/$label"
            println("\n=== case $case_name ===")
            extra = ["-W2/$palette/$ncolors", "-S1"]
            if flip
                push!(extra, "-D1")
            end
            root = joinpath(tmp, "discrete_$(grid_name)_$(label)")
            cmd_text = run_perl(grid, root, extra)
            perl_lines = extract_cpt_lines(cmd_text)

            rows = run_grdhisteq(fwd(grid), ncolors)
            hb = build_histogram_boundaries(rows, ncolors, zmin, zmax, dzz; color_style = 2)
            colors = interpolate_palette(palette, hb.ncolors + 1)
            py_lines_obj = build_cpt_discrete_from_boundaries(
                colors, hb.ncolors, hb.hist; color_flip = flip
            )
            py_lines = [as_written(l) for l in py_lines_obj]

            all_ok &= compare(case_name, perl_lines, py_lines)
        end
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
