#!/usr/bin/env julia
#=
Tests of mbjl_plot (Julia port of python/test_mbpy_plot.py): unit
checks of the mbm_plot-specific logic in mbjl_plot_script.jl (CPT
loops, color interval, contour and shade controls, bounds expansion),
then an end-to-end run of the CLI on the small swath test file in the
repo (navigation, contours and a color fill through `gmt
mbswath`/`gmt mbcontour` in a modern-mode session).
=#
include(joinpath(@__DIR__, "mbjl_plot_script.jl"))
using .MbjlUtil
const color = MbjlColor
const script = MbjlPlotScript

const CLI = joinpath(@__DIR__, "mbjl_plot")
const REPO = normpath(joinpath(@__DIR__, "..", "..", ".."))
const TESTDATA = joinpath(REPO, "test", "utilities", "testdata", "mb21", "TN136HS.309.snipped.mb21")

function check(name, ok, detail = "")
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name" * (!isempty(detail) && !ok ? ": $detail" : ""))
    return ok
end


rgb_fields(l) = split(color.format_echo(l))[2:4]


function unit_tests()
    ok = true
    colors = color.interpolate_palette(1, 11)

    # continuous, unflipped: 10 graded segments, shallow (low) = palette start
    lines = script.build_cpt_plot(colors, 1, false, 3500.0, 175.0)
    ok &= check("continuous: 10 segments", length(lines) == 10)
    ok &= check("continuous: low z gets palette start", rgb_fields(lines[1]) == ["255", "255", "255"])
    ok &= check("continuous: contiguous z", all(a.d2 == b.d1 for (a, b) in zip(lines, lines[2:end])))
    flipped = script.build_cpt_plot(colors, 1, true, 3500.0, 175.0)
    ok &= check("continuous flipped: low z gets palette end", rgb_fields(flipped[1]) == ["37", "57", "175"])

    # discrete: ncolors flat segments, the flipped one keeps all of them
    disc = script.build_cpt_plot(colors, 2, true, 0.0, 1.0)
    ok &= check("discrete flipped: 11 flat segments",
                length(disc) == 11 && all(l.r1 == l.r2 for l in disc))

    # mbhistogram boundaries are consumed in order
    hist = [Float64(v) for v in 0:11]
    ok &= check("stretch boundaries",
                [l.d1 for l in script.build_cpt_plot(colors, 1, false; boundaries = hist)] == hist[1:10])

    # mbm_plot's color interval (EM122 depths 3627.7-5232.4 m, 11 colors)
    start, step = script.compute_color_interval_plot(3627.7341, 5232.4238, 5232.4238, 11)
    ok &= check("color interval", (start, step) == (3500.0, 175.0), "$start $step")

    ok &= check("contour default", script.resolve_contour_control(nothing, 50.0, 2, 11.0) ==
                "50/100000/100000/100000/0.011/0.11")
    ok &= check("contour 2 fields", script.resolve_contour_control("25/100", 50.0, 0, 11.0) ==
                "25/100/100/100/0.01/0.1")
    ok &= check("contour 6 fields keep label height",
                endswith(script.resolve_contour_control("10/50/50/100/0.02/0.2", 50.0, 0, 11.0), "/0.2"))

    sc = script.resolve_shade_control(3; amin = -53.6, amax = -15.0)
    ok &= check("mode 3 default shade from amplitude range", script.as_arg(sc) == "0.05181/-45.88", script.as_arg(sc))
    ok &= check("mode 2 shade flip", script.as_arg(script.resolve_shade_control(2; shade_flip = true)) == "2.5/180")

    xmin, xmax, ymin, ymax = script.expand_bounds(-11.8, -11.55, 36.71, 36.88)
    ok &= check("bounds padded", xmin < -11.8 && xmax > -11.55 && ymin < 36.71 && ymax > 36.88)
    ok &= check("lonflip 1", script.apply_lonflip(-10.0, -5.0, 1) == (350.0, 355.0))
    return ok
end


"""Run a command in `dir`; returns (exitcode, stdout, stderr)."""
function run_in(cmd::Cmd, dir::AbstractString)
    out, err = IOBuffer(), IOBuffer()
    p = run(pipeline(ignorestatus(Cmd(cmd; dir = dir)); stdout = out, stderr = err))
    return p.exitcode, String(take!(out)), String(take!(err))
end


function cli_tests()
    ok = true
    if !isfile(TESTDATA)
        println("  [SKIP] test data not found: $TESTDATA")
        return ok
    end
    # native Windows paths here, as in the Python test: the generated
    # script single-quotes them (backslashes are literal there), and
    # mbdatalist on Windows does not handle a "C:/..." datalist path or
    # entry (it lists no files, or drops the drive letter)
    tmp = mktempdir(; prefix = "mbjl_plot_test_", cleanup = false)
    println("scratch dir: $tmp")
    datalist = joinpath(tmp, "datalist.mb-1")
    write(datalist, "$(TESTDATA) 21\n")

    cases = [
        "nav" => String[],
        "contour" => ["-C", "5", "-N", "0.01/0.05"],
        "fill" => ["-G1", "--coast", "--coast-resolution", "l"],
        "single" => ["-I", TESTDATA, "-G2"],
    ]
    julia = Base.julia_cmd()
    for (tag, extra) in cases
        cmd = `$julia --startup-file=no $CLI -I $datalist -O $tag --format png -X $extra`
        code, _, err = run_in(cmd, tmp)
        ok &= check("$tag: script runs", code == 0, last(err, 400))
        png = joinpath(tmp, "$tag.png")
        ok &= check("$tag: plot written", isfile(png) && filesize(png) > 5000)
        shf = joinpath(tmp, "$tag.sh")
        sh = isfile(shf) ? read(shf, String) : ""
        ok &= check("$tag: modern mode", occursin("gmt begin", sh) && !occursin("-K", sh) && !occursin("psbasemap", sh))
    end
    ok &= check("no gmt.conf left behind", !isfile(joinpath(tmp, "gmt.conf")))
    ok &= check("temporary datalist removed", !isfile(joinpath(tmp, "single_datalist.mb-1")))
    return ok
end


function main()
    ok = unit_tests()
    ok &= cli_tests()
    println(ok ? "ALL OK" : "SOME CHECKS FAILED")
    return ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
