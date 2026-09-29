#!/usr/bin/env julia
#=
End-to-end test of the shaded-relief / slope-magnitude color modes
(2-5) wired into mbjl_grdplot_script.jl (Julia port of
test_mbpy_grdplot_shade.py).

Same approach as test_mbjl_grdplot_script.jl: build up real Layout/CPT
objects, generate a script for each color_mode, run it through a real
GMT installation, and check it produces a valid, non-trivial output
file. This does not (and cannot easily) compare pixel-for-pixel
against mbm_grdplot's own classic-mode output, but the grdgradient/
grdmath pipeline for each mode is a line-for-line port of mbm_grdplot's
own shell commands (see mbjl_grdplot_script.jl's helper docstrings), so
a passing run here confirms that pipeline is both syntactically valid
GMT and produces real image content.
=#
include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))
const layout = MbjlLayout
const color = MbjlColor
const script = MbjlGrdplotScript

pj(a, b) = a * "/" * b


function check(name, ok, detail = "")
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name" * (!isempty(detail) && !ok ? ": $detail" : ""))
    return ok
end


function make_grid(path, region, inc = "0.01")
    run(pipeline(`gmt grdmath -R$region -I$inc X Y MUL 1000 MUL = $path`; stdout = devnull, stderr = devnull))
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
    tmp = replace(mktempdir(; prefix = "mbm_grdplot_shade_test_", cleanup = false), "\\" => "/")
    println("scratch dir: $tmp")
    all_ok = true

    grid_path = pj(tmp, "geo_wide.grd")
    make_grid(grid_path, "-122.5/-121.5/36.5/37.0", "0.01")

    grid_info = layout.run_grdinfo(grid_path)
    lay = layout.compute_layout(grid_info; pagesize = "a", scale_loc = "b")

    ncolors = 11
    interval = color.compute_color_interval(
        grid_info.zmax - grid_info.zmin, grid_info.zmin, grid_info.zmax,
        color.get_ncolors_use(ncolors, 1), lay.contour_int,
    )
    colors = color.interpolate_palette(1, ncolors)
    normal_cpt = color.build_cpt_continuous(colors, interval.color_start, interval.color_int)

    # mode 4 needs its own slope-magnitude CPT (0..magnitude ramp)
    magnitude4 = 1.0
    slope_boundaries = color.build_slope_boundaries(magnitude4, ncolors, 1)
    slope_cpt = color.build_cpt_continuous_from_boundaries(colors, slope_boundaries; color_flip = false)

    # a synthetic external intensity grid for mode 3
    intensity_path = pj(tmp, "intensity.grd")
    run(pipeline(`gmt grdgradient $grid_path -A45 -Ne0.6 -G$intensity_path`; stdout = devnull, stderr = devnull))

    cases = [
        ("mode2_shaded_relief", script.ShadeOptions(color_mode = 2), normal_cpt),
        ("mode3_intensity_raw", script.ShadeOptions(
            color_mode = 3, file_intensity = intensity_path, stretch_shade = false), normal_cpt),
        ("mode3_intensity_stretched", script.ShadeOptions(
            color_mode = 3, file_intensity = intensity_path, stretch_shade = true), normal_cpt),
        ("mode4_slope_fill", script.ShadeOptions(color_mode = 4, magnitude = magnitude4), slope_cpt),
        ("mode5_slope_shaded", script.ShadeOptions(color_mode = 5, magnitude = 1.0), normal_cpt),
        ("mode2_flipped", script.ShadeOptions(color_mode = 2, shade_flip = true), normal_cpt),
    ]

    for (label, shade, cpt_lines) in cases
        println("\n=== case $label ===")
        root = pj(tmp, label)
        opts = script.ScriptOptions(
            grid = grid_path, root = root, layout = lay, grid_info = grid_info,
            output_format = "png", shade = shade, cpt_lines = cpt_lines,
        )
        script_path = root * ".sh"
        script.generate_and_write(opts, script_path)

        script_text = read(script_path, String)
        if shade.color_mode == 3
            all_ok &= check("no gmt grdgradient (mode 3 uses an external intensity file)",
                            !occursin("gmt grdgradient", script_text))
            all_ok &= check("gmt grdhisteq present iff stretch_shade",
                            occursin("gmt grdhisteq", script_text) == shade.stretch_shade)
        else
            all_ok &= check("gmt grdgradient present", occursin("gmt grdgradient", script_text))
        end

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

        # mbm_grdplot deletes the color_mode 4/5 slope grid at the end
        # but -- a real, faithfully-preserved wart, not "fixed" here --
        # never cleans up the color_mode 2/3 intensity (".int") grid,
        # so only check for the former.
        leftover_drv = filter(isfile, [pj(tmp, "$(label)_drvx.grd"), pj(tmp, "$(label)_drvy.grd")])
        all_ok &= check("gradient-component intermediates cleaned up", isempty(leftover_drv),
                        string(leftover_drv))
        leftover_slope = filter(isfile, [pj(tmp, "$(label)_slope.grd")])
        all_ok &= check("slope grid cleaned up if this mode made one", isempty(leftover_slope),
                        string(leftover_slope))
    end

    # verify resolve_shade_defaults() matches mbm_grdplot's own defaulting
    println("\n=== case shade_defaults ===")
    d2 = script.resolve_shade_defaults(script.ShadeOptions(color_mode = 2))
    all_ok &= check("mode 2 defaults", (d2.azimuth, d2.magnitude, d2.elevation) == (0.0, 1.0, 30.0),
                    string((d2.azimuth, d2.magnitude, d2.elevation)))
    d3 = script.resolve_shade_defaults(script.ShadeOptions(color_mode = 3))
    all_ok &= check("mode 3 default magnitude -0.4", d3.magnitude == -0.4, string(d3.magnitude))
    d4 = script.resolve_shade_defaults(script.ShadeOptions(color_mode = 4))
    all_ok &= check("mode 4 default magnitude 1.0", d4.magnitude == 1.0, string(d4.magnitude))
    d2f = script.resolve_shade_defaults(script.ShadeOptions(color_mode = 2, shade_flip = true))
    all_ok &= check("shade_flip negates magnitude", d2f.magnitude == -1.0, string(d2f.magnitude))

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
