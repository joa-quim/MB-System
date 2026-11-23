#include "poisson_multigrid.h"

#include "util/parallel.h"

#include <algorithm>
#include <cmath>

// See poisson_multigrid.h for what this solves and how.

namespace {

// One level of the hierarchy. Values are stored x fastest, as in ScalarGrid3D.
struct Level {
	int nx = 0;
	int ny = 0;
	int nz = 0;
	double h = 1.0;
	std::vector<double> u;               // the solution (FAS: the full field, not a correction)
	std::vector<double> f;               // right-hand side of this level's equation, scaled by h^2: this level's own
	                                     // discretization (F restricted from the finer levels), except on the covered
	                                     // nodes, where each cycle puts the FAS right-hand side
	std::vector<double> diag;            // 6 + h^2 c
	std::vector<double> saved;           // u before the coarse cycle, to take the correction from
	std::vector<double> r;               // the residual, on the refined nodes (zero elsewhere)
	std::vector<unsigned char> fixed;    // boundary nodes and the caller's fixed nodes
	std::vector<unsigned char> occupied; // a sample is near
	std::vector<unsigned char> active;   // refined: solved on this level
	std::vector<long long> smooth[2];    // the refined nodes, by colour (x + y + z even, odd)
	std::vector<long long> ring;         // unrefined, unfixed nodes next to a refined one: the values the smoothing leans on
	std::vector<long long> covered;      // unfixed nodes whose twin on the finer level is refined
	std::vector<long long> frozen;       // interior nodes held because a fixed node of the finer level is in their stencil

	long long size() const { return static_cast<long long>(nx) * ny * nz; }
	long long index(int x, int y, int z) const { return (static_cast<long long>(z) * ny + y) * nx + x; }
	void coords(long long i, int *x, int *y, int *z) const {
		*x = static_cast<int>(i % nx);
		*y = static_cast<int>((i / nx) % ny);
		*z = static_cast<int>(i / (static_cast<long long>(nx) * ny));
	}
	bool interior(int x, int y, int z) const { return x > 0 && y > 0 && z > 0 && x < nx - 1 && y < ny - 1 && z < nz - 1; }
	double neighbors(const std::vector<double> &v, long long i) const {
		const long long sxy = static_cast<long long>(nx) * ny;
		return v[i - 1] + v[i + 1] + v[i - nx] + v[i + nx] + v[i - sxy] + v[i + sxy];
	}
};

// Box dilation of a mask by b nodes along each axis (the nodes within Chebyshev distance b of a set one).
void dilate(std::vector<unsigned char> *mask, int nx, int ny, int nz, int b) {
	if (b <= 0) {
		return;
	}
	std::vector<int> prefix;
	const auto line = [&](unsigned char *base, long long stride, int length) {
		prefix.assign(static_cast<std::size_t>(length) + 1, 0);
		for (int k = 0; k < length; k++) {
			prefix[k + 1] = prefix[k] + (base[k * stride] != 0 ? 1 : 0);
		}
		for (int k = 0; k < length; k++) {
			const int lo = std::max(0, k - b);
			const int hi = std::min(length, k + b + 1);
			base[k * stride] = prefix[hi] - prefix[lo] > 0 ? 1 : 0;
		}
	};
	unsigned char *m = mask->data();
	const long long sxy = static_cast<long long>(nx) * ny;
	for (int z = 0; z < nz; z++) {
		for (int y = 0; y < ny; y++) {
			line(m + z * sxy + static_cast<long long>(y) * nx, 1, nx);
		}
	}
	for (int z = 0; z < nz; z++) {
		for (int x = 0; x < nx; x++) {
			line(m + z * sxy + x, nx, ny);
		}
	}
	for (int y = 0; y < ny; y++) {
		for (int x = 0; x < nx; x++) {
			line(m + static_cast<long long>(y) * nx + x, sxy, nz);
		}
	}
}

// Full weighting of a fine field at the fine node (x, y, z), which is interior: weights 1/2 on the axis, 1/4 off it.
double full_weighting(const Level &fine, const std::vector<double> &v, int x, int y, int z) {
	static const double w[3] = {0.25, 0.5, 0.25};
	double sum = 0.0;
	for (int dz = -1; dz <= 1; dz++) {
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				sum += w[dx + 1] * w[dy + 1] * w[dz + 1] * v[fine.index(x + dx, y + dy, z + dz)];
			}
		}
	}
	return sum;
}

