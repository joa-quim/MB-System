/*--------------------------------------------------------------------
 *    The MB-system:  mb_cube_test.cc  10/3/2026
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Tests for src/mbaux/mb_cube.c, the C port of bathycube's cube.py.  The node tests are
 * bathycube's tests/test_cube.py, with its expected values, driven through the C API: a bathycube
 * CubeNode() is node (0, 0) of a 1x1 grid whose parameters are CubeNode's defaults (var_scale 0).
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mbaux/mb_cube.h"

namespace {

// A bathycube CubeNode(): node (0, 0) of a 1x1 grid with CubeNode's default settings.
struct Node {
  mb_cube_grid *g = nullptr;
  mb_cube_node *n = nullptr;
  explicit Node(bool use_queue = true) {
    mb_cube_params p;
    mb_cube_params_default(&p);
    mb_cube_params_initialize(&p, MB_CUBE_IHO_ORDER1A, 1.0, 1.0);
    p.var_scale = 0.0;  // CubeNode(var_scale=0.0)
    g = mb_cube_grid_new(0.0, 1.0, 1, 1, 1.0, 1.0, &p, use_queue, nullptr, false);
    n = mb_cube_grid_node(g, 0, 0);
  }
  ~Node() { mb_cube_grid_free(&g); }
  void add(double depth, double vert, double horiz, double dist2) {
    ASSERT_EQ(MB_CUBE_OK, mb_cube_node_add_point(g, n, depth, vert, horiz, dist2));
  }
  // add_to_queue(depth, variance): with var_scale 0, no horizontal uncertainty and zero distance,
  // add_point_to_node passes the point to the queue unchanged.
  void queue(double depth, double variance) { add(depth, variance, 0.0, 0.0); }
  double hyp_depth(int i) {
    double d = NAN;
    mb_cube_node_hypothesis(n, i, &d, nullptr, nullptr, nullptr);
    return d;
  }
  double hyp_variance(int i) {
    double v = NAN;
    mb_cube_node_hypothesis(n, i, nullptr, &v, nullptr, nullptr);
    return v;
  }
  int hyp_points(int i) {
    int k = -1;
    mb_cube_node_hypothesis(n, i, nullptr, nullptr, &k, nullptr);
    return k;
  }
};

const double kEleven[] = {5.0, 17.7, 5.2, 5.5, 17.8, 4.4, 4.0, 16.7, 4.2, 4.5, 16.8};

}  // namespace

TEST(MbCubeTest, Params) {
  mb_cube_params p;
  mb_cube_params_default(&p);
  ASSERT_EQ(MB_CUBE_OK, mb_cube_params_initialize(&p, MB_CUBE_IHO_ORDER1A, 0.5, 0.5));
  EXPECT_EQ(0.5, p.grid_resolution_x);
  EXPECT_EQ(0.5, p.grid_resolution_y);
  EXPECT_EQ(1 / 2.0, p.inv_dist_exponent);
  EXPECT_EQ(MB_CUBE_IHO_ORDER1A, p.iho_order);
  EXPECT_DOUBLE_EQ(0.25, p.iho_fixed);             // squared, as the C code
  EXPECT_DOUBLE_EQ(0.013 * 0.013, p.iho_percent);
  EXPECT_EQ(10, p.min_context_nodes);               // 5 m / 0.5 m, floor of one node
  EXPECT_EQ(20, p.max_context_nodes);
  EXPECT_DOUBLE_EQ(4.0, p.var_scale);               // 0.5^-2
  mb_cube_iho_t o;
  EXPECT_EQ(MB_CUBE_OK, mb_cube_iho_from_name("Special", &o));
  EXPECT_EQ(MB_CUBE_IHO_SPECIAL, o);
  EXPECT_NE(MB_CUBE_OK, mb_cube_iho_from_name("order3", &o));
}

TEST(MbCubeTest, ParamsOverrideBeforeInitialize) {
  mb_cube_params p;
  mb_cube_params_default(&p);
  p.dist_exponent = 3.0;
  ASSERT_EQ(MB_CUBE_OK, mb_cube_params_initialize(&p, MB_CUBE_IHO_ORDER2, 2.0, 4.0));
  EXPECT_DOUBLE_EQ(1.0 / 3.0, p.inv_dist_exponent);
  EXPECT_DOUBLE_EQ(std::pow(2.0, -3.0), p.var_scale);
  EXPECT_DOUBLE_EQ(2.0, p.dist_scale);
}

TEST(MbCubeTest, ParameterFileRoundTrip) {
  mb_cube_params p, q;
  mb_cube_params_default(&p);
  p.median_length = 7;
  p.capture_dist_scale = 0.1;
  p.variance_selection = MB_CUBE_VARIANCE_MAX;
  mb_cube_params_initialize(&p, MB_CUBE_IHO_SPECIAL, 0.25, 0.5);
  const std::string f = testing::TempDir() + "mb_cube_params_test.json";
  ASSERT_EQ(MB_CUBE_OK, mb_cube_params_write(&p, f.c_str()));
  mb_cube_params_default(&q);
  bool valid = false;
  ASSERT_EQ(MB_CUBE_OK, mb_cube_params_read(&q, f.c_str(), &valid));
  EXPECT_TRUE(valid);
  EXPECT_EQ(7, q.median_length);
  EXPECT_DOUBLE_EQ(0.1, q.capture_dist_scale);
  EXPECT_EQ(MB_CUBE_VARIANCE_MAX, q.variance_selection);
  EXPECT_EQ(MB_CUBE_IHO_SPECIAL, q.iho_order);
  EXPECT_DOUBLE_EQ(0.25, q.grid_resolution_x);
  EXPECT_TRUE(std::isnan(q.no_data_value));
  std::remove(f.c_str());
}

TEST(MbCubeTest, NodeInit) {
  Node cb;
  mb_cube_node_set_predicted_depth(cb.n, 1.0);
  EXPECT_EQ(1.0, mb_cube_node_predicted_depth(cb.n));
  EXPECT_EQ(0.0, mb_cube_node_predicted_variance(cb.n));
}

TEST(MbCubeTest, NodeNewHypothesis) {
  Node cb;
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 5.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 6.0, 6.0, false);
  EXPECT_EQ(5.0, cb.hyp_depth(0));
  EXPECT_EQ(6.0, cb.hyp_depth(1));
}

TEST(MbCubeTest, NodeRemoveHypothesis) {
  Node cb;
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 5.0, false);
  mb_cube_node_remove_hypothesis(cb.g, cb.n, 99.0);
  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));

  mb_cube_node_remove_hypothesis(cb.g, cb.n, 5.001);
  EXPECT_EQ(0, mb_cube_node_number_of_hypotheses(cb.n));

  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 5.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 6.0, 6.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 7.0, 7.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 8.0, 8.0, false);
  mb_cube_node_remove_hypothesis(cb.g, cb.n, 6.001);
  mb_cube_node_remove_hypothesis(cb.g, cb.n, 7.999);
  mb_cube_node_remove_hypothesis(cb.g, cb.n, 5.001);
  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
  EXPECT_EQ(7.0, cb.hyp_depth(0));
}

TEST(MbCubeTest, NodeRemoveKeepsNominationIndex) {
  Node cb;
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 1.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 6.0, 1.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 7.0, 1.0, false);
  mb_cube_node_nominate_hypothesis(cb.g, cb.n, 7.0);
  mb_cube_node_remove_hypothesis(cb.g, cb.n, 5.0);
  mb_cube_answer a;
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_EQ(7.0, a.depth);
}

TEST(MbCubeTest, NodeNominateHypothesis) {
  Node cb;
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 5.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 6.0, 6.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 7.0, 7.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 8.0, 8.0, false);

  EXPECT_FALSE(mb_cube_node_has_nomination(cb.n));
  mb_cube_answer a;
  mb_cube_node_nominate_hypothesis(cb.g, cb.n, 5.001);
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_EQ(5.0, a.depth);

  mb_cube_node_nominate_hypothesis(cb.g, cb.n, 4.999);
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_EQ(5.0, a.depth);

  mb_cube_node_nominate_hypothesis(cb.g, cb.n, 7.001);
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_EQ(7.0, a.depth);

  EXPECT_EQ(MB_CUBE_ERR_NOT_FOUND, mb_cube_node_nominate_hypothesis(cb.g, cb.n, 7.5));
  EXPECT_FALSE(mb_cube_node_has_nomination(cb.n));
}

TEST(MbCubeTest, NodeNominateExactMatchWins) {
  Node cb;
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 1.0, false);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.004, 1.0, false);
  mb_cube_node_nominate_hypothesis(cb.g, cb.n, 5.0);  // exact match first, a worse one second
  mb_cube_answer a;
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_EQ(5.0, a.depth);
}

TEST(MbCubeTest, NodeResetNomination) {
  Node cb;
  mb_cube_node_clear_nomination(cb.g, cb.n);
  EXPECT_FALSE(mb_cube_node_has_nomination(cb.n));

  mb_cube_node_add_hypothesis(cb.g, cb.n, 8.0, 8.0, false);
  mb_cube_node_nominate_hypothesis(cb.g, cb.n, 8.001);
  EXPECT_TRUE(mb_cube_node_has_nomination(cb.n));
  mb_cube_node_clear_nomination(cb.g, cb.n);
  EXPECT_FALSE(mb_cube_node_has_nomination(cb.n));
}

TEST(MbCubeTest, NodeSetPreddepth) {
  Node cb;
  EXPECT_EQ(0.0, mb_cube_node_predicted_depth(cb.n));
  EXPECT_EQ(0.0, mb_cube_node_predicted_variance(cb.n));
  mb_cube_node_set_predicted_depth(cb.n, 5.0);
  mb_cube_node_set_predicted_variance(cb.n, 1.5);
  EXPECT_EQ(5.0, mb_cube_node_predicted_depth(cb.n));
  EXPECT_EQ(1.5, mb_cube_node_predicted_variance(cb.n));
}

TEST(MbCubeTest, NodeUpdateNode) {
  Node cb(false);  // no queue: every accepted point goes straight to update_node
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 1.0, false);
  cb.queue(5.1, 1.0);
  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
  EXPECT_NEAR(5.05, cb.hyp_depth(0), 0.01);
  EXPECT_EQ(2, cb.hyp_points(0));

  cb.queue(15.1, 1.0);
  EXPECT_EQ(2, mb_cube_node_number_of_hypotheses(cb.n));
}

TEST(MbCubeTest, NodeTruncateQueued) {
  Node cb;
  cb.queue(6.0, 1.0);
  cb.queue(6.1, 1.0);
  cb.queue(6.2, 1.0);
  cb.queue(6.3, 1.0);
  cb.queue(6.4, 1.0);
  cb.queue(16.5, 2.0);
  cb.queue(36.6, 2.0);
  EXPECT_EQ(7, mb_cube_node_number_queued(cb.n));
  mb_cube_node_flush_queue(cb.g, cb.n);  // truncates (36.6 is the outlier) and then flushes
  int total = 0;
  for (int i = 0; i < mb_cube_node_number_of_hypotheses(cb.n); i++)
    total += cb.hyp_points(i);
  EXPECT_EQ(6, total);
}

TEST(MbCubeTest, NodeQueueFlushNode) {
  Node cb;
  cb.queue(5.0, 1.0);
  cb.queue(5.1, 1.0);
  cb.queue(5.2, 1.0);
  cb.queue(5.3, 1.0);
  EXPECT_EQ(4, mb_cube_node_number_queued(cb.n));
  mb_cube_node_flush_queue(cb.g, cb.n);

  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
  EXPECT_EQ(0, mb_cube_node_number_queued(cb.n));
  EXPECT_NEAR(5.150, cb.hyp_depth(0), 0.001);
  EXPECT_NEAR(0.25, cb.hyp_variance(0), 0.001);
  EXPECT_EQ(4, cb.hyp_points(0));
}

TEST(MbCubeTest, NodeChooseHypothesis) {
  Node cb;
  cb.queue(5.0, 1.0);
  cb.queue(5.1, 1.0);
  cb.queue(5.2, 1.0);
  cb.queue(17.7, 1.0);
  cb.queue(17.8, 1.0);
  mb_cube_node_flush_queue(cb.g, cb.n);

  mb_cube_answer a;
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_EQ(3, a.n_points);
  EXPECT_EQ(3.5, a.ratio);
}

TEST(MbCubeTest, NodeAddToQueue) {
  Node cb;
  for (double d : kEleven)
    cb.queue(d, 1.0);
  EXPECT_EQ(0, mb_cube_node_number_of_hypotheses(cb.n));
  // this should trigger update node, as you hit median length limit
  cb.queue(4.6, 1.0);
  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
}

TEST(MbCubeTest, NodeQueueInsert) {
  Node cb;
  for (double d : kEleven)
    cb.queue(d, 1.0);
  // sorted queue 4.0 4.2 4.4 4.5 5.0 5.2 5.5 16.7 16.8 17.7 17.8: the median 5.2 starts the estimate
  cb.queue(10.0, 1.0);
  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
  EXPECT_NEAR(5.2, cb.hyp_depth(0), 1e-12);
  // then 5.5 updates it
  cb.queue(10.0, 1.0);
  EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
  EXPECT_NEAR(5.35, cb.hyp_depth(0), 1e-12);
  // this outlier will trigger truncation; the median 10.0 is an intervention -> second hypothesis
  cb.queue(100.0, 1.0);
  EXPECT_EQ(10, mb_cube_node_number_queued(cb.n));
  EXPECT_EQ(2, mb_cube_node_number_of_hypotheses(cb.n));
  EXPECT_NEAR(10.0, cb.hyp_depth(1), 1e-12);
}

TEST(MbCubeTest, NodeAddPoint) {
  {
    Node cb;  // predicted depth flagged
    mb_cube_node_set_predicted_depth(cb.n, NAN);
    cb.add(5.0, 0.0, 0.0, 1.0);
    EXPECT_EQ(0, mb_cube_node_number_queued(cb.n));
  }
  {
    Node cb;  // blunder
    mb_cube_node_set_predicted_depth(cb.n, 100.0);
    cb.add(50.0, 0.0, 0.0, 1.0);
    EXPECT_EQ(0, mb_cube_node_number_queued(cb.n));
  }
  {
    Node cb;  // too far
    cb.add(5.0, 0.0, 0.0, 1.0);
    EXPECT_EQ(0, mb_cube_node_number_queued(cb.n));
  }
  Node cb;
  cb.add(5.0, 0.5, 0.5, 0.25);
  EXPECT_EQ(1, mb_cube_node_number_queued(cb.n));
  for (int i = 1; i < 11; i++)
    cb.add(kEleven[i], 0.5, 0.5, 0.25);
  EXPECT_EQ(11, mb_cube_node_number_queued(cb.n));
  EXPECT_EQ(0, mb_cube_node_number_of_hypotheses(cb.n));
}

TEST(MbCubeTest, NodeExtractDepthUncertainty) {
  mb_cube_answer a;
  {
    Node cb;
    mb_cube_node_extract_value(cb.g, cb.n, &a);
    EXPECT_TRUE(std::isnan(a.depth));
    EXPECT_TRUE(std::isnan(a.uncertainty));
    EXPECT_TRUE(std::isnan(a.ratio));
  }
  {
    Node cb;
    cb.add(5.0, 0.5, 0.5, 0.25);
    mb_cube_node_flush_queue(cb.g, cb.n);
    mb_cube_node_extract_value(cb.g, cb.n, &a);
    EXPECT_NEAR(5.0, a.depth, 0.001);
    EXPECT_NEAR(1.385, a.uncertainty, 0.001);
    EXPECT_NEAR(0.0, a.ratio, 0.001);
  }
  {
    Node cb;
    cb.add(5.0, 0.5, 0.5, 0.25);
    mb_cube_node_flush_queue(cb.g, cb.n);
    mb_cube_node_nominate_hypothesis(cb.g, cb.n, 5.0);
    mb_cube_node_extract_value(cb.g, cb.n, &a);
    EXPECT_NEAR(5.0, a.depth, 0.001);
    EXPECT_NEAR(1.385, a.uncertainty, 0.001);
    EXPECT_NEAR(0.0, a.ratio, 0.001);
  }
  {
    Node cb;
    for (double d : kEleven)
      cb.add(d, 0.5, 0.5, 0.25);
    mb_cube_node_flush_queue(cb.g, cb.n);
    mb_cube_node_extract_value(cb.g, cb.n, &a);
    EXPECT_NEAR(4.686, a.depth, 0.001);
    EXPECT_NEAR(0.524, a.uncertainty, 0.001);
    EXPECT_NEAR(3.25, a.ratio, 0.001);
  }
}

TEST(MbCubeTest, NodeExtractClosestAndPosterior) {
  for (int posterior = 0; posterior < 2; posterior++) {
    mb_cube_answer a;
    auto extract = [&](Node &cb) {
      if (posterior)
        mb_cube_node_extract_posterior_weighted_value(cb.g, cb.n, 15.0, 0.5, &a);
      else
        mb_cube_node_extract_closest_value(cb.g, cb.n, 15.0, 0.5, &a);
    };
    {
      Node cb;
      extract(cb);
      EXPECT_TRUE(std::isnan(a.depth));
      EXPECT_TRUE(std::isnan(a.uncertainty));
      EXPECT_TRUE(std::isnan(a.ratio));
    }
    {
      Node cb;
      cb.add(5.0, 0.5, 0.5, 0.25);
      mb_cube_node_flush_queue(cb.g, cb.n);
      extract(cb);
      EXPECT_NEAR(5.0, a.depth, 0.001);
      EXPECT_NEAR(1.385, a.uncertainty, 0.001);
      EXPECT_NEAR(0.0, a.ratio, 0.001);
    }
    {
      Node cb;
      cb.add(5.0, 0.5, 0.5, 0.25);
      mb_cube_node_flush_queue(cb.g, cb.n);
      mb_cube_node_nominate_hypothesis(cb.g, cb.n, 5.0);
      extract(cb);
      EXPECT_NEAR(5.0, a.depth, 0.001);
      EXPECT_NEAR(1.385, a.uncertainty, 0.001);
      EXPECT_NEAR(0.0, a.ratio, 0.001);
    }
    {
      Node cb;
      for (double d : kEleven)
        cb.add(d, 0.5, 0.5, 0.25);
      mb_cube_node_flush_queue(cb.g, cb.n);
      extract(cb);
      EXPECT_NEAR(17.25, a.depth, 0.001);
      EXPECT_NEAR(0.693, a.uncertainty, 0.001);
      EXPECT_NEAR(4.429, a.ratio, 0.001);
    }
  }
}

TEST(MbCubeTest, NodeHypothesisCount) {
  {
    Node cb;
    EXPECT_EQ(0, mb_cube_node_number_of_hypotheses(cb.n));
  }
  {
    Node cb;
    cb.add(5.0, 0.5, 0.5, 0.25);
    mb_cube_node_flush_queue(cb.g, cb.n);
    EXPECT_EQ(1, mb_cube_node_number_of_hypotheses(cb.n));
  }
  {
    Node cb;
    for (int i = 0; i < 5; i++)
      cb.add(kEleven[i], 0.5, 0.5, 0.25);
    mb_cube_node_flush_queue(cb.g, cb.n);
    EXPECT_EQ(2, mb_cube_node_number_of_hypotheses(cb.n));
  }
}

TEST(MbCubeTest, OnlyNullHypothesesIsNoData) {
  Node cb;
  mb_cube_node_add_hypothesis(cb.g, cb.n, 5.0, 1.0, true);
  mb_cube_node_add_hypothesis(cb.g, cb.n, 6.0, 1.0, true);
  mb_cube_answer a;
  mb_cube_node_extract_value(cb.g, cb.n, &a);
  EXPECT_TRUE(std::isnan(a.depth));
}

// bathycube README quickstart: 2500 soundings over a 3 m square into a 3x3 grid of 1 m nodes.
TEST(MbCubeTest, RunCubeGriddingQuickstart) {
  std::vector<double> x, y, z, tvu, thu;
  for (int j = 0; j < 50; j++)
    for (int i = 0; i < 50; i++) {
      x.push_back(403744.0 + 3.0 * i / 49.0);
      y.push_back(4122687.0 + 3.0 * j / 49.0);
    }
  for (int k = 0; k < 2500; k++) {
    z.push_back(10.0 + 10.0 * k / 2499.0);
    tvu.push_back(0.3 + 0.4 * k / 2499.0);
    thu.push_back(0.3 + 0.4 * k / 2499.0);
  }
  std::vector<float> depth(9), unc(9), ratio(9), numhyp(9);
  ASSERT_EQ(MB_CUBE_OK, mb_cube_run_gridding(2500, z.data(), thu.data(), tvu.data(), x.data(), y.data(), 3, 3,
                                             403744.0, 4122690.0, MB_CUBE_METHOD_LOCAL, MB_CUBE_IHO_ORDER1A, 1.0, 1.0,
                                             nullptr, depth.data(), unc.data(), ratio.data(), numhyp.data()));
  for (int k = 0; k < 9; k++) {
    EXPECT_FALSE(std::isnan(depth[k])) << "node " << k;
    EXPECT_GE(numhyp[k], 1.0f);
    EXPECT_GT(depth[k], 10.0f);
    EXPECT_LT(depth[k], 20.0f);
  }
}

// A sounding whose search square starts in the last column must reach it: with 2 m x 1 m cells
// the radius is one metre (dist_scale), half a column, so its square is that column alone.
TEST(MbCubeTest, GridEdgeAndLayouts) {
  mb_cube_params p;
  mb_cube_params_default(&p);
  mb_cube_params_initialize(&p, MB_CUBE_IHO_ORDER1A, 2.0, 1.0);
  mb_cube_grid *g = mb_cube_grid_new(0.0, 4.0, 5, 4, 2.0, 1.0, &p, true, nullptr, false);
  ASSERT_NE(nullptr, g);
  // node (row 3, col 4) is centred at (9.0, 0.5)
  const double z = 20.0, thu = 0.01, tvu = 0.04, e = 9.0, n = 0.5;
  ASSERT_EQ(MB_CUBE_OK, mb_cube_grid_insert(g, 1, &z, &thu, &tvu, &e, &n));
  mb_cube_grid_flush(g);
  EXPECT_EQ(1u, mb_cube_grid_populated_nodes_count(g));
  EXPECT_EQ(19u, mb_cube_grid_empty_nodes_count(g));
  EXPECT_EQ(20u, mb_cube_grid_total_nodes_count(g));

  std::vector<float> rows(20), cols(20), npts(20);
  ASSERT_EQ(MB_CUBE_OK,
            mb_cube_grid_get_values(g, MB_CUBE_METHOD_PRIOR, rows.data(), nullptr, nullptr, nullptr, nullptr,
                                    MB_CUBE_LAYOUT_ROWS_NORTH));
  ASSERT_EQ(MB_CUBE_OK,
            mb_cube_grid_get_values(g, MB_CUBE_METHOD_PRIOR, cols.data(), nullptr, nullptr, nullptr, npts.data(),
                                    MB_CUBE_LAYOUT_COLS_SOUTH));
  EXPECT_EQ(20.0f, rows[3 * 5 + 4]);   // row 3 (south), col 4
  EXPECT_EQ(20.0f, cols[4 * 4 + 0]);   // col 4, y index 0 (south)
  EXPECT_EQ(1.0f, npts[4 * 4 + 0]);
  for (int row = 0; row < 4; row++)
    for (int col = 0; col < 5; col++) {
      const float a = rows[row * 5 + col], b = cols[col * 4 + (3 - row)];
      EXPECT_TRUE((std::isnan(a) && std::isnan(b)) || a == b);
    }

  std::vector<float> h(20);
  ASSERT_EQ(MB_CUBE_OK, mb_cube_grid_get_values(g, MB_CUBE_METHOD_LOCAL, nullptr, nullptr, nullptr, h.data(),
                                                nullptr, MB_CUBE_LAYOUT_ROWS_NORTH));
  EXPECT_EQ(1.0f, h[3 * 5 + 4]);
  EXPECT_EQ(0.0f, h[0]);  // the count-only path reports 0, not no-data
  mb_cube_grid_free(&g);
  EXPECT_EQ(nullptr, g);
}
