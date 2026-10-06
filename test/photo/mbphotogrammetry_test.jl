# Tests for mbphotogrammetry command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("photo", "mbphotogrammetry")

@testset "mbphotogrammetry" begin
	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Program mbphotogrammetry", output)
		@test occursin("generates bathymetry from stereo pairs of photographs", output)
		@test occursin("usage: mbphotogrammetry", output)
		@test occursin("--threads=nthreads", output)
	end
end
