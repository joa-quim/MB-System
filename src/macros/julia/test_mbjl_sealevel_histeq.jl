#!/usr/bin/env julia
# Check the histogram-equalized sealevel CPT (palettes 8/9 with
# --stretch-color) in mbjl_color.jl. mbm_grdplot has no equivalent, so
# instead of a Perl cross-check this verifies the properties the CPT must
# have on synthetic grids: increasing, contiguous z boundaries covering
# the data, a boundary exactly at 0, Haxby colors on the bathymetry side
# and land colors on the topography side (swapped with color_flip), bins
# shared in proportion to node counts, and the same result whether or
# not the grid is decimated first.
#
# Julia port of python/test_mbpy_sealevel_histeq.py.

include(joinpath(@__DIR__, "mbjl_color.jl"))
using .MbjlColor

const LAND_COAST = (200, 150, 40)    # palette 8 stop 10
const HAXBY_COAST = (255, 186, 133)  # Haxby stop 1


function make_grid(path::AbstractString, offset::Real)
    run(pipeline(`gmt grdmath -R0/1/0/1 -I0.01 X Y ADD 1000 MUL $(string(Float64(offset))) ADD = $path`;
                 stdout = devnull, stderr = devnull))
end


function build(grid::AbstractString, zmin::Real, zmax::Real, nbins::Integer, flip::Bool; max_nodes = nothing)
    rows_below, rows_above = max_nodes === nothing ?
        run_grdhisteq_sealevel(grid, nbins, zmin, zmax) :
        run_grdhisteq_sealevel(grid, nbins, zmin, zmax; max_nodes = max_nodes)
    below, above = build_sealevel_histogram_boundaries(rows_below, rows_above, zmin, zmax, zmax - zmin)
    return below, above, build_cpt_sealevel_from_boundaries(8, below, above; color_flip = flip)
end


function check(label::AbstractString, cond::Bool)
    println("  $(cond ? "ok  " : "FAIL") $label")
    return cond
end


function main()
    ok = true
    println("allocate_sealevel_bins")
    ok &= check("proportional split", allocate_sealevel_bins(10, 780, 220) == (8, 2))
    ok &= check("at least one bin per side", allocate_sealevel_bins(10, 999, 1) == (9, 1))
    ok &= check("all below", allocate_sealevel_bins(10, 5, 0) == (10, 0))
    ok &= check("all above", allocate_sealevel_bins(10, 0, 5) == (0, 10))

    mktempdir() do tmp
        for (name, offset, zmin, zmax) in (("mixed", -1200.0, -1200.0, 800.0),
                                           ("below", -2100.0, -2100.0, -100.0),
                                           ("above", 5.0, 5.0, 2005.0))
            grid = joinpath(tmp, "$name.nc")
            make_grid(grid, offset)
            for flip in (false, true)
                println("$name grid, flip=$flip")
                below, above, lines = build(grid, zmin, zmax, 10, flip)
                ok &= check("10 segments", length(lines) == 10)
                ok &= check("covers data", lines[1].d1 < zmin && lines[end].d2 > zmax)
                ok &= check("contiguous and increasing",
                            all(a.d2 == b.d1 for (a, b) in zip(lines, lines[2:end]))
                            && all(l.d1 < l.d2 for l in lines))
                ok &= check("boundary exactly at 0",
                            any(l.d2 == 0.0 for l in lines) || any(l.d1 == 0.0 for l in lines))
                sea, land = !flip ? (HAXBY_COAST, LAND_COAST) : (LAND_COAST, HAXBY_COAST)
                # lines[] indices below are the Python 0-based ones + 1
                if !isempty(below)
                    ok &= check("below-zero coast color",
                                as_written(lines[length(below) - 1])[6:8] == sea)
                end
                if !isempty(above)
                    ok &= check("above-zero coast color",
                                as_written(lines[!isempty(below) ? length(below) : 1])[2:4] == land)
                end
                if name == "mixed"
                    ok &= check("bins split by node count", (length(below) - 1, length(above) - 1) == (7, 3))
                end
            end
        end

        println("decimated vs. full grid")
        full = build(joinpath(tmp, "mixed.nc"), -1200.0, 800.0, 10, false)[3]
        dec = build(joinpath(tmp, "mixed.nc"), -1200.0, 800.0, 10, false; max_nodes = 2600)[3]
        ok &= check("same bin count", length(full) == length(dec))
        ok &= check("boundaries within 2% of range",
                    all(abs(a.d2 - b.d2) < 40.0 for (a, b) in zip(full, dec)))
    end

    println("\n" * (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
