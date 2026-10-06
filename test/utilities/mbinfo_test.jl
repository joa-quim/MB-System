# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbinfo command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbinfo")

@testset "mbinfo" begin
	@testset "NoArgs" begin
		# Report a failure by calling exit(3).  There is no real failure.
		output, code = mbrun([CMD])
		@test code == 3
		@test occursin("initialization failed", output)
	end

	@testset "Help" begin
		@test occursin("basic statistics", mbcheck([CMD, "-h"]))
	end

	# In the Python version this case was shadowed by a second, skipped method of the same name
	# and never ran.
	@testset "DefaultOutputFormat" begin
		output = mbcheck([CMD, "-Itestdata/mb21/TN136HS.309.snipped.mb21"])
		@test occursin("MBIO Data Format ID:  21", output)
	end

	## TODO DWCaress 7 Jan 2020
	## I attempted to add tests checking both *.inf and *.json output for all
	## available data samples. The script mbinfo_generate.cmd generates *.inf and
	## *.json files for all available data samples, which was done with the code
	## built on a Mac. These tests are done by the Travis-CI service generating
	## the same output under Ubuntu and checking that the two outputs are identical.
	## Currently these tests  succeed for the *.inf files for all samples with
	## valid data, but fail for some samples that don't have valid data. These
	## tests mostly fail for the *.json output including most of the valid samples.
	## In order to get to a state where the Travis testing is succeeding and
	## proceeding through the whole sequence, I have removed the *.inf tests for
	## the non-valid data samples and commented out the *.json tests below.
	## We definitely want the code should pass these tests, but since distributions
	## include the testing, any failing tests need to be skipped temporarily when
	## we generate a release.
	@testset "JsonOutputStyle" begin
		# Skipped: *.json output does not match between Mac and Ubuntu
		@test_skip false
	end

	@testset "DefaultOutputStyle" begin
		# Skipped: *.inf output does not match between Mac and Ubuntu
		@test_skip false
	end
end
