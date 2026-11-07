#!/usr/bin/env julia
#=
End-to-end test of mbjl_grdtiff (Julia port of
python/test_mbpy_grdtiff.py): runs the CLI on a synthetic geographic
grid for each color_mode (1-5), executes the generated GMT modern-mode
script, and checks that a GeoTIFF plus .tfw world file is produced and
that no gmt.conf is left behind.
=#

const CLI = joinpath(@__DIR__, "mbjl_grdtiff")

# forward slashes: paths end up inside a bash script
fwd(p::AbstractString) = replace(p, '\\' => '/')


function check(name, ok, detail = "")
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name" * (!isempty(detail) && !ok ? ": $detail" : ""))
    return ok
end


"""Run a command in `dir`; returns (exitcode, stdout, stderr)."""
function run_in(cmd::Cmd, dir::AbstractString)
    out, err = IOBuffer(), IOBuffer()
    p = run(pipeline(ignorestatus(Cmd(cmd; dir = dir)); stdout = out, stderr = err))
    return p.exitcode, String(take!(out)), String(take!(err))
end


function main()
    tmp = fwd(mktempdir(; prefix = "mbjl_grdtiff_test_", cleanup = false))
    println("scratch dir: $tmp")
    all_ok = true

    grid = "$tmp/topo.grd"
    run(pipeline(`gmt grdmath -R-122.5/-122.0/36.5/37.0 -I0.005
                  X 122.25 ADD 10 MUL SIN Y 36.75 SUB 8 MUL COS MUL
                  1000 MUL 3000 SUB = $grid`; stdout = devnull, stderr = devnull))
    intensity = "$tmp/int.grd"
    run(pipeline(`gmt grdgradient $grid -A45 -Nt -G$intensity -fg`; stdout = devnull, stderr = devnull))

    cases = [
        "g1" => ["-G1"],
        "g2" => ["-G2", "--azimuth", "45"],
        "g3" => ["-K", intensity],
        "g4" => ["-G4"],
        "g5" => ["-G5", "-W", "2/4/8"],
        "zb" => ["-G1", "-Z", "-3500/-2500/1", "--data-scale", "1.0"],
    ]
    julia = Base.julia_cmd()
    for (tag, extra) in cases
        code, _, err = run_in(`$julia --startup-file=no $CLI -I $grid -O $tag -X $extra`, tmp)
        all_ok &= check("$tag: script runs", code == 0, last(err, 400))
        tif = "$tmp/$tag.tif"
        all_ok &= check("$tag: GeoTIFF written", isfile(tif) && filesize(tif) > 10000)
        all_ok &= check("$tag: world file written", isfile("$tmp/$tag.tfw"))
        shf = "$tmp/$(tag)_tiff.sh"
        sh = isfile(shf) ? read(shf, String) : ""
        all_ok &= check("$tag: modern mode", occursin("gmt begin", sh) && occursin("gmt end", sh) &&
                                             !occursin("gmtset", sh))
    end

    all_ok &= check("no gmt.conf left behind", !isfile("$tmp/gmt.conf"))
    println(all_ok ? "ALL OK" : "SOME CHECKS FAILED")
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
