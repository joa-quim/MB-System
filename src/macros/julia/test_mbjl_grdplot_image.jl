#!/usr/bin/env julia
#=
End-to-end test of color_mode 6/7 (image plot modes) wired into
mbjl_grdplot_script.jl (Julia port of test_mbpy_grdplot_image.py).

Mode 6 needs a real georeferenced image (its own embedded georeferencing
drives placement); mode 7 needs a plain, non-georeferenced image placed
via explicit -R bounds. Both are built here from a synthetic grid via
`gmt grdimage ... -A<file>` rather than checked into the repo.
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


"""Run `bash script` in the script's directory; returns (exitcode, stdout, stderr)."""
function run_script(script_path)
    out, err = IOBuffer(), IOBuffer()
    p = run(pipeline(ignorestatus(Cmd(`bash $script_path`; dir = dirname(script_path))); stdout = out, stderr = err))
    return p.exitcode, String(take!(out)), String(take!(err))
end


const PNG_MAGIC = UInt8[0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]


"""Mean absolute RGB pixel difference between two PNGs, as (same_shape, shapes, mean_diff):
the Python original uses Pillow + numpy; Julia has neither in its
standard library, so this shells out to Python for the decode when
Pillow/numpy are available there and returns `nothing` otherwise
(the caller then SKIPs the pixel comparison, as the original does
on ImportError)."""
function _mean_abs_pixel_diff(a_path, b_path)
    code = """
import sys
from PIL import Image
import numpy as np
a = np.array(Image.open(sys.argv[1]).convert("RGB"), dtype=int)
b = np.array(Image.open(sys.argv[2]).convert("RGB"), dtype=int)
print(a.shape == b.shape, a.shape, b.shape)
print(np.abs(a - b).mean() if a.shape == b.shape else -1)
"""
    py = Sys.which("python3") !== nothing ? "python3" : Sys.which("python") !== nothing ? "python" : nothing
    py === nothing && return nothing
    out = IOBuffer()
    p = run(pipeline(ignorestatus(`$py -c $code $a_path $b_path`); stdout = out, stderr = devnull))
    p.exitcode == 0 || return nothing
    lines = split(strip(String(take!(out))), "\n")
    same_shape = startswith(lines[1], "True")
    shapes = replace(lines[1], r"^(True|False) " => "")
    return same_shape, shapes, parse(Float64, strip(lines[2]))
end


