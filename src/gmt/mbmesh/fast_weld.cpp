#include "fast_weld.h"

#include <cmath>
#include <functional>
#include <cstdint>
#include <algorithm>
#include <thread>
#include <vector>

#include "util/parallel.h"

namespace {

struct Cell {
    long long x, y, z;
};

std::uint64_t cell_hash(const Cell &c) {
    std::uint64_t h = static_cast<std::uint64_t>(c.x) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(c.y) * 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
    h ^= static_cast<std::uint64_t>(c.z) * 0x165667B19E3779F9ULL + (h << 6) + (h >> 2);
    return h ^ (h >> 29);
}

// Open addressing: per occupied cell its key and the first and last of its vertices, which are chained in the order
// they were added (next_in_cell), as upstream's vector per cell holds them.
class CellTable {
public:
    explicit CellTable(std::size_t count) {
        std::size_t size = 16;
        while (size < 2 * count) {
            size *= 2;
        }
        mask_ = size - 1;
        keys_.resize(size);
        head_.assign(size, kEmpty);
        tail_.assign(size, kEmpty);
    }
    static constexpr unsigned int kEmpty = 0xFFFFFFFFu;

    // The slot of the cell, or of the empty place where it would go.
    std::size_t slot(const Cell &c) const {
        std::size_t s = static_cast<std::size_t>(cell_hash(c)) & mask_;
        while (head_[s] != kEmpty && !(keys_[s].x == c.x && keys_[s].y == c.y && keys_[s].z == c.z)) {
            s = (s + 1) & mask_;
        }
        return s;
    }
    unsigned int head(std::size_t s) const { return head_[s]; }
    void append(std::size_t s, const Cell &c, unsigned int vertex, std::vector<unsigned int> *next_in_cell) {
        if (head_[s] == kEmpty) {
            keys_[s] = c;
            head_[s] = vertex;
        }
        else {
            (*next_in_cell)[tail_[s]] = vertex;
        }
        tail_[s] = vertex;
    }

private:
    std::size_t mask_ = 0;
    std::vector<Cell> keys_;
    std::vector<unsigned int> head_;
    std::vector<unsigned int> tail_;
};

}  // namespace

