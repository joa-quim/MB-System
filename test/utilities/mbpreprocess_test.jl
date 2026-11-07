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

	@testset "KongsbergHeadingWithoutPlatform" begin
		# Kongsberg EM122 .all data (format 58) preprocessed without a platform file must keep the
		# recorded heading in the format 59 output and in the .fbt file. mbsys_simrad3_preprocess()
		# used to add uninitialized platform mount offsets, giving heading 0.000 in the .mb59 file
		# and -21474836 in the .fbt file instead of about 8.3 degrees. The test data are two pings
		# of a Sea Acceptance Test line of RV METEOR, 19 December 2011. Uninitialized stack values
		# change with the stack layout, and the old code gave the correct heading by chance in some
		# runs, so mbpreprocess is run with a range of environment sizes, each of which must pass.
		cmd = abspath(CMD)
		cmd_dir = dirname(cmd)
		testdata = abspath(joinpath("testdata", "mb58"))
		src_name = "0008_20111219_210356_METEOR_EM122.snipped.all"
		env = mbenv()
		env["PATH"] = join([cmd_dir, get(env, "PATH", "")], Sys.iswindows() ? ';' : ':')

		headings(filename, iformat) =
			[parse(Float64, value) for value in
			 split(mbcheck([joinpath(cmd_dir, "mblist" * MB_EXE), "-I" * filename, "-F$iformat", "-Q", "-OH"]; env))]

		mktempdir() do tmpdir
			cp(joinpath(testdata, src_name), joinpath(tmpdir, src_name))
			cd(tmpdir) do
				ofile = splitext(src_name)[1] * ".mb59"
				raw = headings(src_name, 58)
				@test length(raw) == 2
				for value in raw
					@test isapprox(value, 8.27; atol = 0.05)
				end

				for pad in 0:8:255
					run_env = copy(env)
					run_env["MB_TEST_STACK_PAD"] = "x"^pad
					mbcheck([cmd, "--input=" * src_name, "--format=58"]; stderr_to_stdout = true, env = run_env)
					for (filename, iformat) in ((ofile, 59), (ofile * ".fbt", 71))
						preprocessed = headings(filename, iformat)
						@test length(preprocessed) == length(raw)
						for (raw_value, value) in zip(raw, preprocessed)
							@test isapprox(value, raw_value; atol = 0.1)
						end
					end
				end
			end
		end
	end
end
