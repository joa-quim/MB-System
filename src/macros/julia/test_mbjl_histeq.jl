#!/usr/bin/env julia
# Validate the histogram-equalization / slope-magnitude CPT logic in
# mbjl_color.jl against the real Perl mbm_grdplot.
#
# Same pattern as test_mbjl_color.jl: generate a grid, run the
# real Perl macro with -S (real grdhisteq stretch) or -G4 (slope
# magnitude fill), extract the CPT echo lines it writes, and compare
# against the Julia port line-for-line.
#
# Julia port of python/test_mbpy_histeq.py (paths handed to perl use
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
        println("      perl:   $perl_lines")
        println("      julia:  $py_lines")
    end
    return ok
end


function main()
    tmp = mktempdir(; prefix = "mbm_grdplot_histeq_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    grids = [
        ("geo_wide", "-122.5/-121.5/36.5/37.0", "0.01"),
        ("geo_tall", "-122.2/-122.0/36.0/37.5", "0.01"),
    ]

    for (grid_name, region, inc) in grids
        grid = joinpath(tmp, "$grid_name.grd")
        make_grid(grid, region, inc)
        grid_info = run_grdinfo(grid)
        zmin, zmax = grid_info.zmin, grid_info.zmax
        dzz = zmax - zmin

        # --- real histogram-equalized stretch (-S1) ---
        for (ncolors, palette, flip, label) in [
            (11, 1, false, "stretch_default"),
            (11, 1, true, "stretch_flip"),
            (6, 2, false, "stretch_palette2_n6"),
            (20, 4, false, "stretch_palette4_n20"),
        ]
            case = "$grid_name/$label"
            println("\n=== case $case ===")
            extra = ["-W1/$palette/$ncolors", "-S1"]
            if flip
                push!(extra, "-D1")
            end
            root = joinpath(tmp, "histeq_$(grid_name)_$(label)")
            cmd_text = run_perl(grid, root, extra)
            perl_lines = extract_cpt_lines(cmd_text)

            rows = run_grdhisteq(fwd(grid), ncolors)
            hb = build_histogram_boundaries(rows, ncolors, zmin, zmax, dzz; color_style = 1)
            colors = interpolate_palette(palette, hb.ncolors)
            py_lines_obj = build_cpt_continuous_from_boundaries(
                colors, hb.hist; color_flip = flip
            )
            py_lines = [as_written(l) for l in py_lines_obj]

            all_ok &= compare(case, perl_lines, py_lines)
        end

        # --- slope magnitude fill (-G4, magnitude via -Amagnitude) ---
        for (magnitude, ncolors, label) in [
            (1.0, 11, "slope_default"),
            (2.5, 8, "slope_mag2p5_n8"),
        ]
            case = "$grid_name/$label"
            println("\n=== case $case ===")
            extra = ["-G4", "-A$(pystr(magnitude))", "-W1/1/$ncolors"]
            root = joinpath(tmp, "histeq_$(grid_name)_$(label)")
            cmd_text = run_perl(grid, root, extra)
            perl_lines = extract_cpt_lines(cmd_text)

            boundaries = build_slope_boundaries(magnitude, ncolors, 1)
            colors = interpolate_palette(1, ncolors)
            py_lines_obj = build_cpt_continuous_from_boundaries(
                colors, boundaries; color_flip = false
            )
            py_lines = [as_written(l) for l in py_lines_obj]

            all_ok &= compare(case, perl_lines, py_lines)
        end
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
