# Tests for mbgrdviz.
#
# Unlike the other Motif GUI editors, mbgrdviz parses -h/--help before
# opening an X display, so this test runs it directly without Xvfb.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("mbgrdviz", "mbgrdviz")

@testset "mbgrdviz" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Program MBgrdviz", output)
		@test occursin("interactive 2D/3Dvizualization of GMT grids", output)
	end
end