function main()
    # forward slashes throughout: every path below is embedded verbatim
    # in a bash script, where a Windows backslash would be an escape
    tmp = replace(mktempdir(; prefix = "mbm_grdplot_image_test_", cleanup = false), "\\" => "/")
    println("scratch dir: $tmp")
    all_ok = true

    region = "-122.5/-121.5/36.5/37.0"
    grid_path = pj(tmp, "geo_wide.grd")
    run(pipeline(`gmt grdmath -R$region -I0.01 X Y MUL 1000 MUL = $grid_path`; stdout = devnull, stderr = devnull))
    grid_info = layout.run_grdinfo(grid_path)
    lay_geo = layout.compute_layout(grid_info; pagesize = "a", scale_loc = "b")

    interval = color.compute_color_interval(
        grid_info.zmax - grid_info.zmin, grid_info.zmin, grid_info.zmax,
        color.get_ncolors_use(11, 1), lay_geo.contour_int,
    )
    colors = color.interpolate_palette(1, 11)
    cpt_lines = color.build_cpt_continuous(colors, interval.color_start, interval.color_int)
    cpt_path = pj(tmp, "src.cpt")
    script.write_cpt_file(cpt_path, cpt_lines)

    # --- mode 6: a real georeferenced GeoTIFF (embedded georeferencing) ---
    println("\n=== case mode6_embedded_georeferencing ===")
    geotiff_path = pj(tmp, "georef.tif")
    run(pipeline(`gmt grdimage $grid_path -C$cpt_path -A$geotiff_path`; stdout = devnull, stderr = devnull))
    all_ok &= check("GeoTIFF fixture created", isfile(geotiff_path))

    image_info = layout.run_grdinfo(geotiff_path)
    lay6 = layout.compute_layout(image_info; pagesize = "a", scale_loc = "b")
    root6 = pj(tmp, "mode6")
    opts6 = script.ScriptOptions(
        grid = geotiff_path, root = root6, layout = lay6, grid_info = image_info,
        output_format = "png", image_mode = 6, title = "Embedded Georeferencing",
    )
    script_path6 = root6 * ".sh"
    script.generate_and_write(opts6, script_path6)
    text6 = read(script_path6, String)
    all_ok &= check("no -C or -I flags for grdimage (image, not grid+CPT)",
                    !occursin(" -C", text6) && !occursin(" -I", text6))
    all_ok &= check("no -D/-Dr flag (auto-detect embedded georeferencing)",
                    !occursin(" -D", text6))
    all_ok &= check("no color scale bar (image has no scalar z)", !occursin("gmt colorbar", text6))

    code6, so6, se6 = run_script(script_path6)
    all_ok &= check("mode 6 script runs without error", code6 == 0, "stdout:\n$so6\nstderr:\n$se6")
    out6 = root6 * ".png"
    all_ok &= check("mode 6 output PNG produced", isfile(out6))
    if isfile(out6)
        data = read(out6)
        all_ok &= check("mode 6 output has a real PNG header", data[1:8] == PNG_MAGIC)
        all_ok &= check("mode 6 output is non-trivially sized (>2KB)", length(data) > 2000,
                        "$(length(data)) bytes")
    end

    # --- mode 7: a plain, non-georeferenced image placed via -R ---
    println("\n=== case mode7_specified_bounds ===")
    plain_png_path = pj(tmp, "plain.png")
    run(pipeline(`gmt grdimage $grid_path -C$cpt_path -A$plain_png_path`; stdout = devnull, stderr = devnull))
    worldfile = pj(tmp, "plain.pgw")
    if isfile(worldfile)
        rm(worldfile)  # strip any georeferencing GMT wrote alongside it
    end
    all_ok &= check("plain PNG fixture created with no world file",
                    isfile(plain_png_path) && !isfile(worldfile))

    root7 = pj(tmp, "mode7")
    opts7 = script.ScriptOptions(
        # reuse mode 6's own layout/grid_info (not lay_geo/grid_info from
        # the source grid) so both cases render the exact same map
        # extent/size -- any leftover mismatch is then a real content
        # bug (like the grdedit band-collapse this test caught), not
        # just harmless pixel-registration noise between two
        # independently-computed Layouts.
        grid = plain_png_path, root = root7, layout = lay6, grid_info = image_info,
        output_format = "png", image_mode = 7, title = "Specified Bounds",
    )
    script_path7 = root7 * ".sh"
    script.generate_and_write(opts7, script_path7)
    text7 = read(script_path7, String)
    # mbm_grdplot passes -Dr here, but that hits a real GMT 6.7.0 bug
    # under a non-Cartesian projection (confirmed directly against a
    # real GMT install before writing this workaround: identical
    # "gmt_img_project: Input image does not have sufficient (2)
    # padding" error mode 6's own -D fix was for) -- so this generator
    # instead pre-attaches -R as if it were real georeferencing via
    # `gdal_translate -a_ullr` (NOT `gmt grdedit`, which was tried first
    # and rejected: confirmed directly that it silently collapses a
    # 3-band RGB image to one grayscale band, which grdimage then
    # renders through its own default rainbow CPT instead of the
    # image's real colors), then treats the result like mode 6 (no
    # -D/-Dr at all needed once the image "has" georeferencing).
    all_ok &= check("no -Dr (worked around instead, see comment)", !occursin("-Dr", text7))
    all_ok &= check("gdal_translate present (the -Dr workaround)", occursin("gdal_translate", text7))
    all_ok &= check("no color scale bar", !occursin("gmt colorbar", text7))

    code7, so7, se7 = run_script(script_path7)
    all_ok &= check("mode 7 script runs without error", code7 == 0, "stdout:\n$so7\nstderr:\n$se7")
    out7 = root7 * ".png"
    all_ok &= check("mode 7 output PNG produced", isfile(out7))
    if isfile(out7)
        data = read(out7)
        all_ok &= check("mode 7 output has a real PNG header", data[1:8] == PNG_MAGIC)
        all_ok &= check("mode 7 output is non-trivially sized (>2KB)", length(data) > 2000,
                        "$(length(data)) bytes")
    end
    leftover_georef = filter(isfile, [pj(tmp, "mode7_georef.tif")])
    all_ok &= check("temporary re-georeferenced image cleaned up", isempty(leftover_georef),
                    string(leftover_georef))

    # mode 6 and mode 7 render the identical underlying colorized data
    # (same grid, same CPT, same region/projection) through two
    # different code paths -- their output should match closely. This
    # is what actually would have caught the grdedit band-collapse bug:
    # that render was structurally fine (ran, produced a properly
    # sized/placed PNG) but silently wrong in content.
    println("\n=== case mode6_vs_mode7_pixel_match ===")
    if isfile(out6) && isfile(out7)
        res = _mean_abs_pixel_diff(out6, out7)
        if res === nothing
            println("  [SKIP] Pillow/numpy not available; skipping pixel comparison")
        else
            same_shape, shapes, mean_diff = res
            all_ok &= check("mode 6 and mode 7 outputs are the same size", same_shape, shapes)
            if same_shape
                all_ok &= check("mode 6 and mode 7 outputs match closely (mean diff < 5)",
                                mean_diff < 5, "mean diff = $mean_diff")
            end
        end
    end

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