// Trilinear interpolation of a coarse field at the fine node (x, y, z): the fine node 2I is the coarse node I.
template <typename Value>
double interpolate(const Level &coarse, int x, int y, int z, const Value &value) {
	const int x0 = x >> 1;
	const int y0 = y >> 1;
	const int z0 = z >> 1;
	const int x1 = (x & 1) ? x0 + 1 : x0;
	const int y1 = (y & 1) ? y0 + 1 : y0;
	const int z1 = (z & 1) ? z0 + 1 : z0;
	const int xs[2] = {x0, x1};
	const int ys[2] = {y0, y1};
	const int zs[2] = {z0, z1};
	double sum = 0.0;
	for (int c = 0; c < 8; c++) {
		sum += value(coarse.index(xs[c & 1], ys[(c >> 1) & 1], zs[(c >> 2) & 1]));
	}
	return sum / 8.0;
}

class Hierarchy {
public:
	std::vector<Level> levels;
	int threads = 1;

	// Runs body over the entries of a list, split between the threads when it is long enough to pay for them.
	void for_list(const std::vector<long long> &list, const std::function<void(long long)> &body) const {
		const long n = static_cast<long>(list.size());
		const int count = n > 40000 ? threads : 1;
		mbmesh::parallel_for_ranges(0, n, count, [&](long begin, long end) {
			for (long k = begin; k < end; k++) {
				body(list[static_cast<std::size_t>(k)]);
			}
		});
	}

	// Red-black Gauss-Seidel on the refined nodes. A node's six neighbors all have the other colour, so each colour is
	// updated in place and the result does not depend on the order or on the number of threads.
	void smooth(Level &level, int sweeps) const {
		for (int s = 0; s < sweeps; s++) {
			for (int colour = 0; colour < 2; colour++) {
				for_list(level.smooth[colour], [&level](long long i) {
					level.u[i] = (level.neighbors(level.u, i) + level.f[i]) / level.diag[i];
				});
			}
		}
	}

	void residual(Level &level) const {
		for (int colour = 0; colour < 2; colour++) {
			for_list(level.smooth[colour], [&level](long long i) {
				level.r[i] = level.f[i] - (level.diag[i] * level.u[i] - level.neighbors(level.u, i));
			});
		}
	}

	// Relative residual on the refined nodes.
	double relative_residual(Level &level) const {
		residual(level);
		double rr = 0.0;
		double ff = 0.0;
		for (int colour = 0; colour < 2; colour++) {
			for (long long i : level.smooth[colour]) {
				rr += level.r[i] * level.r[i];
				ff += level.f[i] * level.f[i];
			}
		}
		return ff > 0.0 ? std::sqrt(rr / ff) : 0.0;
	}

	// The coarser level's values, interpolated, on the unrefined nodes of `fine` that the smoothing leans on (`ring`),
	// or on all of them (`all`, at the start and at the end).
	void interpolate_unrefined(Level &fine, const Level &coarse, bool all) const {
		const auto set = [&](long long i) {
			int x = 0;
			int y = 0;
			int z = 0;
			fine.coords(i, &x, &y, &z);
			fine.u[i] = interpolate(coarse, x, y, z, [&coarse](long long j) { return coarse.u[j]; });
		};
		if (!all) {
			for_list(fine.ring, set);
			return;
		}
		for (int z = 1; z < fine.nz - 1; z++) {
			for (int y = 1; y < fine.ny - 1; y++) {
				for (int x = 1; x < fine.nx - 1; x++) {
					const long long i = fine.index(x, y, z);
					if (fine.fixed[i] == 0 && fine.active[i] == 0) {
						set(i);
					}
				}
			}
		}
	}

