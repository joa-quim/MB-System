#!/usr/bin/env julia
# Validate resolve_color_control() (the -W<color_control> parser) in
# mbjl_color.jl against the real Perl mbm_grdplot.
#
# Unlike the other test_mbjl_*.jl scripts, this one is mostly
# pure option-parsing logic and doesn't need to compare generated CPT
# content -- it checks that:
#   - an existing file path bypasses CPT generation entirely (CPT_FILE
#     points straight at it, no "echo ... >> $CPT_FILE" lines, and the
#     file is not deleted at cleanup), and
#   - the "style/palette/ncolors", "style/palette", and bare "style"
#     numeric forms parse (and clamp out-of-range values) the same way
#     mbm_grdplot's own -W option parsing does.
#
# Julia port of python/test_mbpy_colorcontrol.py. Paths handed to perl
# (and to resolve_color_control, so the two can be compared) use
# forward slashes -- see test_mbjl_layout.jl.

include(joinpath(@__DIR__, "mbjl_color.jl"))
using .MbjlColor
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
    return read(cmdfile, String)
end


function check(name, ok, detail = "")
    status = ok ? "OK  " : "FAIL"
    println("  [$status] $name" * (!isempty(detail) && !ok ? ": $detail" : ""))
    return ok
end


function main()
    tmp = mktempdir(; prefix = "mbjl_colorcontrol_test_", cleanup = false)
    println("scratch dir: $tmp")
    all_ok = true

    grid = joinpath(tmp, "geo_wide.grd")
    make_grid(grid, "-122.5/-121.5/36.5/37.0", "0.01")

    # --- existing-file bypass ---
    println("\n=== case existing_file_bypass ===")
    custom_cpt = fwd(joinpath(tmp, "custom.cpt"))
    write(custom_cpt,
        "-5000 255 0 0 -2500 0 255 0\n" *
        "-2500 0 255 0 0 0 0 255\n"
    )
    root = joinpath(tmp, "customcpt")
    cmd_text = run_perl(grid, root, ["-W$custom_cpt"])

    m = match(r"^CPT_FILE=(.*)$"m, cmd_text)
    perl_cpt_file = m !== nothing ? String(rstrip(m.captures[1], '\r')) : nothing
    has_generation_lines = occursin(r"^echo\s+\S.*\$CPT_FILE\s*$"m, cmd_text)
    has_cleanup_rm = occursin("rm -f \$CPT_FILE", cmd_text)

    cc = resolve_color_control(custom_cpt)
    all_ok &= check("CPT_FILE points at the given file",
                    perl_cpt_file == custom_cpt,
                    "perl CPT_FILE=$(repr(perl_cpt_file)) vs given $(repr(custom_cpt))")
    all_ok &= check("no CPT-content generation lines in the real .cmd", !has_generation_lines)
    all_ok &= check("no cleanup rm of \$CPT_FILE in the real .cmd", !has_cleanup_rm)
    all_ok &= check("resolve_color_control reports file_cpt", cc.file_cpt == custom_cpt)
    all_ok &= check("custom.cpt still exists after running the script", isfile(custom_cpt))

    # --- numeric forms: cross-check against the CPT content mbm_grdplot
    #     actually generates, which only depends on style/palette/ncolors ---
    println("\n=== case numeric forms (cross-checked via generated CPT ncolors) ===")
    numeric_cases = [
        ("1/2/6", 1, 2, 6),
        ("2/3", 2, 3, NCPT),
        ("1", 1, 1, NCPT),
        ("1/0/4", 1, 1, 4),      # palette 0 out of range -> clamps to 1
        ("1/2/1", 1, 2, 2),      # ncolors 1 out of range -> clamps to 2
        ("1/99/5", 1, 1, 5),     # palette 99 out of range -> clamps to 1
    ]
    for (spec, exp_style, exp_palette, exp_ncolors) in numeric_cases
        cc = resolve_color_control(spec)
        ok = (cc.file_cpt === nothing && cc.color_style == exp_style &&
              cc.color_palette == exp_palette && cc.ncolors == exp_ncolors)
        all_ok &= check(
            "-W$spec", ok,
            "got file_cpt=$(pystr(cc.file_cpt)) style=$(cc.color_style) " *
            "palette=$(cc.color_palette) ncolors=$(cc.ncolors)"
        )
    end

    # spot-check one clamped case end-to-end against real Perl output:
    # -W1/99/5 should behave exactly like -W1/1/5 (palette clamped to 1)
    println("\n=== case palette_clamp_end_to_end ===")
    root_a = joinpath(tmp, "clamp_a")
    root_b = joinpath(tmp, "clamp_b")
    cmd_a = run_perl(grid, root_a, ["-W1/99/5"])
    cmd_b = run_perl(grid, root_b, ["-W1/1/5"])
    cpt_line_re = r"^echo\s+(\S.*\S)\s+>>?\s+\$CPT_FILE\s*$"m
    lines_a = [String(m.captures[1]) for m in eachmatch(cpt_line_re, replace(cmd_a, "\r\n" => "\n"))]
    lines_b = [String(m.captures[1]) for m in eachmatch(cpt_line_re, replace(cmd_b, "\r\n" => "\n"))]
    all_ok &= check("out-of-range palette 99 clamps to palette 1 (matches real Perl)",
                    lines_a == lines_b, "$lines_a vs $lines_b")

    println("\n" * (all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED"))
    return all_ok ? 0 : 1
end


if abspath(PROGRAM_FILE) == @__FILE__
    exit(main())
end
