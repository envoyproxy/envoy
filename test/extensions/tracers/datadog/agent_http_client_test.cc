#include <chrono>
#include <utility>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/extensions/tracers/datadog/agent_http_client.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "datadog/expected.h"
#include "datadog/optional.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

TEST(DatadogAgentHttpClientTest, PathFromURL) {
  // The `.path` portion of the `URL` argument to `AgentHTTPClient::post` ends
  // up as the "reference path" of the `Http::RequestHeaderMap`.
  // That is, the URL "http://foobar.com/trace/v04" results in "/trace/v04".

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  cluster_manager.initializeClusters({"fake_cluster"}, {});
  cluster_manager.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
  cluster_manager.initializeThreadLocalClusters({"fake_cluster"});
  Http::MockAsyncClientRequest request(&cluster_manager.thread_local_cluster_.async_client_);
  Stats::TestUtil::TestStore store;
  TracerStats stats = makeTracerStats(*store.rootScope());
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats);
  datadog::tracing::HTTPClient::URL url;
  url.scheme = "http";
  url.authority = "localhost:8126";
  url.path = "/foo/bar";

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ(url.path, message->headers().path());
            return &request;
          }));

  // `~AgentHTTPClient()` will cancel the request since we don't finish it here.
  EXPECT_CALL(request, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client.post(url, ignore, "", ignore, ignore);
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(0, stats.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats.reports_failed_.value());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
