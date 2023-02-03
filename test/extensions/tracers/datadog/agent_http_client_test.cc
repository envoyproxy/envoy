#include <chrono>

#include "envoy/http/header_map.h"

#include "source/extensions/tracers/datadog/agent_http_client.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "datadog/dict_writer.h"
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

TEST(DatadogAgentHttpClientTest, MissingThreadLocalCluster) {
  // If ...`threadLocalCluster().has_value()` is false, then `post` cannot
  // create a request and so will immediately return successfully but increment
  // the "reports skipped no cluster" counter.

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  Stats::TestUtil::TestStore store;
  TracerStats stats = makeTracerStats(*store.rootScope());
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats);
  datadog::tracing::HTTPClient::URL url;
  url.scheme = "http";
  url.authority = "localhost:8126";
  url.path = "/foo/bar";

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client.post(url, ignore, "", ignore, ignore);
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(1, stats.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats.reports_failed_.value());
}

TEST(DatadogAgentHttpClientTest, RequestHeaders) {
  // The `set_headers` argument to `post(...)` results in the corresponding
  // headers being set in `Http::RequestMessage::headers()`.
  // Additionally, the "Host" header will always be the same as the
  // corresponding parameter of `AgentHTTPClient`'s constructor.

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
  const auto set_headers = [&](datadog::tracing::DictWriter& headers) {
    headers.set("foo", "bar");
    headers.set("baz-boing", "boing boing");
    headers.set("boing-boing", "boing boing");
    headers.set("boing-boing", "boing boing boing");
  };

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ("test_host", message->headers().getHostValue());

            EXPECT_EQ("bar", message->headers().getByKey("foo"));
            EXPECT_EQ("boing boing", message->headers().getByKey("baz-boing"));
            EXPECT_EQ("boing boing boing", message->headers().getByKey("boing-boing"));

            return &request;
          }));

  // `~AgentHTTPClient()` will cancel the request since we don't finish it here.
  EXPECT_CALL(request, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client.post(url, set_headers, "", ignore, ignore);
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(0, stats.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats.reports_failed_.value());
}

TEST(DatadogAgentHttpClientTest, RequestBody) {
  // The `body` parameter to `AgentHTTPClient::post` corresponds to the
  // resulting `Http::RequestMessage::body()`.

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
  const std::string body = R"latin(
    Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod
    tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam,
    quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo
    consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse
    cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat
    non proident, sunt in culpa qui officia deserunt mollit anim id est
    laborum.)latin";

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ(body, message->body().toString());
            return &request;
          }));

  // `~AgentHTTPClient()` will cancel the request since we don't finish it here.
  EXPECT_CALL(request, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client.post(url, ignore, body, ignore, ignore);
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(0, stats.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats.reports_failed_.value());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
