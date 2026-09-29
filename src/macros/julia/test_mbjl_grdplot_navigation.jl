#!/usr/bin/env julia
#=
End-to-end test of the swath-navigation/ping-tick overlay
(color_mode-independent -- it layers onto any of them) wired into
mbjl_grdplot_script.jl via MB-System's own `gmt mbcontour` GMT
supplement module (Julia port of test_mbpy_grdplot_navigation.py).

Uses a real (small, "snipped") MB-System swath test data file already
in the repo, referenced through a one-line datalist, exactly the way
mbm_grdplot itself expects -I<swathnavdatalist> to be structured.
=#
include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))
const layout = MbjlLayout
const color = MbjlColor
const misc = MbjlMisc
const script = MbjlGrdplotScript

pj(a, b) = a * "/" * b

const REPO = replace(normpath(joinpath(@__DIR__, "..", "..", "..")), "\\" => "/")
const TESTDATA = pj(REPO, "test/utilities/testdata/mb21/TN136HS.309.snipped.mb21")


function check(name, ok, detail = "")
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name" * (!isempty(detail) && !ok ? ": $detail" : ""))
    return ok
end


"""Run `bash script` in the script's directory; returns (exitcode, stdout, stderr)."""
function run_script(script_path)
    out, err = IOBuffer(), IOBuffer()
    p = run(pipeline(ignorestatus(Cmd(`bash $script_path`; dir = dirname(script_path))); stdout = out, stderr = err))
    return p.exitcode, String(take!(out)), String(take!(err))
end


const PNG_MAGIC = UInt8[0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]


function main()
    # forward slashes throughout: every path below is embedded verbatim
    # in a bash script, where a Windows backslash would be an escape
    tmp = replace(mktempdir(; prefix = "mbm_grdplot_navigation_test_", cleanup = false), "\\" => "/")
    println("scratch dir: $tmp")
    all_ok = true

    if !isfile(TESTDATA)
        println("  [SKIP] test data not found: $TESTDATA")
        return 0
    end

    datalist = pj(tmp, "datalist.mb-1")
    write(datalist, "$TESTDATA 21\n")

    # a synthetic backdrop grid covering the swath file's own bounds
    region = "-124.51/-124.49/40.83/40.85"
    grid_path = pj(tmp, "backdrop.grd")
    run(pipeline(`gmt grdmath -R$region -I0.001 X Y MUL 1000 MUL = $grid_path`; stdout = devnull, stderr = devnull))
    grid_info = layout.run_grdinfo(grid_path)
    lay = layout.compute_layout(grid_info; pagesize = "a", scale_loc = "b")

    interval = color.compute_color_interval(
        grid_info.zmax - grid_info.zmin, grid_info.zmin, grid_info.zmax,
        color.get_ncolors_use(11, 1), lay.contour_int,
    )
    colors = color.interpolate_palette(1, 11)
    cpt_lines = color.build_cpt_continuous(colors, interval.color_start, interval.color_int)

    println("\n=== case resolve_swath_format ===")
    fmt = misc.resolve_swath_format(datalist)
    all_ok &= check("auto-detected format is -1 (a datalist)", fmt == "-1", repr(fmt))

    println("\n=== case resolve_navigation_control ===")
    nav_raw = misc.NavigationOptions(swathnavdatalist = datalist, navigation_control = "50/100")
    nav = misc.resolve_navigation_control(nav_raw, true)
    all_ok &= check("2-field form normalized to 4 fields",
                    nav.navigation_control == "50/100/100000/0.15", string(nav.navigation_control))
    nav_fp = misc.resolve_navigation_control(
        misc.NavigationOptions(swathnavdatalist = datalist, navigation_control = "FP"),
        true,
    )
    all_ok &= check("FP keyword sets name_mode+name_perp and fixed control",
                    nav_fp.name_mode && nav_fp.name_perp &&
                    nav_fp.navigation_control == "0.25/1/4/0.15", string(nav_fp))
    nav_ping = misc.resolve_navigation_control(
        misc.NavigationOptions(
            swathnavdatalist = datalist, pingnumber_mode = true,
            pingnumber_tick = 50, pingnumber_annot = 100, pingnumber_tick_len = 0.1,
        ),
        true,
    )
    all_ok &= check("pingnumber_control rebuilt from tick/annot/ticklen",
                    nav_ping.pingnumber_control == "50/100/0.1", string(nav_ping.pingnumber_control))

    println("\n=== case full_navigation_overlay ===")
    nav_opts = misc.resolve_navigation_control(
        misc.NavigationOptions(
            swathnavdatalist = datalist,
            swathformat = fmt,
            navigation_control = "0.25/1/4/0.15",
            pingnumber_mode = true, pingnumber_tick = 50, pingnumber_annot = 100, pingnumber_tick_len = 0.1,
            nav_pen = "1p,red",
        ),
        true,
    )
    root = pj(tmp, "navplot")
    opts = script.ScriptOptions(
        grid = grid_path, root = root, layout = lay, grid_info = grid_info,
        output_format = "png", cpt_lines = cpt_lines, navigation = nav_opts,
        title = "Swath Navigation Overlay",
    )
    script_path = root * ".sh"
    script.generate_and_write(opts, script_path)

    text = read(script_path, String)
    all_ok &= check("gmt mbcontour present", occursin("gmt mbcontour", text))
    all_ok &= check("-F-1 (datalist format) present", occursin("-F-1", text))
    all_ok &= check("-D (navigation ticks) present", occursin("-D0.25/1/4/0.15", text))
    all_ok &= check("-M (ping numbers) present", occursin("-M50/100/0.1", text))
    all_ok &= check("-W (nav pen) present", occursin("-W1p,red", text))

    code, so, se = run_script(script_path)
    all_ok &= check("script runs without error", code == 0, "stdout:\n$so\nstderr:\n$se")
    outfile = root * ".png"
    all_ok &= check("output PNG produced", isfile(outfile))
    if isfile(outfile)
        data = read(outfile)
        all_ok &= check("output PNG has a real PNG header", data[1:8] == PNG_MAGIC)
        all_ok &= check("output PNG is non-trivially sized (>2KB)", length(data) > 2000,
                        "$(length(data)) bytes")
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
