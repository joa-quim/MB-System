# See README file for copying and redistribution conditions.

# Tests for mbhistory command line app.
#
# The tests make synthetic edit save (*.esf) files next to an empty placeholder swath file, so no
# sonar data are needed.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = abspath(mbprog("utilities", "mbhistory"))
const MBCLEAN = abspath(mbprog("utilities", "mbclean"))

const MB_PATH_MAXLINE = 1024
const ACTION_FLAG = 1
const ACTION_UNFLAG = 2

# Write a version 3 edit save file (big endian) with (time, beam, action) edits.
function write_esf(path, edits)
	header = Vector{UInt8}("ESFVERSION03\nESF Mode: 0\nProgram: mbhistory_test\n")
	open(path, "w") do esf
		write(esf, header)
		write(esf, zeros(UInt8, MB_PATH_MAXLINE - length(header)))
		for (time_d, beam, action) in edits
			write(esf, hton(Float64(time_d)), hton(Int32(beam)), hton(Int32(action)))
		end
	end
end

# zlib's CRC-32 of a file
const CRC32_TABLE = [foldl((c, _) -> isodd(c) ? xor(0xedb88320, c >> 1) : c >> 1, 1:8; init = UInt32(n)) for n in 0:255]
function crc32(path)
	crc = 0xffffffff
	for byte in read(path)
		crc = xor(CRC32_TABLE[(xor(crc, byte) & 0xff) + 1], crc >> 8)
	end
	return xor(crc, 0xffffffff)
end
hex8(crc) = string(crc; base = 16, pad = 8)

# The entries of a history manifest, split at tabs
function read_manifest(path)
	lines = split(unixlines(read(path, String)), '\n'; keepempty = false)
	return [split(line, '\t') for line in lines if !startswith(line, "#")]
end

# A temporary directory with the placeholder swath file line.mb88 (datalists skip empty files);
# f(tmpdir, swath, esf, history)
function with_swath(f)
	mktempdir(; prefix = "mbhistory_test_") do tmpdir
		swath = joinpath(tmpdir, "line.mb88")
		write(swath, "placeholder\n")
		f(tmpdir, swath, swath * ".esf", joinpath(tmpdir, "mbhistory"))
	end
end

run_mbhistory(swath, args...) = reverse(mbrun(vcat([CMD, "--input=" * swath], collect(args)); stderr_to_stdout = true))

manifest(history) = read_manifest(joinpath(history, "line.mb88.esf.manifest"))

snapshots(history) = sort(filter(n -> startswith(n, "line.mb88.esf.v"), readdir(history)))

# Make versions 1-3 with 1, 2 and 3 edits; the current file has 4 edits.
function make_versions(swath, esf)
	edits = Tuple{Float64,Int,Int}[]
	for i in 0:2
		push!(edits, (1761000000.0 + i, 10 + i, ACTION_FLAG))
		write_esf(esf, edits)
		code, output = run_mbhistory(swath, "--tag=t$(i + 1)")
		@test code == 0
		code == 0 || println(output)
	end
	push!(edits, (1761000003.0, 20, ACTION_FLAG))
	write_esf(esf, edits)
end

