# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbpreprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbpreprocess")

@testset "mbpreprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 3
		@test occursin("MBIO Error returned from function", output)
		@test occursin("Illegal format identifier", output)
		@test occursin("Multibeam File <> not initialized for reading", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("handles preprocessing of swath sonar data", output)
		@test occursin("--platform-file=platform_file", output)
		@test occursin("--kluge-fix-wissl-timestamps", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "--help", "--verbose", "--verbose"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("handles preprocessing of swath sonar data", output)
		@test occursin("--platform-file=platform_file", output)
		@test occursin("--kluge-fix-wissl-timestamps", output)
	end

	# Old Simrad formats (51, 53, 54) are written by mbpreprocess as format 57; check that the
	# conversion runs and that the output reads back fully (see github issue #1626)
	function check_old_simrad_to_format57(src_dir, src_name, iformat, records, good_beams)
		cmd = abspath(CMD)
		mbinfo = abspath(mbprog("utilities", "mbinfo"))
		src = abspath(joinpath("testdata", src_dir, src_name))
		env = mbenv()
		env["PATH"] = join([dirname(cmd), get(env, "PATH", "")], Sys.iswindows() ? ';' : ':')
		mktempdir() do tmpdir
			cp(src, joinpath(tmpdir, src_name))
			cd(tmpdir) do
				mbcheck([cmd, "--input=" * src_name, "--format=$iformat"]; stderr_to_stdout = true, env)
				ofile = splitext(src_name)[1] * ".mb57"
				@test isfile(ofile)
				output = unixlines(mbcheck([mbinfo, "-F57", "-I" * ofile]; stderr_to_stdout = true, env))
				@test occursin(Regex("Number of Records: +$records
"), output)
				@test occursin(Regex("Number of Good Beams: +$good_beams "), output)
			end
		end
	end

	@testset "OldSimradFormat51" begin
		check_old_simrad_to_format57("mb51", "tahoe_98_134.0.em1000.mb51", 51, 2, 120)
	end

	@testset "OldSimradFormat54" begin
		check_old_simrad_to_format57("mb54", "TN136HS.309.snipped.mb54", 54, 2, 115)
	end
end
