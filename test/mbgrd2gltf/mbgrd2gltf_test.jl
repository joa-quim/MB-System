# Tests for mbgrd2gltf command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("mbgrd2gltf", "mbgrd2gltf")

# Run mbgrd2gltf with -I nonexistent.grd and `args`; return (output, exit code).
run_bad(args...) = mbrun([CMD, "-I", "nonexistent.grd", args...]; stderr_to_stdout = true)

@testset "mbgrd2gltf" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("MBgrd2gltf", output)
		@test occursin("converts a GMT GRD format bathymetry grid file", output)
		@test occursin("usage: mbgrd2gltf", output)
	end

	@testset "GeoViewpointIsValidated" begin
		# --geoviewpoint takes 6 or 10 numbers, attached, and needs --html.
		for (args, message) in ((["--html", "--geoviewpoint=1,2,3"], "takes 6 numbers"),
		                        (["--html", "--geoviewpoint=a,b,c,d,e,f"], "is not a number"),
		                        (["--html", "--geoviewpoint=36.6,-122.4,-900,36.61,-122.39,-950"], "lon,lat,elev"),
		                        (["--geoviewpoint=-122.4,36.6,-900,-122.39,36.61,-950"], "use one of them too"))
			output, code = run_bad(args...)
			@test code != 0
			@test occursin(message, output)
		end
	end

	@testset "DracoOptionsAreValidated" begin
		# The options are those of mbmesh: --draco-precision is a size in meters above zero and --draco-normal-bits a number of
		# bits from 2 to 30. The quantize options that they replace are refused with a message that says so.
		for (args, message) in ((["--draco-precision=abc"], "not a size in meters"),
		                        (["--draco-precision=0"], "not a size in meters"),
		                        (["--draco-normal-bits=1"], "not a number of bits"),
		                        (["--draco-normal-bits=31"], "not a number of bits"),
		                        (["--quantize-position", "20"], "replaced by --draco-precision"),
		                        (["-Qp", "20"], "replaced by --draco-precision"))
			output, code = run_bad(args...)
			@test code != 0
			@test occursin(message, output)
		end
	end

	@testset "HelpDocumentsOptions" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		for option in ("--no-binary", "--no-draco", "--draco-precision", "--draco-normal-bits",
		               "--geoviewpoint", "Copy GeoViewpoint")
			@test occursin(option, output)
		end
		@test !occursin("--quantize-position", output)
	end
end
