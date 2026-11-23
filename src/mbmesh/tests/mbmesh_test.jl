# Smoke test for the mbmesh command line app itself (as opposed to the
# algorithm-level tests alongside this file, e.g. test_settings.cpp).

include(joinpath(@__DIR__, "..", "..", "..", "test", "mbtest.jl"))

const CMD = mbprog("mbmesh", "mbmesh")

# Run mbmesh with -I nonexistent and `args`; return (output, exit code).
run_bad(args...) = mbrun([CMD, "-I", "nonexistent", args...]; stderr_to_stdout = true)

help_text() = mbcheck([CMD, "-h"]; stderr_to_stdout = true)

@testset "mbmesh" begin
	@testset "Help" begin
		output = help_text()
		@test occursin("mbmesh generates 3D meshes from swath sonar bathymetry", output)
		@test occursin("usage: mbmesh", output)
	end

	@testset "GeoOriginRequiresThreeValues" begin
		# Same rule as mbgrd2gltf: a specified GeoOrigin must be lon,lat,elev.
		for arg in ("--geoorigin=-122.4,36.6", "-G-122.4")
			output, code = run_bad(arg)
			@test code != 0
			@test occursin("GeoOrigin format must be lon,lat,elev", output)
		end
	end

	@testset "ChunkOptionsAreValidated" begin
		# one comma, and each of I and J a whole number or a range with the first not after the last
		for value in ("abc", "1", "1,2,3", "1,", ",2", "3..1,0", "0,2..1", "1..,0", "0,x", "0..100000,0..100000")
			output, code = run_bad("--chunk=" * value)
			@test code != 0
			@test occursin("invalid --chunk:", output)
		end
	end

	@testset "PoissonSolverOptionsAreValidated" begin
		# an unknown solver or a bad count is refused before any data is read
		for (arg, message) in (("--poisson-solver=bogus", "'bogus' is not one of jacobi, sor"),
		                       ("--poisson-iterations=0", "whole number from 1 to 100000"),
		                       ("--poisson-iterations=x", "whole number from 1 to 100000"))
			output, code = run_bad(arg)
			@test code != 0
			@test occursin(message, output)
			@test !occursin("reading datalist", output)
		end
	end

	@testset "PoissonSolverNamesAreAccepted" begin
		# a known solver gets past option parsing, to the missing input
		for name in ("jacobi", "sor")
			output, _ = run_bad("--poisson-solver=" * name)
			@test !occursin("invalid --poisson-solver", output)
		end
	end

	@testset "HelpDocumentsOptions" begin
		output = help_text()
		for option in ("--poisson-solver", "--poisson-iterations",
		               "--chunk-size", "--chunk-halo", "--seam-blend", "--chunk <i,j>", "--max-chunks",
		               "--label-chunks", "--threads",
		               "--info", "--void-size", "--island-size", "--no-draco", "--spill-points", "mbmesh_work",
		               "--pointcloud-colormap", "--lift ", "--geoviewpoint", "Copy GeoViewpoint",
		               "--tileset", "tileset/tileset.json", "--x3dom-url", "--geoorigin")
			@test occursin(option, output)
		end
		@test !occursin("--metadata", output)
	end

	@testset "OldMetadataOptionIsRefusedNotIgnored" begin
		# --metadata was renamed --info. It must not be silently ignored, which
		# would start a full reconstruction.
		for option in ("--metadata", "-metadata")
			output, code = run_bad(option)
			@test code != 0
			@test occursin("renamed --info", output)
			# it stopped before reading any data
			@test !occursin("reading datalist", output)
		end
	end

	@testset "GeoOriginValueMustBeAttached" begin
		# "--geoorigin VALUE" is the option with no value plus a stray argument, which
		# used to be ignored and gave the automatic GeoOrigin without a word.
		for value in ("122.4,36.6,0", "-122.4,36.6,0")
			output, code = run_bad("--geoorigin", value)
			@test code != 0
			@test !occursin("reading datalist", output)
			@test occursin("unexpected argument", output) || occursin("unrecognized", output)
		end
	end

	@testset "UnknownOptionsAreRefused" begin
		output, code = run_bad("--bogus")
		@test code != 0
		@test occursin("unrecognized", output)
	end

	@testset "GeoViewpointIsValidated" begin
		# --geoviewpoint takes 6 or 10 numbers and needs --html; bad values are refused
		# before any data are read.
		for (args, message) in ((["--html", "--geoviewpoint=1,2,3,4,5"], "6 numbers"),
		                        (["--html", "--geoviewpoint=1,2,3,4,5,x"], "'x'"),
		                        (["--html", "--geoviewpoint=1 2 3 4 5 6"], "not a number"),
		                        (["--geoviewpoint=1,2,3,4,5,6"], "use one of them too"))
			output, code = run_bad(args...)
			@test code != 0
			@test occursin(message, output)
			@test !occursin("reading datalist", output)
		end
	end

	@testset "LiftIsValidated" begin
		# --lift takes meters, zero or more, and needs --tileset.
		for (args, message) in ((["--tileset", "--lift=abc"], "not a number of meters"),
		                        (["--lift"], "use --tileset too"),
		                        (["--tileset", "--lift=-5"], "not a number of meters"),
		                        (["--lift=100"], "use --tileset too"))
			output, code = run_bad(args...)
			@test code != 0
			@test occursin(message, output)
		end
	end

	@testset "SizeOptionsAreValidated" begin
		# option prefix, bad values, expected message
		for (option, values, message) in (("--spill-points=", ("abc", "-5", "1.5", ""), "not a whole number of soundings"),
		                                  ("--void-size=", ("abc", "-5", ""), "not a size in meters"),
		                                  ("--island-size=", ("abc", "-5", ""), "not a size in meters"),
		                                  ("--draco-precision=", ("abc", "-1", "0", ""), "not a size in meters"),
		                                  ("--draco-normal-bits=", ("abc", "1", "31", ""), "not a number of bits"),
		                                  ("--seam-method=", ("abc", "", "Solve"), "is not blend or solve"),
		                                  ("--chunk-halo=", ("abc", "-1", "0", ""), "not a size in meters"),
		                                  ("--seam-blend=", ("abc", "-1", ""), "not a width in meters"))
			for value in values
				output, code = run_bad(option * value)
				@test code != 0
				@test occursin(message, output)
			end
		end
	end

	@testset "LabelChunksNeedsHtml" begin
		output, code = run_bad("--label-chunks")
		@test code != 0
		@test occursin("use --html too", output)
	end

	@testset "PointcloudColormapIsValidated" begin
		output, code = run_bad("--pointcloud-colormap=rainbow")
		@test code != 0
		@test occursin("linear, equalized, none", output)
	end

	@testset "OutputGoesBesideTheDatalistAndDirectoryInputWritesSurveyPage" begin
		cmd = abspath(CMD)
		mktempdir() do work
			survey = joinpath(work, "20260101A")
			mkpath(joinpath(survey, "multibeam", "mbmesh_output", "chunks"))
			mkpath(joinpath(survey, "lidar"))
			# a minimal GLB header: magic, version 2, length 28, no chunks
			glb = vcat(Vector{UInt8}("glTF"), reinterpret(UInt8, htol.(UInt32[2, 28, 0])))
			for name in ("multibeam/mbmesh_output/mesh.glb", "multibeam/mbmesh_output/chunks/t.glb", "lidar/l.glb")
				write(joinpath(survey, name), glb)
			end
			slashes(s) = replace(s, '\\' => '/')
			cd(survey) do
				# Without -O the output directory is beside the datalist
				output, _ = mbrun([cmd, "-I", "multibeam/datalistp.mb-1"]; stderr_to_stdout = true)
				@test occursin("output=multibeam/mbmesh_output", slashes(output))
				output, _ = mbrun([cmd, "-I", "multibeam/datalistp.mb-1", "-O", "elsewhere"]; stderr_to_stdout = true)
				@test occursin("output=elsewhere", output)
			end
			cd(work) do
				# A directory input writes only the survey page, in the current directory
				output, code = mbrun([cmd, "-I", "20260101A"]; stderr_to_stdout = true)
				@test code == 0
				page = slashes(read(joinpath(work, "mbmesh_survey.html"), String))
				@test occursin("<summary><b>multibeam</b></summary>", page)
				@test occursin("uri: '20260101A/lidar/l.glb'", page)
				@test occursin("uri: '20260101A/multibeam/mbmesh_output/mesh.glb'", page)
				@test !occursin("t.glb", page)
			end
		end
	end

	@testset "NoInputFailsCleanly" begin
		# With no arguments at all the usage is shown, as mbgrd2gltf does
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 0
		@test occursin("mbmesh generates 3D meshes from swath sonar bathymetry", output)
		@test occursin("usage: mbmesh", output)
		@test occursin("Earth-Centered, Earth-Fixed", output)
		# Options but no input is still an error
		output, code = mbrun([CMD, "-V"]; stderr_to_stdout = true)
		@test code != 0
		@test occursin("input datalist path is empty", output)
	end
end
