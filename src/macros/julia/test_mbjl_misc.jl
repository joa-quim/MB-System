#!/usr/bin/env julia
# Validate mbjl_misc.jl's -M sub-option parsing against real
# Perl mbm_grdplot output.
#
# Rather than checking every one of the ~28 sub-flags end-to-end (many
# need real swath-navigation or georeferenced-image files that aren't
# practical to synthesize here), this covers a representative sample
# from each category and confirms the downstream GMT command flag mbm_
# grdplot actually writes matches what parse_misc() extracted.
#
# Julia port of python/test_mbpy_misc.py. Paths handed to perl use
# forward slashes (see test_mbjl_layout.jl), and the xy overlay file is
# named relative to the scratch dir (perl runs there): an absolute
# Windows path "C:/..." carries a drive colon that the ":"-separated -M
# grammar would split on, in mbm_grdplot and parse_misc() alike.

include(joinpath(@__DIR__, "mbjl_misc.jl"))
using .MbjlMisc
using .MbjlUtil

const REPO = normpath(joinpath(@__DIR__, "..", "..", ".."))
const MBM_GRDPLOT = joinpath(REPO, "src", "macros", "mbm_grdplot")

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
    return replace(read(cmdfile, String), "\r\n" => "\n")
end


function check(name, ok, detail = "")
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name" * (!isempty(detail) && !ok ? ": $detail" : ""))
    return ok
end


function main()
    tmp = mktempdir(; prefix = "mbjl_misc_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    grid = joinpath(tmp, "geo_wide.grd")
    make_grid(grid, "-122.5/-121.5/36.5/37.0", "0.01")

    # --- general: -MGD (gmt default override) ---
    println("\n=== case general_gmt_default ===")
    root = joinpath(tmp, "misc_gmt_default")
    cmd_text = run_perl(grid, root, ["-MGDMAP_FRAME_TYPE/plain"])
    opts = parse_misc("GDMAP_FRAME_TYPE/plain")
    all_ok &= check("parsed gmt_defs", opts.general.gmt_defs == ["MAP_FRAME_TYPE/plain"])
    all_ok &= check("gmt gmtset line present in real .cmd",
                    occursin("gmt gmtset MAP_FRAME_TYPE plain", cmd_text))

    # --- general: -MGO (map origin) ---
    println("\n=== case general_map_origin ===")
    root = joinpath(tmp, "misc_origin")
    cmd_text = run_perl(grid, root, ["-MGO2.5/3.5"])
    opts = parse_misc("GO2.5/3.5")
    all_ok &= check("parsed xorigin/yorigin", (opts.general.xorigin, opts.general.yorigin) == (2.5, 3.5))
    m = match(r"^X_OFFSET=(\S+)\nY_OFFSET=(\S+)"m, cmd_text)
    all_ok &= check("X_OFFSET/Y_OFFSET match in real .cmd",
                    m !== nothing && pyfloat(m.captures[1]) == 2.5 && pyfloat(m.captures[2]) == 3.5,
                    m !== nothing ? string(Tuple(m.captures)) : "no match")

    # --- general: -MGT (text label) ---
    println("\n=== case general_text_label ===")
    root = joinpath(tmp, "misc_text")
    spec = "GT1.0/2.0/12/0/1/LB/Hello World"
    cmd_text = run_perl(grid, root, ["-M$spec"])
    opts = parse_misc(spec)
    all_ok &= check("parsed one text label", opts.general.text_labels == ["1.0/2.0/12/0/1/LB/Hello World"])
    all_ok &= check("pstext data line present in real .cmd",
                    occursin("1.0 2.0 12 0 1 LB Hello World", cmd_text))

    # --- contour: -MCA, -MCW (needs -C to enable contour mode) ---
    println("\n=== case contour_annotation_pen ===")
    root = joinpath(tmp, "misc_contour")
    cmd_text = run_perl(grid, root, ["-C", "-MCA10000", "-MCWthick,red"])
    opts = parse_misc("CA10000:CWthick,red")
    all_ok &= check("parsed contour_anot_int/contour_pen",
                    (opts.contour.contour_anot_int, opts.contour.contour_pen) == ("10000", "thick,red"))
    all_ok &= check("-A10000 present in real .cmd", occursin("-A10000", cmd_text))
    all_ok &= check("-Wthick,red present in real .cmd", occursin("-Wthick,red", cmd_text))

    # --- coast: -MTD, -MTG, -MTW ---
    println("\n=== case coast_resolution_dryfill_pen ===")
    root = joinpath(tmp, "misc_coast")
    cmd_text = run_perl(grid, root, ["-MTDf", "-MTGtan", "-MTW2p,black"])
    opts = parse_misc("TDf:TGtan:TW2p,black")
    all_ok &= check(
        "parsed coast_resolution/coast_dryfill/coast_pen",
        (opts.coast.coast_resolution, opts.coast.coast_dryfill, opts.coast.coast_pen) ==
        ("f", "tan", "2p,black"),
    )
    all_ok &= check("-Df present in real .cmd", occursin("-Df", cmd_text))
    all_ok &= check("-Gtan present in real .cmd", occursin("-Gtan", cmd_text))
    all_ok &= check("-W2p,black present in real .cmd", occursin("-W2p,black", cmd_text))

    # --- xy overlay: -MXG/-MXS/-MXW set, then -MXI to commit an entry ---
    println("\n=== case xy_overlay ===")
    write(joinpath(tmp, "points.xy"), "-122.0 36.7\n-121.8 36.8\n")
    xyfile = "points.xy"
    root = joinpath(tmp, "misc_xy")
    cmd_text = run_perl(grid, root, ["-MXGred:XSc0.1:XW1p,blue:XI$xyfile"])
    opts = parse_misc("XGred:XSc0.1:XW1p,blue:XI$xyfile")
    entry = !isempty(opts.xy.entries) ? opts.xy.entries[1] : nothing
    all_ok &= check(
        "parsed one xy entry with fill/symbol/pen set",
        entry !== nothing && entry.file == xyfile &&
        entry.fill == "red" && entry.symbol == "c0.1" && entry.pen == "1p,blue",
        string(entry),
    )
    all_ok &= check("-Gred present in real .cmd", occursin("-Gred", cmd_text))
    all_ok &= check("-Sc0.1 present in real .cmd", occursin("-Sc0.1", cmd_text))
    all_ok &= check("-W1p,blue present in real .cmd", occursin("-W1p,blue", cmd_text))

    # --- xy overlay: default fill/symbol/pen ("N") are omitted from psxy ---
    println("\n=== case xy_overlay_defaults ===")
    root = joinpath(tmp, "misc_xy_default")
    cmd_text = run_perl(grid, root, ["-MXI$xyfile"])
    opts = parse_misc("XI$xyfile")
    entry = !isempty(opts.xy.entries) ? opts.xy.entries[1] : nothing
    all_ok &= check("parsed one xy entry with defaulted N fields",
                    entry !== nothing && (entry.fill, entry.symbol, entry.pen) == ("N", "N", "N"),
                    string(entry))
    all_ok &= check("no -G/-S/-W flags for psxy in real .cmd (all defaulted to N)",
                    !occursin(r"gmt psxy.*?\n(?:.*\\\n)*?.*-[GSW]\S", cmd_text))

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