std::size_t mbmesh_fast_weld_coincident_vertices(Mesh *mesh, double tolerance) {
    if (mesh == nullptr || mesh->vertices.empty() || !(tolerance > 0.0)) {
        return 0;
    }
    const std::size_t count = mesh->vertices.size();
    const bool had_normals = mesh->normals.size() == count;
    const double tolerance_squared = tolerance * tolerance;

    // A vertex can only be merged, or have others merged into it, if another vertex is within the tolerance: those
    // candidates, found first in parallel among all the vertices (sorted by cell), are the only ones the sequential
    // pass below has to see. It sees them in their order, as upstream sees all of them, so every merge is the same.
    std::vector<Cell> keys(count);
    const int threads = static_cast<int>(std::max(1u, std::min(16u, std::thread::hardware_concurrency())));
    const auto parallel = [&](std::size_t n, const std::function<void(long, long)> &body) {
        mbmesh::parallel_for_ranges(0, static_cast<long>(n), n > 50000 ? threads : 1, body);
    };
    parallel(count, [&](long begin, long end) {
        for (long n = begin; n < end; n++) {
            const Vec3 &v = mesh->vertices[static_cast<std::size_t>(n)];
            keys[static_cast<std::size_t>(n)] = Cell{static_cast<long long>(std::floor(v.x / tolerance)),
                                                     static_cast<long long>(std::floor(v.y / tolerance)),
                                                     static_cast<long long>(std::floor(v.z / tolerance))};
        }
    });
    const auto less = [](const Cell &a, const Cell &b) {
        return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z);
    };
    std::vector<unsigned int> by_cell(count);
    for (std::size_t n = 0; n < count; n++) {
        by_cell[n] = static_cast<unsigned int>(n);
    }
    std::sort(by_cell.begin(), by_cell.end(), [&](unsigned int a, unsigned int b) {
        return less(keys[a], keys[b]) || (!less(keys[b], keys[a]) && a < b);
    });
    std::vector<unsigned char> candidate(count, 0);
    parallel(count, [&](long begin, long end) {
        for (long n = begin; n < end; n++) {
            const Cell &c = keys[static_cast<std::size_t>(n)];
            const Vec3 &v = mesh->vertices[static_cast<std::size_t>(n)];
            bool near = false;
            for (long long dx = -1; dx <= 1 && !near; dx++) {
                for (long long dy = -1; dy <= 1 && !near; dy++) {
                    for (long long dz = -1; dz <= 1 && !near; dz++) {
                        const Cell want{c.x + dx, c.y + dy, c.z + dz};
                        auto it = std::lower_bound(by_cell.begin(), by_cell.end(), want,
                                                   [&](unsigned int a, const Cell &w) { return less(keys[a], w); });
                        for (; it != by_cell.end() && !less(want, keys[*it]); ++it) {
                            if (*it == static_cast<unsigned int>(n)) {
                                continue;
                            }
                            const Vec3 &o = mesh->vertices[*it];
                            const double ex = v.x - o.x, ey = v.y - o.y, ez = v.z - o.z;
                            if (ex * ex + ey * ey + ez * ez <= tolerance_squared) {
                                near = true;
                                break;
                            }
                        }
                    }
                }
            }
            candidate[static_cast<std::size_t>(n)] = near ? 1 : 0;
        }
    });
    by_cell = std::vector<unsigned int>();

    // The sequential pass of upstream over the candidates only: each merged into the first earlier kept candidate
    // within the tolerance, found in upstream's order (cells -1..1 in x, then y, then z; in a cell, the order added).
    std::size_t candidates = 0;
    for (std::size_t n = 0; n < count; n++) {
        candidates += candidate[n];
    }
    CellTable cells(std::max<std::size_t>(candidates, 1));
    std::vector<unsigned int> next_in_cell(count, CellTable::kEmpty);
    std::vector<unsigned int> merged_into(count);
    std::size_t merged = 0;
    for (std::size_t n = 0; n < count; n++) {
        merged_into[n] = static_cast<unsigned int>(n);
        if (candidate[n] == 0) {
            continue;
        }
        const Vec3 &v = mesh->vertices[n];
        const Cell &cell = keys[n];
        bool found = false;
        for (long long dx = -1; dx <= 1 && !found; dx++) {
            for (long long dy = -1; dy <= 1 && !found; dy++) {
                for (long long dz = -1; dz <= 1 && !found; dz++) {
                    const unsigned int first = cells.head(cells.slot(Cell{cell.x + dx, cell.y + dy, cell.z + dz}));
                    for (unsigned int other = first; other != CellTable::kEmpty; other = next_in_cell[other]) {
                        const Vec3 &o = mesh->vertices[other];
                        const double ex = v.x - o.x, ey = v.y - o.y, ez = v.z - o.z;
                        if (ex * ex + ey * ey + ez * ez <= tolerance_squared) {
                            merged_into[n] = other;
                            found = true;
                            merged++;
                            break;
                        }
                    }
                }
            }
        }
        if (!found) {
            cells.append(cells.slot(cell), cell, static_cast<unsigned int>(n), &next_in_cell);
        }
    }
    // The kept vertices in their order, and the new index of each vertex.
    std::vector<unsigned int> new_index(count);
    std::vector<Vec3> vertices;
    vertices.reserve(count - merged);
    for (std::size_t n = 0; n < count; n++) {
        if (merged_into[n] == n) {
            new_index[n] = static_cast<unsigned int>(vertices.size());
            vertices.push_back(mesh->vertices[n]);
        }
        else {
            new_index[n] = new_index[merged_into[n]];
        }
    }
    if (merged == 0) {
        return 0;
    }

    std::vector<unsigned int> indices;
    indices.reserve(mesh->indices.size());
    for (std::size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
        const unsigned int a = new_index[mesh->indices[t]];
        const unsigned int b = new_index[mesh->indices[t + 1]];
        const unsigned int c = new_index[mesh->indices[t + 2]];
        if (a == b || b == c || c == a) {
            continue;
        }
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->normals.clear();
    if (had_normals) {
        mesh->normals.assign(mesh->vertices.size(), Vec3(0.0));
        for (std::size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
            const unsigned int a = mesh->indices[t], b = mesh->indices[t + 1], c = mesh->indices[t + 2];
            const Vec3 face = cross(mesh->vertices[b] - mesh->vertices[a], mesh->vertices[c] - mesh->vertices[a]);
            mesh->normals[a] += face;
            mesh->normals[b] += face;
            mesh->normals[c] += face;
        }
        for (Vec3 &normal : mesh->normals) {
            normal = normal.length_squared() > 1.0e-20 ? normalize(normal) : Vec3(0.0, 0.0, 1.0);
        }
    }
    return merged;
}
