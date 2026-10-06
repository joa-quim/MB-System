# Tests for mbimagelist command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("photo", "mbimagelist")

@testset "mbimagelist" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Program mbimagelist", output)
		@test occursin("parses recursive imagelist files", output)
		@test occursin("usage: mbimagelist", output)
	end
end