	void cycle(std::size_t l) {
		Level &fine = levels[l];
		if (l + 1 == levels.size()) {
			smooth(fine, 60);
			return;
		}
		Level &coarse = levels[l + 1];
		smooth(fine, 2);
		residual(fine);
		// The coarse nodes held by the finer level's fixed nodes take its solution there, restricted: the boundary
		// values of the coarse problem. Their correction is zero, as for any fixed node.
		for_list(coarse.frozen, [&](long long I) {
			int x = 0;
			int y = 0;
			int z = 0;
			coarse.coords(I, &x, &y, &z);
			const long long twin = fine.index(2 * x, 2 * y, 2 * z);
			coarse.u[I] = fine.fixed[twin] != 0 ? fine.u[twin] : full_weighting(fine, fine.u, 2 * x, 2 * y, 2 * z);
		});
		// FAS: where the fine level is refined, the coarse one takes its solution and is corrected by its residual.
		for_list(coarse.covered, [&](long long I) {
			int x = 0;
			int y = 0;
			int z = 0;
			coarse.coords(I, &x, &y, &z);
			coarse.u[I] = fine.u[fine.index(2 * x, 2 * y, 2 * z)];
		});
		for_list(coarse.covered, [&](long long I) {
			int x = 0;
			int y = 0;
			int z = 0;
			coarse.coords(I, &x, &y, &z);
			coarse.f[I] = coarse.diag[I] * coarse.u[I] - coarse.neighbors(coarse.u, I) +
			              4.0 * full_weighting(fine, fine.r, 2 * x, 2 * y, 2 * z);
		});
		coarse.saved = coarse.u;
		cycle(l + 1);
		// The correction on the refined nodes, the coarse values on the ones around them.
		for (int colour = 0; colour < 2; colour++) {
			for_list(fine.smooth[colour], [&](long long i) {
				int x = 0;
				int y = 0;
				int z = 0;
				fine.coords(i, &x, &y, &z);
				fine.u[i] += interpolate(coarse, x, y, z, [&coarse](long long j) { return coarse.u[j] - coarse.saved[j]; });
			});
		}
		interpolate_unrefined(fine, coarse, false);
		smooth(fine, 2);
	}
};

} // namespace

