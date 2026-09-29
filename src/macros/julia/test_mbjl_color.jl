#!/usr/bin/env julia
# Validate mbjl_color.jl against the real Perl mbm_grdplot.
#
# For each grid / palette / ncolors / flip combination, this:
#   1. runs mbjl_layout to get contour_int (needed for the "nice
#      interval" color stretch, exactly as mbm_grdplot itself computes
#      it before building the CPT),
#   2. runs the Julia color-interval + palette-interpolation + CPT-line
#      port,
#   3. runs the real Perl mbm_grdplot with matching -W/-D/-Y options and
#      extracts the `echo d1 r1 g1 b1 d2 r2 g2 b2 >[>] $CPT_FILE` lines
#      it writes into the generated .cmd script,
#   4. compares the two CPT line lists element-for-element.
#
# Requires `gmt` and `perl` on PATH. Not wired into ctest -- a hand-run
# validation script for the color port, same pattern as
# test_mbjl_layout.jl.
#
# Julia port of python/test_mbpy_color.py (paths handed to perl use
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
    tmp = mktempdir(; prefix = "mbjl_color_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    grids = [
        ("geo_wide", "-122.5/-121.5/36.5/37.0", "0.01"),
        ("geo_tall", "-122.2/-122.0/36.0/37.5", "0.01"),
        ("geo_small", "-122.05/-122.0/36.80/36.83", "0.0005"),
    ]

    cases = [
        # (label, extra perl args, palette, ncolors, flip, no_nice)
        ("default",        String[],             1, 11, false, false),
        ("flip",           ["-D1"],             1, 11, true,  false),
        ("palette2_n6",    ["-W1/2/6"],         2, 6,  false, false),
        ("palette2_n6_flip", ["-W1/2/6", "-D1"], 2, 6, true,  false),
        ("palette4_n20",   ["-W1/4/20"],        4, 20, false, false),
        ("palette3_n7_flip", ["-W1/3/7", "-D1"], 3, 7, true, false),
        ("no_nice_int",    ["-Y"],              1, 11, false, true),
    ]

    for (grid_name, region, inc) in grids
        grid = joinpath(tmp, "$grid_name.grd")
        make_grid(grid, region, inc)
        grid_info = run_grdinfo(grid)
        lay = compute_layout(grid_info; pagesize = "a", scale_loc = "b")
        zmin, zmax = grid_info.zmin, grid_info.zmax
        dzz = zmax - zmin

        for (name, extra, palette, ncolors, flip, no_nice) in cases
            case_name = "$grid_name/$name"
            println("\n=== case $case_name ===")
            root = joinpath(tmp, "cpt_$(grid_name)_$(name)")
            cmd_text = run_perl(grid, root, extra)
            perl_lines = extract_cpt_lines(cmd_text)

            ncolors_use = get_ncolors_use(ncolors, 1)
            interval = compute_color_interval(
                dzz, zmin, zmax, ncolors_use, lay.contour_int, no_nice
            )
            colors = interpolate_palette(palette, ncolors)
            py_lines_obj = build_cpt_continuous(
                colors, interval.color_start, interval.color_int; color_flip = flip
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
