# Tests for mbusbl2fnv command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbusbl2fnv")

@testset "mbusbl2fnv" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-H"]; stderr_to_stdout = true)
		@test occursin("Program mbusbl2fnv", output)
		@test occursin("Usage:", output)
		@test occursin("-I usbl.csv", output)
	end

	@testset "NoArgsEmitsFnvHeader" begin
		# With no input file it reads stdin; closing stdin immediately still
		# lets it print the fnv column header before exiting. (mbrun's stdin is the null device.)
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("<longitude (deg)>", output)
		@test occursin("<latitude (deg)>", output)
	end
end
