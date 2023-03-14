#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/tracer.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "datadog/error.h"
#include "datadog/expected.h"
#include "datadog/tracer_config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class DatadogTracerTest : public testing::Test {
public:
  DatadogTracerTest() {
    cluster_manager_.initializeClusters({"fake_cluster"}, {});
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::TestUtil::TestStore store_;
  NiceMock<ThreadLocal::MockInstance> thread_local_slot_allocator_;
};

TEST_F(DatadogTracerTest, Breathing) {
  // Verify that constructing a `Tracer` instance with mocked dependencies
  // does not throw exceptions.
  datadog::tracing::TracerConfig config;
  config.defaults.service = "envoy";

  Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                thread_local_slot_allocator_);

  (void)tracer;
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
