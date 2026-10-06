# Tests for mbtiff2png command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("photo", "mbtiff2png")

@testset "mbtiff2png" begin
	@testset "NoInputFailsCleanly" begin
		# mbtiff2png does not special-case --help/-h before checking for a
		# required input file, so the smoke-test contract here is simply that
		# a missing input is reported cleanly rather than crashing.
		output, code = mbrun([CMD, "--help"]; stderr_to_stdout = true)
		@test code != 0
		@test occursin("Program <mbtiff2png>", output)
		@test occursin("Input Tiff image file not specified", output)
	end
end
