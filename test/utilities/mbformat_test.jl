# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbformat command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbformat")

@testset "mbformat" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD])
		@test occursin("Supported MBIO Formats:", output)
		@test occursin("MBIO Data Format ID:  11", output)
		@test occursin("MBIO Data Format ID:  261", output)
		@test occursin("Format name:          MBF_KEMKMALL", output)
		@test occursin("Informal Description: Kongsberg", output)
	end

	@testset "Html" begin
		output = mbcheck([CMD, "-W"])
		@test occursin("<TITLE>MB-System Supported Data Formats</TITLE>", output)
		@test occursin("<LI>MBIO Data Format ID:  11 </LI>", output)
		@test occursin("<HTML>", output)
		@test occursin("<BODY", output)
		@test occursin("<UL>", output)
	end

	@testset "VerboseOnly" begin
		output = mbcheck([CMD, "-V"]; stderr_to_stdout = true)
		@test occursin("MB-system Version", output)
	end

	@testset "Junk" begin
		output, code = mbrun([CMD, "--garbage-opt"]; stderr_to_stdout = true)
		@test code == 9
		@test occursin("usage", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-H"]; stderr_to_stdout = true)
		@test occursin("identifies the swath data formats", output)
		@test occursin("usage:", output)
	end

	@testset "Format" begin
		output = mbcheck([CMD, "-F51"])
		@test occursin("MBF_EMOLDRAW", output)
		@test occursin("EM121", output)
		@test !occursin("MBF_EM12IFRM", output)
	end

	@testset "InputFile" begin
		output = mbcheck([CMD, "-Itestdata/mb21/TN136HS.309.snipped.mb21"])
		@test occursin("MBF_HSATLRAW", output)
		@test occursin("Hydrosweep", output)
		@test !occursin("MBF_EM12IFRM", output)
	end

	@testset "InputFileWithFileRoot" begin
		output = rstrip(mbcheck([CMD, "-K", "-Itestdata/mb173/NBP0209.snipped.a77"]))
		@test output == "testdata/mb173/NBP0209.snipped 173"
	end

	@testset "InputFileIgnoreFormat" begin
		output = mbcheck([CMD, "-F173", "-Itestdata/mb21/TN136HS.309.snipped.mb21"])
		@test occursin("MBF_HSATLRAW", output)
		@test occursin("Hydrosweep", output)
		@test !occursin("MBF_EM12IFRM", output)
	end

	@testset "InputFileWithOnlyIdNumber" begin
		output = rstrip(mbcheck([CMD, "-L", "-Itestdata/mb173/NBP0209.snipped.a77"]))
		@test parse(Int, output) == 173
	end

	@testset "DoesNotExist" begin
		output, code = mbrun([CMD, "-I/does/not/exist"]; stderr_to_stdout = true)
		@test code == 3
		@test occursin("unable to infer format from filename", output)
	end
end
