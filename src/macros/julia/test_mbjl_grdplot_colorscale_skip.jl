#!/usr/bin/env julia
#=
Validate the color-scale-bar skip condition (mbm_grdplot lines
~2958-2960, ported as MbjlGrdplotScript._show_colorscale()) end to
end: uniform gray/black/white palettes and seismic-profile grids
should draw a color-filled grid with no colorbar; everything else
should keep it. (Julia port of test_mbpy_grdplot_colorscale_skip.py.)
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


function main()
    # forward slashes throughout: every path below is embedded verbatim
    # in a bash script, where a Windows backslash would be an escape
    tmp = replace(mktempdir(; prefix = "mbjl_grdplot_colorscale_skip_test_", cleanup = false), "\\" => "/")
    println("scratch dir: $tmp")
    all_ok = true

    grid_path = pj(tmp, "geo_wide.grd")
    make_grid(grid_path, "-122.5/-121.5/36.5/37.0", "0.01")
    grid_info = layout.run_grdinfo(grid_path)
    lay = layout.compute_layout(grid_info; pagesize = "a", scale_loc = "b")

    interval = color.compute_color_interval(
        grid_info.zmax - grid_info.zmin, grid_info.zmin, grid_info.zmax,
        color.get_ncolors_use(11, 1), lay.contour_int,
    )

    function make_opts(label, palette, gridprojected_override = nothing)
        if palette in color.SEALEVEL_PALETTES
            izero = color.compute_sealevel_izero(
                11, interval.color_int, interval.color_start, interval.color_end, false
            )
            cptub, cptue = color.interpolate_palette_sealevel(palette, 11, izero)
            cpt_lines = color._write_cpt_continuous_arithmetic(
                cptub, cptue, interval.color_start, interval.color_int, false, 0, nothing, nothing
            )
        else
            colors = color.interpolate_palette(palette, 11)
            cpt_lines = color.build_cpt_continuous(colors, interval.color_start, interval.color_int)
        end
        gi = grid_info
        if gridprojected_override !== nothing
            gi = MbjlUtil.pyreplace(grid_info; gridprojected = gridprojected_override)
        end
        root = pj(tmp, label)
        return script.ScriptOptions(
            grid = grid_path, root = root, layout = lay, grid_info = gi,
            output_format = "png", cpt_lines = cpt_lines, color_palette = palette,
        ), root
    end

    cases = [
        # (label, palette, gridprojected_override, expect_colorbar)
        ("palette1_haxby", 1, nothing, true),
        ("palette4_grayscale", 4, nothing, true),
        ("palette5_uniform_gray", 5, nothing, false),
        ("palette6_uniform_black", 6, nothing, false),
        ("palette7_uniform_white", 7, nothing, false),
        ("palette8_sealevel", 8, nothing, true),
        ("palette10_other", 10, nothing, true),
        ("seismic_profile", 1, 2, false),
    ]

    for (label, palette, gp_override, expect_colorbar) in cases
        println("\n=== case $label ===")
        opts, root = make_opts(label, palette, gp_override)
        script_path = root * ".sh"
        script.generate_and_write(opts, script_path)
        text = read(script_path, String)

        has_colorbar_line = occursin("gmt colorbar", text)
        all_ok &= check("_show_colorscale() == $(expect_colorbar ? "True" : "False")",
                        script._show_colorscale(opts) == expect_colorbar,
                        string(script._show_colorscale(opts)))
        all_ok &= check("colorbar $(expect_colorbar ? "present" : "absent") in generated script",
                        has_colorbar_line == expect_colorbar)

        code, so, se = run_script(script_path)
        all_ok &= check("script runs without error", code == 0, "stdout:\n$so\nstderr:\n$se")
        outfile = root * ".png"
        all_ok &= check("output PNG produced", isfile(outfile))
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
