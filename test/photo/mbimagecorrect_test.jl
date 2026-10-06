# Tests for mbimagecorrect command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("photo", "mbimagecorrect")

@testset "mbimagecorrect" begin
	@testset "HelpDoesNotCrash" begin
		# Known bug: mbimagecorrect currently tries to open/convert an image
		# before checking --help or validating that an input was given, and
		# aborts with an uncaught cv::Exception (SIGABRT) instead of printing
		# usage. This test documents the expected (non-crashing) behavior and
		# is marked broken until that is fixed; if it starts passing, Julia
		# reports an unexpected pass, which is this test's own signal the bug
		# was fixed and @test_broken should become @test.
		output, code = mbrun([CMD, "--help"]; stderr_to_stdout = true)
		@test_broken code >= 0
	end
end
