# Run the MB-System tests (Julia).
#
#     julia test/runtests.jl <build dir> [name filter ...]
#
# Every test/**/<name>_test.jl runs in its own Julia process, from its own directory, with
# MB_BUILD_DIR set to <build dir>; a line per file is printed as it finishes ([n/N] PASS|FAIL), and
# the run exits non-zero when any file failed. With filters, only the files whose name contains one
# of them run (e.g. "mbinfo" "utilities/").

isempty(ARGS) && error("usage: julia test/runtests.jl <build dir> [name filter ...]")
const BUILD = abspath(ARGS[1])
const FILTERS = ARGS[2:end]

files = String[]
for (root, _, names) in walkdir(@__DIR__)
	for n in names
		endswith(n, "_test.jl") && push!(files, joinpath(root, n))
	end
end
sort!(files)
isempty(FILTERS) || filter!(f -> any(p -> occursin(p, replace(f, '\\' => '/')), FILTERS), files)

failed = String[]
t0 = time()
for (i, f) in enumerate(files)
	name = relpath(f, @__DIR__)
	log = tempname()
	cmd = addenv(`$(Base.julia_cmd()) --startup-file=no $f`, "MB_BUILD_DIR" => BUILD)
	ok = success(pipeline(Cmd(cmd; dir = dirname(f)); stdout = log, stderr = log))
	println("[$i/$(length(files))] ", round(Int, time() - t0), "s  ", ok ? "PASS" : "FAIL", "  ", name)
	if !ok
		push!(failed, name)
		print(read(log, String))
	end
	rm(log; force = true)
end
println(length(files) - length(failed), " passed, ", length(failed), " failed")
isempty(failed) || (foreach(n -> println("  FAILED: ", n), failed); exit(1))
