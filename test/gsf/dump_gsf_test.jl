# Tests for dump_gsf command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("gsf", "dump_gsf")

@testset "dump_gsf" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Usage:", output)
		@test occursin("-f <gsf filename>", output)
	end
end