@testset "mbhistory" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 9
		@test occursin("No input swath file or datalist specified", output)
		@test occursin("usage:", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("restores the versions", output)
		@test occursin("usage:", output)
		@test occursin("--restore-date", output)
		@test occursin("--prune", output)
	end

	@testset "BadUsage" begin
		with_swath() do tmpdir, swath, esf, history
			code, output = run_mbhistory(swath, "--list", "--prune", "--keep=2")
			@test code == 9
			@test occursin("Only one of", output)
			code, output = run_mbhistory(swath, "--prune")
			@test code == 9
			@test occursin("--prune needs --keep", output)
			code, output = run_mbhistory(swath, "--restore-date=yesterday")
			@test code == 9
			@test occursin("Bad date", output)
		end
	end

	@testset "ListNoHistory" begin
		with_swath() do tmpdir, swath, esf, history
			code, output = run_mbhistory(swath)
			@test code == 0
			@test occursin("no history versions", output)
		end
	end

	@testset "TagAndList" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			names = snapshots(history)
			@test length(names) == 3
			@test occursin(r"^line\.mb88\.esf\.v0001_\d{8}T\d{6}$", names[1])

			entries = manifest(history)
			@test [e[1] for e in entries] == ["1", "2", "3"]
			@test entries[1][4] == "mbhistory"
			@test entries[3][8] == hex8(crc32(joinpath(history, names[3])))

			code, output = run_mbhistory(swath, "--list")
			@test code == 0
			@test occursin("[t1]", output)
			@test !occursin("[t1] tag t1", output)
			@test occursin(r"now .*\(current\)\s+4", output)

			# tagging an unchanged file reuses the newest version
			write_esf(esf, [(1761000000.0 + i, 10 + i, ACTION_FLAG) for i in 0:2])
			code, output = run_mbhistory(swath, "--tag=again")
			@test occursin("tagged version 3 as again", output)

			# a tag name is not reused unless forced
			code, output = run_mbhistory(swath, "--tag=t1")
			@test code != 0
			@test occursin("already set", output)
		end
	end

	@testset "ShowAndDiff" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			code, output = run_mbhistory(swath, "--show=2")
			@test code == 0
			@test occursin("Edit events:    2 (flag 2", output)
			@test occursin("ESFVERSION03", output)

			code, output = run_mbhistory(swath, "--diff=1")
			@test code == 0
			@test occursin("from v1 to now", output)
			@test occursin("3 beams changed: 3 now flagged", output)

			code, output = run_mbhistory(swath, "--diff=now:1")
			@test occursin("3 edits removed", output)

			code, output = run_mbhistory(swath, "--diff=2:2")
			@test occursin("none", output)

			code, output = run_mbhistory(swath, "--diff=9")
			@test code != 0
			@test occursin("version 9 not found", output)
		end
	end

	@testset "DiffUsesFinalEdit" begin
		with_swath() do tmpdir, swath, esf, history
			# flag then unflag the same beam: the final edit is an unflag
			write_esf(esf, [(1761000000.0, 5, ACTION_FLAG)])
			run_mbhistory(swath, "--tag=a")
			write_esf(esf, [(1761000000.0, 5, ACTION_FLAG), (1761000000.0, 5, ACTION_UNFLAG)])
			code, output = run_mbhistory(swath, "--diff=1")
			@test occursin("1 beams changed: 0 now flagged, 1 now unflagged", output)
		end
	end

	@testset "RestoreKeepsLaterVersions" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			before = crc32(esf)

			# without --yes a restore is a dry run
			code, output = run_mbhistory(swath, "--restore=1")
			@test code == 0
			@test occursin("would restore version 1", output)
			@test occursin("Add --yes to apply", output)
			@test crc32(esf) == before

			code, output = run_mbhistory(swath, "--restore=1", "--yes")
			@test code == 0
			@test occursin("current edits saved as version 4", output)
			@test occursin("restored version 1", output)
			@test !isfile(swath * ".lck")

			entries = manifest(history)
			@test [e[1] for e in entries] == ["1", "2", "3", "4"]
			@test entries[4][10] == "restore (v4 -> v1)"
			@test entries[4][8] == hex8(before)
			@test entries[1][8] == hex8(crc32(esf))

			code, output = run_mbhistory(swath, "--list")
			@test occursin("restored from v1", output)

			# the restore can be undone
			code, output = run_mbhistory(swath, "--restore=4", "--yes")
			@test code == 0
			@test crc32(esf) == before
			@test length(manifest(history)) == 5

			# restoring the current state changes nothing
			code, output = run_mbhistory(swath, "--restore=4", "--yes")
			@test occursin("already match version 4", output)
			@test length(manifest(history)) == 5
		end
	end

	@testset "RestoreLockedFile" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			write(swath * ".lck", "Locking Program: mbeditviz\n")
			code, output = run_mbhistory(swath, "--restore=1", "--yes")
			@test code != 0
			@test occursin("file is locked", output)
			@test length(manifest(history)) == 3
		end
	end

	@testset "RestoreTagAndDate" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			code, output = run_mbhistory(swath, "--restore-tag=t2", "--yes")
			@test code == 0
			@test occursin("restored version 2", output)

			code, output = run_mbhistory(swath, "--restore-tag=nosuchtag", "--yes")
			@test occursin("tag nosuchtag not set", output)

			# every snapshot was taken after 2000, so the first one holds that state
			code, output = run_mbhistory(swath, "--restore-date=2000-01-01", "--yes")
			@test occursin("restored version 1", output)

			# no snapshot after a future date: the current file is that state
			code, output = run_mbhistory(swath, "--restore-date=2999-12-31T23:59")
			@test occursin("no change since that date", output)
		end
	end

	@testset "RestoreDateBeforeHistoryWarns" begin
		with_swath() do tmpdir, swath, esf, history
			# a history without a "created" version does not know the edits made before its oldest
			# version
			make_versions(swath, esf)
			code, output = run_mbhistory(swath, "--restore-date=2000-01-01")
			@test code == 0
			@test occursin("would restore version 1", output)
			@test occursin("edits made before", output)
			@test occursin("version 1 is the oldest known state", output)
		end
	end

	@testset "CreatedVersionWithMbclean" begin
		with_swath() do tmpdir, swath, esf, history
			# mbclean creates the esf file: version 1 holds no edits and has the note "created", so
			# restoring a date before the first edits removes them all. Snapshots are saved before
			# each run: runs 2 and 3 save versions 2 and 3. Run 3 adds no edits, so run 4 finds the
			# same edits as version 3 (only the esf header was rewritten) and saves no new version.
			testdata = abspath(joinpath("testdata", "mb54"))
			data = joinpath(tmpdir, "TN136HS.309.snipped.mb54")
			cp(joinpath(testdata, "TN136HS.309.snipped.mb54"), data)
			cd(tmpdir) do
				for depth_range in ("0/460", "0/455", "0/455", "0/455")
					mbcheck([MBCLEAN, "-I" * data, "-F54", "--depth-range=" * depth_range])
				end
			end
			entries = read_manifest(joinpath(history, "TN136HS.309.snipped.mb54.esf.manifest"))
			@test length(entries) == 3
			@test entries[1][10] == "created"
			@test entries[1][4] == "mbclean"
			@test filesize(joinpath(history, entries[1][9])) == MB_PATH_MAXLINE

			output, code = mbrun([CMD, "--input=" * data, "--restore-date=2000-01-01", "--yes"]; stderr_to_stdout = true)
			@test code == 0
			@test occursin("restored version 1", output)
			@test !occursin("were not recorded", output)
			@test filesize(data * ".esf") == MB_PATH_MAXLINE
		end
	end

	@testset "Datalist" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			write(joinpath(tmpdir, "line2.mb88"), "placeholder\n")
			datalist = joinpath(tmpdir, "datalist.mb-1")
			write(datalist, "line.mb88 88\nline2.mb88 88\n")
			output, code = mbrun([CMD, "--input=" * datalist, "--list"]; stderr_to_stdout = true)
			@test code == 0
			@test occursin("line.mb88.esf", output)
			@test occursin("line2.mb88.esf, not present", output)

			# an empty datalist is reported
			empty = joinpath(tmpdir, "empty.mb-1")
			touch(empty)
			output, code = mbrun([CMD, "--input=" * empty]; stderr_to_stdout = true)
			@test occursin("No swath files found", output)
		end
	end

	@testset "Prune" begin
		with_swath() do tmpdir, swath, esf, history
			make_versions(swath, esf)
			code, output = run_mbhistory(swath, "--prune", "--keep=1")
			@test occursin("2 tagged versions kept", output)
			@test occursin("nothing to prune", output)

			code, output = run_mbhistory(swath, "--prune", "--keep=1", "--force")
			@test occursin("would remove version 1", output)
			@test occursin("would remove version 2", output)
			@test length(manifest(history)) == 3

			code, output = run_mbhistory(swath, "--prune", "--keep=1", "--force", "--yes")
			@test code == 0
			@test [e[1] for e in manifest(history)] == ["3"]
			@test length(snapshots(history)) == 1
			tags = read(joinpath(history, "tags.txt"), String)
			@test !occursin("t1\t", tags)
			@test occursin("t3\t", tags)
		end
	end
end
