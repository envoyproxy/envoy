#include "source/common/http/http1/request_tracker.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

class Http1RequestTrackerTest : public testing::Test {
public:
  Http1RequestTrackerTest() : api_(Api::createApiForTest(time_system_)) {
  }

  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  NiceMock<Upstream::MockClusterInfo> cluster_info_;
};

TEST_F(Http1RequestTrackerTest, BasicFunctionality) {
  Http1RequestTracker tracker(*cluster_info_.trafficStats());

  // Test connection lifecycle
  const uint64_t conn_id = 12345;
  
  // Establish connection
  tracker.onConnectionEstablished(conn_id);
  
  // Start some requests
  tracker.onRequestStarted(conn_id);
  tracker.onRequestStarted(conn_id);
  tracker.onRequestStarted(conn_id);
  
  // Close connection and verify gauge is set to 300 (3.0 * 100)
  tracker.onConnectionClosed(conn_id);
  EXPECT_EQ(300, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
}

TEST_F(Http1RequestTrackerTest, MultipleConnections) {
  Http1RequestTracker tracker(*cluster_info_.trafficStats());

  // Test multiple connections
  const uint64_t conn1 = 11111;
  const uint64_t conn2 = 22222;
  const uint64_t conn3 = 33333;
  
  // Connection 1: 2 requests
  tracker.onConnectionEstablished(conn1);
  tracker.onRequestStarted(conn1);
  tracker.onRequestStarted(conn1);
  
  // Connection 2: 5 requests
  tracker.onConnectionEstablished(conn2);
  tracker.onRequestStarted(conn2);
  tracker.onRequestStarted(conn2);
  tracker.onRequestStarted(conn2);
  tracker.onRequestStarted(conn2);
  tracker.onRequestStarted(conn2);
  
  // Connection 3: 1 request
  tracker.onConnectionEstablished(conn3);
  tracker.onRequestStarted(conn3);
  
  // Close connections and verify average calculation
  // Expected average after conn1: 2.0 -> 200
  tracker.onConnectionClosed(conn1);
  EXPECT_EQ(200, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
  
  // Expected average after conn2: (2 + 5) / 2 = 3.5 -> 350
  tracker.onConnectionClosed(conn2);
  EXPECT_EQ(350, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
  
  // Expected average after conn3: (2 + 5 + 1) / 3 = 2.67 -> 266
  tracker.onConnectionClosed(conn3);
  EXPECT_EQ(266, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
}

TEST_F(Http1RequestTrackerTest, EventDrivenMetricEmission) {
  Http1RequestTracker tracker(*cluster_info_.trafficStats());

  // Test that metrics are emitted immediately when connections close
  const uint64_t conn_id = 12345;
  tracker.onConnectionEstablished(conn_id);
  tracker.onRequestStarted(conn_id);
  tracker.onRequestStarted(conn_id);
  tracker.onRequestStarted(conn_id);
  
  // Initially gauge should be 0 (no completed connections yet)
  EXPECT_EQ(0, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
  
  // Close connection - metric should be emitted immediately
  tracker.onConnectionClosed(conn_id);
  EXPECT_EQ(300, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
}

TEST_F(Http1RequestTrackerTest, EmptyState) {
  Http1RequestTracker tracker(*cluster_info_.trafficStats());

  // Initial state - no change to gauge expected
  // The gauge should retain its initial value (0 by default)
  EXPECT_EQ(0, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
}

TEST_F(Http1RequestTrackerTest, UnknownConnectionRequests) {
  Http1RequestTracker tracker(*cluster_info_.trafficStats());

  // Try to start request on unknown connection - should not crash
  tracker.onRequestStarted(99999);
  
  // Try to close unknown connection - should not crash
  tracker.onConnectionClosed(88888);
  
  // Gauge should remain unchanged
  EXPECT_EQ(0, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
}

TEST_F(Http1RequestTrackerTest, HistoricalDataLimiting) {
  Http1RequestTracker tracker(*cluster_info_.trafficStats());

  // Create more connections than the historical limit to test truncation
  // Note: This test is mostly to ensure the code doesn't crash with large amounts of data
  for (uint64_t i = 0; i < 1100; ++i) {
    tracker.onConnectionEstablished(i);
    tracker.onRequestStarted(i);
    tracker.onConnectionClosed(i);
  }
  
  // Final value should be 100 (1.0 * 100) since each connection had 1 request
  EXPECT_EQ(100, cluster_info_.trafficStats()->upstream_rq_per_cx_http1_.value());
}

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
