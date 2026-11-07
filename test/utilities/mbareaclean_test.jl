# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbareaclean command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbareaclean")

@testset "mbareaclean" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("identifies and flags artifacts", output)
		@test occursin("usage:", output)
		@test occursin("minbeam[/maxbeam]", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("identifies and flags artifacts", output)
		@test occursin("usage:", output)
		@test occursin("minbeam[/maxbeam]", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("median_filter", output)
	end

	@testset "KeepsExistingEdits" begin
		# mbareaclean must keep the edits already in the edit save file, also for soundings it does
		# not evaluate (here: outside the --bounds area). It used to rewrite the file with only the
		# edits of the soundings stored in its grid, deleting all the others.
		cmd = abspath(CMD)
		cmd_dir = dirname(cmd)
		testdata = abspath(joinpath("testdata", "mb54"))
		swath = "TN136HS.309.snipped.mb54"
		env = mbenv()
		env["PATH"] = join([cmd_dir, get(env, "PATH", "")], Sys.iswindows() ? ';' : ':')

		# keep time, beam and action, not the record index
		function edits()
			output = unixlines(mbcheck([joinpath(cmd_dir, "mbdumpesf" * MB_EXE), "--input=" * swath * ".esf"]; env))
			return [split(line, "time:"; limit = 2)[2] for line in split(output, '\n') if startswith(line, "EDITS READ:")]
		end

		mktempdir() do tmpdir
			cp(joinpath(testdata, swath), joinpath(tmpdir, swath))
			cd(tmpdir) do
				mbcheck([joinpath(cmd_dir, "mbclean" * MB_EXE), "--input=" * swath, "--format=54", "--depth-range=455/470"];
				        stderr_to_stdout = true, env)
				before = edits()
				@test length(before) > 0

				write("datalist.mb-1", swath * " 54\n")
				mbcheck([cmd, "--input=datalist.mb-1", "--bin-size=20", "--median-filter=0.5/2",
				         "--flag-bad", "--bounds=-124.51/-124.5015/40.83/40.85"]; stderr_to_stdout = true, env)
				after = edits()
				for edit in before
					@test edit in after
				end
			end
		end
	end

	# TODO(schwehr): Add more tests of actual usage.
end
