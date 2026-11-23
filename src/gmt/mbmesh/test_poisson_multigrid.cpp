// The fork's octree multigrid solver of the screened Poisson equation (--poisson-solver multigrid) against SOR run to
// convergence, on soundings of a known surface: the same field near the samples, the same mesh where the soundings
// are, convergence in a few V-cycles, and the fixed nodes of chunked reconstruction kept.

#include "algorithms/marching_cubes.h"
#include "algorithms/screened_poisson.h"
#include "mbmesh_fast_poisson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        failures++;
    }
}

// A sea floor z = height(x, y), sampled every `spacing` over [0, extent]^2, normals up and out of it.
double height(double x, double y) {
    return 0.4 * std::sin(0.8 * x) + 0.3 * std::cos(0.6 * y);
}

OrientedPointCloud soundings(double extent, double spacing) {
    OrientedPointCloud points;
    for (double y = 0.0; y <= extent + 1e-9; y += spacing) {
        for (double x = 0.0; x <= extent + 1e-9; x += spacing) {
            const double dzdx = 0.32 * std::cos(0.8 * x);
            const double dzdy = -0.18 * std::sin(0.6 * y);
            points.emplace_back(Vec3(x, y, height(x, y)), normalize(Vec3(-dzdx, -dzdy, 1.0)));
        }
    }
    return points;
}

ScreenedPoissonOptions options_for(PoissonSolver solver, int iterations) {
    ScreenedPoissonOptions options;
    options.cell_size = 0.1;
    options.padding = 0.5;
    options.normal_splat_radius = 0.25;
    options.screening_weight = 4.0;
    options.solver = solver;
    options.solver_iterations = iterations;
    options.threads = 4;
    return options;
}

// Trilinear value of a grid at a point inside it.
double sample(const ScalarGrid3D &grid, const Vec3 &p) {
    const Vec3 g = (p - grid.origin) / grid.cell_size;
    const int x = std::clamp(static_cast<int>(std::floor(g.x)), 0, static_cast<int>(grid.nx) - 2);
    const int y = std::clamp(static_cast<int>(std::floor(g.y)), 0, static_cast<int>(grid.ny) - 2);
    const int z = std::clamp(static_cast<int>(std::floor(g.z)), 0, static_cast<int>(grid.nz) - 2);
    const double tx = g.x - x;
    const double ty = g.y - y;
    const double tz = g.z - z;
    double v = 0.0;
    for (int c = 0; c < 8; c++) {
        const int dx = c & 1;
        const int dy = (c >> 1) & 1;
        const int dz = (c >> 2) & 1;
        v += grid.at(x + dx, y + dy, z + dz) * (dx ? tx : 1.0 - tx) * (dy ? ty : 1.0 - ty) * (dz ? tz : 1.0 - tz);
    }
    return v;
}

} // namespace

