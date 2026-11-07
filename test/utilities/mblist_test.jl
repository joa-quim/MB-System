# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mblist command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mblist")

@testset "mblist" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("prints the specified contents of a swath data", output)
		@test occursin("usage:", output)
		@test occursin("-Zsegment", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("prints the specified contents of a swath data", output)
		@test occursin("usage:", output)
		@test occursin("-Zsegment", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("projection_pars:", output)
	end

	@testset "InvalidBeamRange" begin
		# An unparsable beam range used to leave the beam range uninitialized, so mblist could loop
		# over a random number of beams for every ping.
		for arg in ("-MC", "-M5", "-MX", "--beam-range=C", "--beam-range=X")
			output, code = mbrun([CMD, arg]; stderr_to_stdout = true)
			@test code == 9
			@test occursin("Invalid beam range:", output)
			@test occursin("usage:", output)
		end
	end

	@testset "ValidBeamRange" begin
		# Valid beam ranges are accepted; mblist then stops because there is no input file.
		for arg in ("-M10/20", "-MA", "-MX20", "--beam-range=10/20")
			output, code = mbrun([CMD, arg]; stderr_to_stdout = true)
			@test code == 2
			@test !occursin("Invalid beam range:", output)
			@test occursin("Unable to open data list file:", output)
		end
	end

	@testset "InvalidPixelRange" begin
		# Same as InvalidBeamRange for the sidescan pixel range.
		for arg in ("-NC", "-N5", "--pixel-range=C")
			output, code = mbrun([CMD, arg]; stderr_to_stdout = true)
			@test code == 9
			@test occursin("Invalid pixel range:", output)
			@test occursin("usage:", output)
		end
	end

	@testset "ValidPixelRange" begin
		for arg in ("-N10/20", "-NA", "--pixel-range=10/20")
			output, code = mbrun([CMD, arg]; stderr_to_stdout = true)
			@test code == 2
			@test !occursin("Invalid pixel range:", output)
			@test occursin("Unable to open data list file:", output)
		end
	end

	# TODO(schwehr): Add tests of actual usage.
end
