# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbsegygrid command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbsegygrid")

@testset "mbsegygrid" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Bad ", output)
		@test occursin(" specified", output)
		@test occursin("Terminated", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("grids trace data from segy data files", output)
		@test occursin("usage:", output)
		@test occursin("-Wwindowmode/windowstart/windowend", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("grids trace data from segy data files", output)
		@test occursin("usage:", output)
		@test occursin("-Wwindowmode/windowstart/windowend", output)
		@test occursin("dbg2", output)
		@test occursin("scale2distance:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
