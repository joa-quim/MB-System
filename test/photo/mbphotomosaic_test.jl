# Tests for mbphotomosaic command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("photo", "mbphotomosaic")

@testset "mbphotomosaic" begin
	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Program mbphotomosaic", output)
		@test occursin("makes a mosaic of navigated downlooking photographs", output)
		@test occursin("usage: mbphotomosaic", output)
		@test occursin("--threads=nthreads", output)
	end
end