void poisson_multigrid_solve(std::vector<double> *u, int nx, int ny, int nz, double cell_size, std::vector<double> &&source,
                             std::vector<double> &&screening, const std::vector<unsigned char> &fixed,
                             std::vector<unsigned char> &&occupied, double refine_radius, int cycles, double tolerance,
                             int max_cycles, int threads, MultigridReport *report) {
	Hierarchy hierarchy;
	hierarchy.threads = std::max(1, threads);
	std::vector<Level> &levels = hierarchy.levels;

	// The finest level, in the caller's storage: the field, and F and c turned in place into f and the diagonal.
	{
		Level level;
		level.nx = nx;
		level.ny = ny;
		level.nz = nz;
		level.h = cell_size;
		level.u.swap(*u);
		level.f = std::move(source);
		level.diag = std::move(screening);
		const long long n = level.size();
		level.fixed.assign(n, 0);
		const double h2 = cell_size * cell_size;
		for (long long i = 0; i < n; i++) {
			level.f[i] *= h2;
			level.diag[i] = 6.0 + h2 * level.diag[i];
		}
		for (int z = 0; z < nz; z++) {
			for (int y = 0; y < ny; y++) {
				for (int x = 0; x < nx; x++) {
					const long long i = level.index(x, y, z);
					if (!level.interior(x, y, z) || (!fixed.empty() && fixed[i] != 0)) {
						level.fixed[i] = 1;
					}
				}
			}
		}
		level.occupied = std::move(occupied);
		levels.push_back(std::move(level));
	}

	// The coarser levels: half the nodes along each axis (the fine node 2I is the coarse node I), until an axis would
	// have fewer than three. F and c are restricted by full weighting and discretized again at the coarser spacing: with
	// h doubled, h^2 F is 4 times the restriction of the finer h^2 F, and h^2 c 4 times that of the finer diagonal less 6
	// (the weights add up to 1).
	while (std::min({levels.back().nx, levels.back().ny, levels.back().nz}) >= 5 && levels.size() < 16) {
		const Level &fine = levels.back();
		Level coarse;
		coarse.nx = (fine.nx - 1) / 2 + 1;
		coarse.ny = (fine.ny - 1) / 2 + 1;
		coarse.nz = (fine.nz - 1) / 2 + 1;
		coarse.h = 2.0 * fine.h;
		const long long n = coarse.size();
		coarse.f.assign(n, 0.0);
		coarse.diag.assign(n, 6.0);
		coarse.u.assign(n, 0.0);
		coarse.fixed.assign(n, 0);
		coarse.occupied.assign(n, 0);
		for (int z = 0; z < coarse.nz; z++) {
			for (int y = 0; y < coarse.ny; y++) {
				for (int x = 0; x < coarse.nx; x++) {
					const long long I = coarse.index(x, y, z);
					const long long twin = fine.index(2 * x, 2 * y, 2 * z);
					coarse.u[I] = fine.u[twin];
					// A fixed node of the finer level anywhere in the stencil of an interior coarse node holds the coarse
					// node too (frozen): a plane of fixed nodes at an odd index has no twin on the coarse grid, and the
					// coarse problem must still see it. It starts at the mean of those fixed values.
					int fixed_seen = 0;
					double fixed_sum = 0.0;
					if (coarse.interior(x, y, z)) {
						for (int dz = -1; dz <= 1; dz++) {
							for (int dy = -1; dy <= 1; dy++) {
								for (int dx = -1; dx <= 1; dx++) {
									const long long j = fine.index(2 * x + dx, 2 * y + dy, 2 * z + dz);
									const bool fine_boundary = !fine.interior(2 * x + dx, 2 * y + dy, 2 * z + dz);
									if (fine.fixed[j] != 0 && !fine_boundary) {
										fixed_seen++;
										fixed_sum += fine.u[j];
									}
								}
							}
						}
					}
					if (!coarse.interior(x, y, z) || fine.fixed[twin] != 0 || fixed_seen > 0) {
						coarse.fixed[I] = 1;
						if (coarse.interior(x, y, z) && fine.fixed[twin] == 0) {
							coarse.u[I] = fixed_sum / fixed_seen;
						}
					}
					else {
						coarse.f[I] = 4.0 * full_weighting(fine, fine.f, 2 * x, 2 * y, 2 * z);
						coarse.diag[I] = 6.0 + 4.0 * (full_weighting(fine, fine.diag, 2 * x, 2 * y, 2 * z) - 6.0);
					}
					for (int dz = -1; dz <= 1 && coarse.occupied[I] == 0; dz++) {
						for (int dy = -1; dy <= 1 && coarse.occupied[I] == 0; dy++) {
							for (int dx = -1; dx <= 1; dx++) {
								const int fx = 2 * x + dx;
								const int fy = 2 * y + dy;
								const int fz = 2 * z + dz;
								if (fx >= 0 && fy >= 0 && fz >= 0 && fx < fine.nx && fy < fine.ny && fz < fine.nz &&
								    fine.occupied[fine.index(fx, fy, fz)] != 0) {
									coarse.occupied[I] = 1;
									break;
								}
							}
						}
					}
				}
			}
		}
		levels.push_back(std::move(coarse));
	}

	// The refinement: near the samples on every level but the coarsest, which is solved everywhere; and every node of a
	// coarser level whose twin is refined on the finer one, so that each level covers the refined region of the next.
	const std::size_t last = levels.size() - 1;
	bool interior_fixed = false;
	for (int z = 1; z < nz - 1 && !interior_fixed && !fixed.empty(); z++) {
		for (int y = 1; y < ny - 1 && !interior_fixed; y++) {
			for (int x = 1; x < nx - 1; x++) {
				if (fixed[levels[0].index(x, y, z)] != 0) {
					interior_fixed = true;
					break;
				}
			}
		}
	}
	for (std::size_t l = 0; l < levels.size(); l++) {
		Level &level = levels[l];
		if (l == last) {
			level.active.assign(level.size(), 1);
			level.occupied = std::vector<unsigned char>();
		}
		else {
			level.active = std::move(level.occupied);   // the coarser levels have taken theirs from it already
			if (l == 0 && interior_fixed) {
				// Fixed nodes inside the grid (chunked reconstruction's planes of a neighbor's field) hold values in the
				// neighbor's gauge, which make the field far from small away from the samples too: there a coarser grid
				// is measurably less accurate than sor (test_poisson_multigrid), so the finest grid is solved everywhere.
				std::fill(level.active.begin(), level.active.end(), static_cast<unsigned char>(1));
			}
			else {
				// two nodes more on the coarser levels: the nodes the finer level's ring interpolates from are refined too
				dilate(&level.active, level.nx, level.ny, level.nz, static_cast<int>(std::ceil(refine_radius / level.h)) + (l > 0 ? 2 : 0));
			}
		}
		if (l > 0) {
			const Level &fine = levels[l - 1];
			for (int z = 0; z < level.nz; z++) {
				for (int y = 0; y < level.ny; y++) {
					for (int x = 0; x < level.nx; x++) {
						if (fine.active[fine.index(2 * x, 2 * y, 2 * z)] != 0) {
							level.active[level.index(x, y, z)] = 1;
						}
					}
				}
			}
		}
		for (long long i = 0; i < level.size(); i++) {
			if (level.fixed[i] != 0) {
				level.active[i] = 0;
			}
		}
	}

	// The node lists, the right-hand sides and the residual arrays.
	for (std::size_t l = 0; l < levels.size(); l++) {
		Level &level = levels[l];
		if (l < last || l == 0) {
			level.r.assign(level.size(), 0.0);   // a coarsest level above the finest has no residual to restrict
		}
		for (int z = 1; z < level.nz - 1; z++) {
			for (int y = 1; y < level.ny - 1; y++) {
				for (int x = 1; x < level.nx - 1; x++) {
					const long long i = level.index(x, y, z);
					if (level.fixed[i] != 0) {
						if (l > 0) {
							level.frozen.push_back(i);   // an interior node held for the finer level's fixed nodes
						}
						continue;
					}
					if (level.active[i] != 0) {
						level.smooth[(x + y + z) & 1].push_back(i);
					}
					else if (level.active[i - 1] || level.active[i + 1] || level.active[i - level.nx] || level.active[i + level.nx] ||
					         level.active[i - static_cast<long long>(level.nx) * level.ny] ||
					         level.active[i + static_cast<long long>(level.nx) * level.ny]) {
						level.ring.push_back(i);
					}
					if (l > 0 && levels[l - 1].active[levels[l - 1].index(2 * x, 2 * y, 2 * z)] != 0) {
						level.covered.push_back(i);
					}
				}
			}
		}
	}

	// Full multigrid start: the coarsest level solved, then each finer one started from the coarser solution and given
	// one V-cycle.
	hierarchy.smooth(levels[last], 200);
	for (std::size_t l = last; l-- > 0;) {
		Level &fine = levels[l];
		const Level &coarse = levels[l + 1];
		for (int colour = 0; colour < 2; colour++) {
			for (long long i : fine.smooth[colour]) {
				int x = 0;
				int y = 0;
				int z = 0;
				fine.coords(i, &x, &y, &z);
				fine.u[i] = interpolate(coarse, x, y, z, [&coarse](long long j) { return coarse.u[j]; });
			}
		}
		hierarchy.interpolate_unrefined(fine, coarse, true);
		hierarchy.cycle(l);
	}

	MultigridReport result;
	result.levels = static_cast<int>(levels.size());
	result.refined_nodes = static_cast<long long>(levels[0].smooth[0].size() + levels[0].smooth[1].size());
	double residual = hierarchy.relative_residual(levels[0]);
	result.residual_start = residual;
	const int wanted = cycles > 0 ? cycles : std::max(1, max_cycles);
	while (result.cycles < wanted && (cycles > 0 || residual > tolerance)) {
		hierarchy.cycle(0);
		result.cycles++;
		residual = hierarchy.relative_residual(levels[0]);
	}
	result.residual_final = residual;

	// Every unrefined node of every level from its coarser level, coarsest first.
	for (std::size_t l = last; l-- > 0;) {
		hierarchy.interpolate_unrefined(levels[l], levels[l + 1], true);
	}

	u->swap(levels[0].u);
	if (report != nullptr) {
		*report = result;
	}
}
