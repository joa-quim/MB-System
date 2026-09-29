#!/usr/bin/env julia
# Validate the sealevel dual-colormap CPT logic in
# mbjl_color.jl against the real Perl mbm_grdplot.
#
# Same pattern as the other test_mbjl_*.jl scripts, but needs a
# grid whose z values actually straddle zero (the sealevel palettes'
# whole point is a land/sea color split at z == 0), unlike the other
# scripts' synthetic grids which are all-negative.
#
# Julia port of python/test_mbpy_sealevel.py (paths handed to perl use
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


function make_grid(path::AbstractString, region::AbstractString, expr::AbstractVector, inc::AbstractString = "0.01")
    run(pipeline(`gmt grdmath -R$region -I$inc $expr = $(fwd(path))`;
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
    tmp = mktempdir(; prefix = "mbm_grdplot_sealevel_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    # z = (lon + 121.9) * 20 straddles zero across the grid's lon range
    grid = joinpath(tmp, "sealevel.grd")
    make_grid(grid, "-122.5/-121.5/36.5/37.0", ["X", "121.9", "ADD", "20", "MUL"])
    grid_info = run_grdinfo(grid)
    lay = compute_layout(grid_info; pagesize = "a", scale_loc = "b")
    zmin, zmax = grid_info.zmin, grid_info.zmax
    dzz = zmax - zmin

    cases = [
        # (label, extra perl args, palette, ncolors, style, flip)
        ("continuous_default", ["-W1/8"], 8, 11, 1, false),
        ("continuous_flip",    ["-W1/8", "-D1"], 8, 11, 1, true),
        ("continuous_n16",     ["-W1/8/16"], 8, 16, 1, false),
        ("discrete_default",   ["-W2/8"], 8, 11, 2, false),
        ("discrete_flip",      ["-W2/8", "-D1"], 8, 11, 2, true),
        ("discrete_palette9",  ["-W2/9"], 9, 11, 2, false),
    ]

    for (label, extra, palette, ncolors, style, flip) in cases
        println("\n=== case $label ===")
        root = joinpath(tmp, "sealevel_$label")
        cmd_text = run_perl(grid, root, extra)
        perl_lines = extract_cpt_lines(cmd_text)

        ncolors_use = get_ncolors_use(ncolors, style)
        interval = compute_color_interval(
            dzz, zmin, zmax, ncolors_use, lay.contour_int, false
        )
        izero = compute_sealevel_izero(
            ncolors_use, interval.color_int, interval.color_start, interval.color_end, flip
        )
        cptub, cptue = interpolate_palette_sealevel(palette, ncolors_use, izero)

        if style == 1
            py_lines_obj = MbjlColor._write_cpt_continuous_arithmetic(
                cptub, cptue, interval.color_start, interval.color_int, flip, 0, nothing, nothing
            )
        else
            py_lines_obj = MbjlColor._write_cpt_discrete_arithmetic(
                cptub, cptue, ncolors, interval.color_start, interval.color_int, flip, 0, nothing, nothing
            )
        end
        py_lines = [as_written(l) for l in py_lines_obj]

        all_ok &= compare(label, perl_lines, py_lines)
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
