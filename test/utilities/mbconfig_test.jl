# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbconfig command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbconfig")

# e.g. 5.7.5beta9
const VERSION_REGEX = r"[0-9]+[.][0-9]+[.][0-9]+((alpha|beta)[0-9]+)?"

@testset "mbconfig" begin
	@testset "NoArgs" begin
		output = rstrip(mbcheck([CMD]))
		# regex 5.7.5beta9
		@test occursin(VERSION_REGEX, output)
	end

	@testset "VerboseOnly" begin
		output = rstrip(mbcheck([CMD, "--verbose"]; stderr_to_stdout = true))
		@test occursin("MB-System version:", output)
		@test occursin(VERSION_REGEX, output)
	end

	@testset "Junk" begin
		output, code = mbrun([CMD, "--garbage-opt"]; stderr_to_stdout = true)
		@test code == 9
		@test occursin("usage", output)
	end

	@testset "Help" begin
		output = rstrip(mbcheck([CMD, "--verbose", "--help"]; stderr_to_stdout = true))
		@test occursin("flags", output)
		@test occursin("levitus database", output)
		@test occursin("tidal correction", output)
	end

	@testset "Prefix" begin
		output = rstrip(mbcheck([CMD, "--verbose", "--prefix"]; stderr_to_stdout = true))
		@test occursin("MB-System install prefix:", output)
	end

	@testset "Cflags" begin
		output = rstrip(mbcheck([CMD, "--verbose", "--cflags"]; stderr_to_stdout = true))
		@test occursin("MB-System compile flags:", output)
		# TODO(schwehr): -INONE/include is wrong
	end

	@testset "Libs" begin
		output = rstrip(mbcheck([CMD, "--verbose", "--libs"]; stderr_to_stdout = true))
		@test occursin("MB-System link flags:", output)
		for lib in ("mbaux", "mbsapi", "mbbsio", "mbview", "mbgsf", "mbxgr", "mbio")
			@test occursin(" -l$lib", output)
		end
		# TODO(schwehr): -LNONE/include is wrong
	end

	@testset "VersionId" begin
		output = rstrip(mbcheck([CMD, "--verbose", "--version-id"]; stderr_to_stdout = true))
		@test occursin("MB-System version id:", output)
		# e.g. 50700005
		m = match(r"[0-9]{8}", output)
		@test m !== nothing
		m === nothing || @test parse(Int, m.match) > 50700000
	end

	@testset "VersionMajor" begin
		@test parse(Int, strip(mbcheck([CMD, "--version-major"]))) >= 5
	end

	@testset "VersionMinor" begin
		@test parse(Int, strip(mbcheck([CMD, "--version-minor"]))) >= 0
	end

	@testset "VersionArchive" begin
		# a.k.a. minor minor
		@test parse(Int, strip(mbcheck([CMD, "--version-archive"]))) >= 0
	end

	@testset "Levitus" begin
		@test occursin("LevitusAnnual82", mbcheck([CMD, "--levitus"]))
	end

	@testset "Otps" begin
		@test occursin("otps", lowercase(mbcheck([CMD, "--otps"])))
		# TODO(schwehr): Why if built without otps?
	end
end
