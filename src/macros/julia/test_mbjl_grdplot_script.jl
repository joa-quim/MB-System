#!/usr/bin/env julia
#=
End-to-end test of mbjl_grdplot_script.jl (Julia port of
test_mbpy_grdplot_script.py): wire mbjl_layout / mbjl_color /
mbjl_misc together, generate a GMT modern-mode script, actually run it
through a real GMT installation, and check it produces a valid output
file.

Unlike the other test_mbjl_grdplot_*.jl scripts, this does not compare
against real Perl mbm_grdplot output line-for-line (a modern-mode
script is structurally different from mbm_grdplot's classic-mode one
by design -- see mbjl_grdplot_script.jl's module header). Instead it
verifies the generated script is actually runnable GMT and produces
sane output.
=#
include(joinpath(@__DIR__, "mbjl_grdplot_script.jl"))
const layout = MbjlLayout
const color = MbjlColor
const misc = MbjlMisc
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
    tmp = replace(mktempdir(; prefix = "mbjl_grdplot_script_test_", cleanup = false), "\\" => "/")
    println("scratch dir: $tmp")
    all_ok = true

    grid_path = pj(tmp, "geo_wide.grd")
    make_grid(grid_path, "-122.5/-121.5/36.5/37.0", "0.01")

    xy_path = pj(tmp, "points.xy")
    write(xy_path, "-122.0 36.7\n-121.8 36.8\n-121.9 36.6\n")

    grid_info = layout.run_grdinfo(grid_path)
    lay = layout.compute_layout(grid_info; pagesize = "a", scale_loc = "b")

    ncolors = 11
    interval = color.compute_color_interval(
        grid_info.zmax - grid_info.zmin, grid_info.zmin, grid_info.zmax,
        color.get_ncolors_use(ncolors, 1), lay.contour_int,
    )
    colors = color.interpolate_palette(1, ncolors)
    cpt_lines = color.build_cpt_continuous(colors, interval.color_start, interval.color_int)

    # --- case 1: full-featured plot (color fill + contour + coast + xy + text + title) ---
    println("\n=== case full_featured ===")
    root1 = pj(tmp, "full")
    opts1 = script.ScriptOptions(
        grid = grid_path,
        root = root1,
        layout = lay,
        grid_info = grid_info,
        output_format = "pdf",
        title = "Test Plot",
        cpt_lines = cpt_lines,
        contour = misc.ContourOptions(contour_pen = "0.5p,black"),
        coast = misc.CoastOptions(coast_control = true, coast_resolution = "i", coast_dryfill = "gray"),
        xy_entries = [misc.XYOverlayEntry(
            file = xy_path, symbol = "c0.1", fill = "red", segment = "N", segchar = ">", pen = "N"
        )],
        text_labels = ["-122.2/36.6/12/0/1/LB/Monterey Bay"],
    )
    script_path1 = pj(tmp, "full.sh")
    script.generate_and_write(opts1, script_path1)

    all_ok &= check("cpt file written", isfile(pj(tmp, "full.cpt")))
    all_ok &= check("script file written and executable",
                    # Sys.isexecutable, not a raw filemode() & 0o111 test (the
                    # Python original's st_mode check): NTFS has no execute
                    # bit, so on Windows filemode() always reports 0o666
                    # even after chmod 0o755 and the raw test always fails
                    isfile(script_path1) && Sys.isexecutable(script_path1))

    script_text = read(script_path1, String)
    all_ok &= check("modern-mode begin/end framing present",
                    occursin("gmt begin $root1 pdf", script_text) && endswith(strip(script_text), "gmt end"))
    all_ok &= check("no classic-mode -K/-O flags anywhere", !occursin(" -K", script_text) && !occursin(" -O ", script_text))
    # a real bug once here: PS_PAGE_ORIENTATION LANDSCAPE (copied
    # verbatim from mbm_grdplot's classic-mode baseline) rotated modern
    # mode's auto-sized output 90 degrees, and PS_MEDIA forced a full
    # physical-page canvas with large blank margins around it -- both
    # confirmed against real mbm_grdplot output before being removed
    # (see baseline_gmt_defaults()'s docstring).
    all_ok &= check("no PS_PAGE_ORIENTATION (rotates modern-mode output)",
                    !occursin("PS_PAGE_ORIENTATION", script_text))
    all_ok &= check("no PS_MEDIA (forces an oversized canvas in modern mode)",
                    !occursin("PS_MEDIA", script_text))
    all_ok &= check("grdimage + colorbar present", occursin("gmt grdimage", script_text) && occursin("gmt colorbar", script_text))
    # a real bug once here: `gmt basemap`'s bare -B<interval> (no axes-side
    # selector) only annotates whichever frame sides grdimage's earlier
    # implicit frame hadn't already claimed, in GMT modern mode -- silently
    # dropping lat/lon tick labels from two of the four sides. -BWESN forces
    # all four sides regardless of what ran before it (see _basemap_axes()'s
    # docstring; confirmed by direct comparison against real mbm_grdplot
    # classic-mode reference output, which always annotates all four sides).
    all_ok &= check("basemap forces all four sides annotated (-BWESN)",
                    occursin("gmt basemap", script_text) && occursin("-BWESN", script_text))
    # a real bug once here: the colorbar -D used both colorscale_offx AND
    # colorscale_offy together, but mbm_grdplot hardcodes whichever one
    # doesn't apply to the bar's orientation to 0 (see module header).
    # scale_loc "b" (this case's default) is horizontal: X must be 0.
    m = match(r"gmt colorbar \S+ -Dx(\S+?)/(\S+?)\+h", script_text)
    all_ok &= check("horizontal colorbar has X hardcoded to 0",
                    m !== nothing && m.captures[1] == "0", m !== nothing ? string(Tuple(m.captures)) : "nothing")
    all_ok &= check("grdcontour present", occursin("gmt grdcontour", script_text))
    # a real bug once here: grdcontour was missing -C entirely, so every
    # contour silently fell back to GMT's own auto levels instead of the
    # requested/heuristic interval -- see mbjl_grdplot_script.jl's module
    # header. Check the flag is actually present with a real value,
    # not just that the command line exists.
    m = match(r"gmt grdcontour \S+ -J\S+ -R\S+ -C(\S+)", script_text)
    all_ok &= check("grdcontour has a real -C interval (defaulted from layout.contour_int)",
                    m !== nothing && parse(Float64, m.captures[1]) == lay.contour_int,
                    "match=$(m !== nothing ? m.match : nothing), expected contour_int=$(lay.contour_int)")
    all_ok &= check("coast present with -Di -Ggray", occursin("-Di", script_text) && occursin("-Ggray", script_text))
    all_ok &= check("xy plot present with -Sc0.1 -Gred", occursin("-Sc0.1", script_text) && occursin("-Gred", script_text))
    all_ok &= check("text label heredoc present", occursin("gmt text", script_text) && occursin("Monterey Bay", script_text))

    code, so, se = run_script(script_path1)
    all_ok &= check("script runs without error", code == 0, "stdout:\n$so\nstderr:\n$se")
    outfile = root1 * ".pdf"
    all_ok &= check("output PDF produced", isfile(outfile))
    if isfile(outfile)
        data = read(outfile)
        all_ok &= check("output PDF has a real PDF header", data[1:5] == Vector{UInt8}("%PDF-"))
        all_ok &= check("output PDF is non-trivially sized (>5KB)", length(data) > 5000,
                        "$(length(data)) bytes")
    end

    # --- case 2: minimal plot (color fill only, no overlays) ---
    println("\n=== case minimal ===")
    root2 = pj(tmp, "minimal")
    opts2 = script.ScriptOptions(
        grid = grid_path, root = root2, layout = lay, grid_info = grid_info,
        output_format = "png", cpt_lines = cpt_lines,
    )
    script_path2 = pj(tmp, "minimal.sh")
    script.generate_and_write(opts2, script_path2)
    code2, so2, se2 = run_script(script_path2)
    all_ok &= check("minimal script runs without error", code2 == 0, "stdout:\n$so2\nstderr:\n$se2")
    outfile2 = root2 * ".png"
    all_ok &= check("minimal output PNG produced", isfile(outfile2))
    if isfile(outfile2)
        all_ok &= check("minimal output PNG has a real PNG header",
                        read(outfile2)[1:8] == UInt8[0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])
    end

    # --- case 3: explicit contour_interval overrides the layout.contour_int default ---
    println("\n=== case explicit_contour_interval ===")
    root3 = pj(tmp, "explicit_contour")
    opts3 = script.ScriptOptions(
        grid = grid_path, root = root3, layout = lay, grid_info = grid_info,
        output_format = "png", cpt_lines = cpt_lines,
        contour = misc.ContourOptions(), contour_interval = "12345",
    )
    script_path3 = pj(tmp, "explicit_contour.sh")
    script.generate_and_write(opts3, script_path3)
    text3 = read(script_path3, String)
    all_ok &= check("explicit contour_interval used verbatim", occursin("-C12345", text3), text3)
    code3, so3, se3 = run_script(script_path3)
    all_ok &= check("explicit-interval script runs without error", code3 == 0, "stdout:\n$so3\nstderr:\n$se3")

    # --- case 4: vertical (scale_loc "l") colorbar -- Y must be hardcoded to 0 ---
    println("\n=== case vertical_colorbar ===")
    lay_left = layout.compute_layout(grid_info; pagesize = "a", scale_loc = "l")
    interval_left = color.compute_color_interval(
        grid_info.zmax - grid_info.zmin, grid_info.zmin, grid_info.zmax,
        color.get_ncolors_use(ncolors, 1), lay_left.contour_int,
    )
    cpt_lines_left = color.build_cpt_continuous(colors, interval_left.color_start, interval_left.color_int)
    root4 = pj(tmp, "vertical_colorbar")
    opts4 = script.ScriptOptions(
        grid = grid_path, root = root4, layout = lay_left, grid_info = grid_info,
        output_format = "png", cpt_lines = cpt_lines_left,
    )
    script_path4 = pj(tmp, "vertical_colorbar.sh")
    script.generate_and_write(opts4, script_path4)
    text4 = read(script_path4, String)
    m4 = match(r"gmt colorbar \S+ -Dx(\S+?)/(\S+?)\+v", text4)
    all_ok &= check("vertical colorbar has Y hardcoded to 0",
                    m4 !== nothing && m4.captures[2] == "0", m4 !== nothing ? string(Tuple(m4.captures)) : "nothing")
    code4, so4, se4 = run_script(script_path4)
    all_ok &= check("vertical-colorbar script runs without error", code4 == 0, "stdout:\n$so4\nstderr:\n$se4")

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
