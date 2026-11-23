#pragma once

#include <cstddef>
#include <vector>

// An octree multigrid solver for the screened Poisson grid equation of screened_poisson() (--poisson-solver multigrid).
//
// The equation at an interior node of a grid of spacing h is
//     (6 + h^2 c) u - (sum of the six neighbors of u) = h^2 F
// with F = -div(normals) + c * target and c >= 0 the screening coefficient; boundary nodes, and nodes marked fixed, keep
// the values they have.
//
// The grid is coarsened by two again and again into a hierarchy of levels. Like an octree, a level is refined, that is
// solved on, only near the samples: within refine_radius of one (a little more on the coarser levels, so that each
// level covers the refined region of the finer one), the coarsest level everywhere. Away from the samples a level's
// values are those of the coarser level, interpolated, which is what an octree leaf there stands for. The levels are
// solved together by the full approximation scheme (FAS): V-cycles of red-black Gauss-Seidel smoothing, the finer level's
// residual and solution restricted to the coarser one where it is refined, its correction interpolated back; started by
// nested iteration from the coarsest level (full multigrid). The cost of a cycle is proportional to the refined nodes,
// not to the volume, and the number of cycles does not grow with the grid.
//
// The solution of the finest level is returned on the whole grid: solved on the refined nodes, interpolated from the
// coarser levels elsewhere. Away from the samples, where there is no surface, it is the solution of a coarser grid.

struct MultigridReport {
    int levels = 0;                   // in the hierarchy, the finest included
    int cycles = 0;                   // V-cycles on the finest level, after the full multigrid start
    long long refined_nodes = 0;      // nodes solved on the finest level
    double residual_start = -1.0;     // relative residual on the refined nodes of the finest level, after the start
    double residual_final = -1.0;     // and at the end: the 2-norm of h^2 F - A u over them, divided by that of h^2 F
};

// u: the field, nx * ny * nz values, x fastest; boundary and fixed nodes hold their values on entry, the rest is
// overwritten. source: F per node. screening: c per node. Both are taken (moved from) and used as the finest level's
// storage, so that the solver adds no copy of them. fixed: one byte per node, 1 = keep (may be empty).
// occupied: one byte per node, 1 = a sample is nearest that node. refine_radius: in the units of cell_size.
// cycles: the V-cycles to run, or 0 to run them until the relative residual is below tolerance (at most max_cycles).
void poisson_multigrid_solve(std::vector<double> *u, int nx, int ny, int nz, double cell_size, std::vector<double> &&source,
                             std::vector<double> &&screening, const std::vector<unsigned char> &fixed,
                             std::vector<unsigned char> &&occupied, double refine_radius, int cycles, double tolerance,
                             int max_cycles, int threads, MultigridReport *report);
