#!/usr/bin/env julia
# Validate mbjl_layout.jl against the real Perl mbm_grdplot.
#
# For each synthetic test grid and option combination, this:
#   1. runs the Perl `mbm_grdplot` macro to generate a `.cmd` shellscript,
#   2. extracts the ground-truth MAP_PROJECTION / MAP_SCALE / X_OFFSET /
#      Y_OFFSET / basemap tick values from that script,
#   3. runs the equivalent computation through mbjl_layout.jl,
#   4. compares the two numerically.
#
# Requires `gmt` and `perl` on PATH and mbm_grdplot from this checkout.
# Not wired into ctest yet -- this is a validation script for the layout
# port, run by hand while porting.
#
# Julia port of python/test_mbpy_layout.py. Paths handed to perl are
# written with forward slashes: mbm_grdplot runs `gmt grdinfo <path>`
# through a shell, which (under an MSYS/Cygwin perl on Windows) eats
# the backslashes of a native Windows path.

include(joinpath(@__DIR__, "mbjl_layout.jl"))
using .MbjlLayout
using .MbjlUtil

const REPO = normpath(joinpath(@__DIR__, "..", "..", ".."))
const MBM_GRDPLOT = joinpath(REPO, "src", "macros", "mbm_grdplot")

const TOLERANCE = 5e-3  # inches / scale units

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


function extract(cmd_text::AbstractString, key::AbstractString)
    m = match(Regex("^$(key)=(.*)\$", "m"), cmd_text)
    return m !== nothing ? String(rstrip(m.captures[1], '\r')) : nothing
end


function extract_tick(cmd_text::AbstractString)
    m = match(r"-B(\S+) -B\+t", cmd_text)
    return m !== nothing ? String(m.captures[1]) : nothing
end


function check(name, expected, actual, tol = TOLERANCE)
    ok = false
    e, a = expected, actual
    try
        e = expected isa Real ? Float64(expected) : pyfloat(expected)
        a = actual isa Real ? Float64(actual) : pyfloat(actual)
        ok = abs(e - a) <= tol * max(1.0, abs(e))
    catch
        ok = (expected == actual)
        e, a = expected, actual
    end
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name: perl=$(repr(e)) julia=$(repr(a))")
    return ok
end


function main()
    tmp = mktempdir(; prefix = "mbjl_layout_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    cases = [
        ("geo_wide", "-122.5/-121.5/36.5/37.0", Dict{String,String}()),
        ("geo_tall", "-122.2/-122.0/36.0/37.5", Dict{String,String}()),
        ("geo_small", "-122.05/-122.0/36.80/36.83", Dict("inc" => "0.0005")),
    ]

    for (name, region, opts) in cases
        grid = joinpath(tmp, "$name.grd")
        make_grid(grid, region, get(opts, "inc", "0.01"))

        for (pagesize, scale_loc) in [("a", "b"), ("a", "l"), ("a", "r"), ("a", "t"), ("b", "b")]
            root = joinpath(tmp, "$(name)_$(pagesize)_$(scale_loc)")
            extra = ["-P$pagesize"]
            if scale_loc != "b"
                push!(extra, "-MGF$scale_loc")
            end
            println("\n=== case $name pagesize=$pagesize scale_loc=$scale_loc ===")
            cmd_text = run_perl(grid, root, extra)

            grid_info = run_grdinfo(grid)
            result = compute_layout(grid_info; pagesize = pagesize, scale_loc = scale_loc)

            perl_proj = extract(cmd_text, "MAP_PROJECTION")
            perl_scale = extract(cmd_text, "MAP_SCALE")
            perl_x = extract(cmd_text, "X_OFFSET")
            perl_y = extract(cmd_text, "Y_OFFSET")
            perl_tick = extract_tick(cmd_text)

            all_ok &= check("projection", perl_proj, result.projection)
            all_ok &= check("scale/pars", perl_scale, result.projection_pars)
            all_ok &= check("x_offset", perl_x, result.xoffset)
            all_ok &= check("y_offset", perl_y, result.yoffset)
            all_ok &= check("base_tick", perl_tick, result.base_tick.tick_x)
        end
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
