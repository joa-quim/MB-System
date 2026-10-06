# Tests for mbgetphotocorrection command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("photo", "mbgetphotocorrection")

@testset "mbgetphotocorrection" begin
	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Program mbgetphotocorrection", output)
		@test occursin("usage: mbgetphotocorrection", output)
		@test occursin("--threads=nthreads", output)
	end
end