int main() {
    const OrientedPointCloud points = soundings(6.0, 0.1);

    PoissonDiagnostics sor_report;
    PoissonDiagnostics mg_report;
    const ScalarGrid3D sor = screened_poisson(points, options_for(PoissonSolver::Sor, 3000), &sor_report);
    const ScalarGrid3D mg = screened_poisson(points, options_for(kPoissonSolverMultigrid, 0), &mg_report);

    check(sor.nx == mg.nx && sor.ny == mg.ny && sor.nz == mg.nz, "the same grid");
    check(mg_report.solver == kPoissonSolverMultigrid && mbmesh_fast_poisson_last_report().levels >= 3, "a hierarchy of levels");
    check(mg_report.iterations > 0 && mg_report.iterations <= 20, "a few V-cycles");
    check(mg_report.residual_final >= 0.0 && mg_report.residual_final <= 1.0e-4, "the residual below the tolerance");
    check(mbmesh_fast_poisson_last_report().refined_nodes > 0 && mbmesh_fast_poisson_last_report().refined_nodes < mg_report.cells, "refined near the samples only");
    std::printf("multigrid: %d levels, %d V-cycles, residual %.2e -> %.2e, %lld of %lld cells refined; sor residual %.2e\n",
                mbmesh_fast_poisson_last_report().levels, mg_report.iterations, mbmesh_fast_poisson_last_report().residual_start, mg_report.residual_final,
                mbmesh_fast_poisson_last_report().refined_nodes, mg_report.cells, sor_report.residual_final);

    // The field across the surface, one cell above and below each sounding: the same sign and nearly the same values.
    double worst = 0.0;
    double scale = 0.0;
    int signs = 0;
    for (const OrientedPoint &p : points) {
        if (p.point.x < 1.0 || p.point.x > 5.0 || p.point.y < 1.0 || p.point.y > 5.0) {
            continue;
        }
        for (double dz : {-0.1, 0.1}) {
            const Vec3 q = p.point + Vec3(0.0, 0.0, dz);
            const double a = sample(sor, q);
            const double b = sample(mg, q);
            worst = std::max(worst, std::fabs(a - b));
            scale = std::max(scale, std::fabs(a));
            if ((a > 0.0) != (b > 0.0)) {
                signs++;
            }
        }
    }
    std::printf("field near the soundings: largest difference %.3e of a range %.3e, %d sign changes\n", worst, scale, signs);
    check(signs == 0, "the same side of the surface everywhere near the soundings");
    check(worst <= 0.05 * scale, "the same field near the soundings, within 5% of its range");

    // The meshes over the soundings: the vertices on the sampled surface (marching cubes also finds the sheets Poisson
    // makes away from the data, which support trimming removes), how many and how far from it, the same for both.
    const auto on_surface = [](const Mesh &mesh, int *count, double *far) {
        *count = 0;
        *far = 0.0;
        for (const Vec3 &v : mesh.vertices) {
            if (v.x < 1.0 || v.x > 5.0 || v.y < 1.0 || v.y > 5.0 || std::fabs(v.z - height(v.x, v.y)) > 0.5) {
                continue;
            }
            *far = std::max(*far, std::fabs(v.z - height(v.x, v.y)));
            (*count)++;
        }
    };
    int mg_count = 0;
    int sor_count = 0;
    double mg_far = 0.0;
    double sor_far = 0.0;
    on_surface(marching_cubes(mg, MarchingCubesOptions()), &mg_count, &mg_far);
    on_surface(marching_cubes(sor, MarchingCubesOptions()), &sor_count, &sor_far);
    std::printf("mesh on the sampled surface: multigrid %d vertices, farthest %.4f; sor %d vertices, farthest %.4f\n", mg_count,
                mg_far, sor_count, sor_far);
    check(mg_count > 1000 && std::abs(mg_count - sor_count) <= sor_count / 100, "as many vertices on the surface as sor, within 1%");
    check(mg_far <= sor_far + 0.005, "the mesh as close to the sampled surface as sor's");

    // Fixed nodes, as chunked reconstruction gives them: kept as they are.
    ScreenedPoissonOptions fixed_options = options_for(kPoissonSolverMultigrid, 0);
    fixed_options.fix_nodes = [](ScalarGrid3D *grid, std::vector<unsigned char> *fixed) -> std::size_t {
        std::size_t n = 0;
        for (int z = 1; z < static_cast<int>(grid->nz) - 1; z++) {
            for (int y = 1; y < static_cast<int>(grid->ny) - 1; y++) {
                grid->at(1, y, z) = 0.25;
                (*fixed)[grid->index(1, y, z)] = 1;
                n++;
            }
        }
        return n;
    };
    const ScalarGrid3D kept = screened_poisson(points, fixed_options);
    bool all_kept = true;
    for (int z = 1; z < static_cast<int>(kept.nz) - 1; z++) {
        for (int y = 1; y < static_cast<int>(kept.ny) - 1; y++) {
            all_kept = all_kept && kept.at(1, y, z) == 0.25;
        }
    }
    check(all_kept, "the fixed nodes keep their values");

    // Planes fixed inside the grid, as chunked reconstruction fixes the field of a neighbor on the edge it shares: at an
    // odd and at an even node index (an odd one has no twin on the coarser grids), holding a solution shifted by a
    // constant, as a neighbor's field is in its own gauge. Measured against sor run to convergence, multigrid must be at
    // least as close to it as sor with its own default number of iterations, which is what mbmesh has used until now.
    for (int plane : {17, 30}) {
        const auto fix_plane = [&sor, plane](ScalarGrid3D *grid, std::vector<unsigned char> *fixed) -> std::size_t {
            std::size_t n = 0;
            for (int z = 0; z < static_cast<int>(grid->nz); z++) {
                for (int y = 0; y < static_cast<int>(grid->ny); y++) {
                    grid->at(plane, y, z) = sor.at(plane, y, z) + 50.0;
                    (*fixed)[grid->index(plane, y, z)] = 1;
                    n++;
                }
            }
            return n;
        };
        ScreenedPoissonOptions converged = options_for(PoissonSolver::Sor, 3000);
        ScreenedPoissonOptions usual = options_for(PoissonSolver::Sor, 0);
        ScreenedPoissonOptions mg_fixed = options_for(kPoissonSolverMultigrid, 0);
        converged.fix_nodes = fix_plane;
        usual.fix_nodes = fix_plane;
        mg_fixed.fix_nodes = fix_plane;
        PoissonDiagnostics fixed_report;
        const ScalarGrid3D reference = screened_poisson(points, converged);
        const ScalarGrid3D by_sor = screened_poisson(points, usual);
        const ScalarGrid3D by_mg = screened_poisson(points, mg_fixed, &fixed_report);
        double sor_error = 0.0;
        double mg_error = 0.0;
        for (const OrientedPoint &p : points) {
            for (double dz : {-0.1, 0.1}) {
                const Vec3 q = p.point + Vec3(0.0, 0.0, dz);
                const double r = sample(reference, q);
                sor_error = std::max(sor_error, std::fabs(sample(by_sor, q) - r));
                mg_error = std::max(mg_error, std::fabs(sample(by_mg, q) - r));
            }
        }
        std::printf("plane fixed at x node %d: multigrid %d V-cycles, residual %.2e; largest difference near the soundings to "
                    "converged sor: multigrid %.3e, sor with its default iterations %.3e\n",
                    plane, fixed_report.iterations, fixed_report.residual_final, mg_error, sor_error);
        check(fixed_report.residual_final <= 1.0e-4 && fixed_report.iterations <= 20, "converged with a plane of fixed nodes");
        check(mg_error <= sor_error, "with a plane of fixed nodes, at least as close to the converged field as sor");
    }

    check(std::string(poisson_solver_names()).find("multigrid") != std::string::npos, "multigrid among the solver names");
    PoissonSolver parsed = PoissonSolver::Sor;
    check(parse_poisson_solver("multigrid", &parsed) && parsed == kPoissonSolverMultigrid, "--poisson-solver multigrid");

    if (failures == 0) {
        std::printf("poisson multigrid tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
