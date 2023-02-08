#include <chrono>

#include "envoy/http/header_map.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/tracers/datadog/agent_http_client.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "datadog/dict_writer.h"
#include "datadog/error.h"
#include "datadog/expected.h"
#include "datadog/json.hpp"
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

TEST(DatadogAgentHttpClientTest, OnResponse200) {
  // When `onSuccess` is invoked on the `Http::AsyncClient::Callbacks`, the
  // associated `on_response` callback is invoked with corresponding arguments.
  // Additionally, if the HTTP response status is 200, `stats_.reports_sent_` is
  // incremented.

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
  Http::AsyncClient::Callbacks* callbacks;
  testing::MockFunction<void(int status, const datadog::tracing::DictReader& headers,
                             std::string body)>
      on_response;
  testing::MockFunction<void(datadog::tracing::Error)> on_error;

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));

  // `callbacks->onSuccess(...)` will cause `on_response` to be called.
  // `on_error` will not be called.
  EXPECT_CALL(on_response, Call(200, _, "{}"));
  EXPECT_CALL(on_error, Call(_)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been successfully fulfilled.
  EXPECT_CALL(request, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client.post(url, ignore, "{}", on_response.AsStdFunction(), on_error.AsStdFunction());
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  callbacks->onSuccess(request, std::move(msg));
  EXPECT_EQ(1, stats.reports_sent_.value());
  EXPECT_EQ(0, stats.reports_failed_.value());
  EXPECT_EQ(0, stats.reports_skipped_no_cluster_.value());
}

TEST(DatadogAgentHttpClientTest, OnResponseNot200) {
  // When `onSuccess` is invoked on the `Http::AsyncClient::Callbacks`, the
  // associated `on_response` callback is invoked with corresponding arguments.
  // Additionally, if the HTTP response status is not 200,
  // `stats_.reports_dropped_` is incremented.

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
  Http::AsyncClient::Callbacks* callbacks;
  testing::MockFunction<void(int status, const datadog::tracing::DictReader& headers,
                             std::string body)>
      on_response;
  testing::MockFunction<void(datadog::tracing::Error)> on_error;

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));

  // `callbacks->onSuccess(...)` will cause `on_response` to be called.
  // The `404` value corresponds to the response sent below.
  // `on_error` will not be called.
  EXPECT_CALL(on_response, Call(404, _, "{}"));
  EXPECT_CALL(on_error, Call(_)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been successfully fulfilled.
  EXPECT_CALL(request, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client.post(url, ignore, "{}", on_response.AsStdFunction(), on_error.AsStdFunction());
  EXPECT_TRUE(result) << result.error();

  // The "404" below is what causes `stats.reports_failed_` to be incremented
  // instead of `stats.reports_sent_`.
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));
  msg->body().add("{}");

  callbacks->onSuccess(request, std::move(msg));
  EXPECT_EQ(1, stats.reports_dropped_.value());
  EXPECT_EQ(0, stats.reports_sent_.value());
  EXPECT_EQ(0, stats.reports_failed_.value());
  EXPECT_EQ(0, stats.reports_skipped_no_cluster_.value());
}

TEST(DatadogAgentHttpClientTest, OnResponseBogusRequest) {
  // When `onSuccess` is invoked on the `Http::AsyncClient::Callbacks` with a
  // request that is not registered with the HTTP client, then no callback is
  // invoked (how would we look it up?).

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
  Http::AsyncClient::Callbacks* callbacks;
  testing::MockFunction<void(int status, const datadog::tracing::DictReader& headers,
                             std::string body)>
      on_response;
  testing::MockFunction<void(datadog::tracing::Error)> on_error;

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));

  // `callbacks->onSuccess(...)` will not invoke any callbacks, because the
  // request argument passed in is not registered with the HTTP client.
  EXPECT_CALL(on_response, Call(_, _, _)).Times(0);
  EXPECT_CALL(on_error, Call(_)).Times(0);

  // The request will will canceled by `~AgentHTTPClient` because `onSuccess`
  // was passed the wrong request, and so the real request is never removed from
  // the HTTP client's registry.
  EXPECT_CALL(request, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client.post(url, ignore, "{}", on_response.AsStdFunction(), on_error.AsStdFunction());
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  // The first argument to `onSuccess` should be `request`, but instead we pass
  // `bogus_request`.
  Http::MockAsyncClientRequest bogus_request(&cluster_manager.thread_local_cluster_.async_client_);
  callbacks->onSuccess(bogus_request, std::move(msg));
}

TEST(DatadogAgentHttpClientTest, OnErrorStreamReset) {
  // When `onFailure` is invoked on the `Http::AsyncClient::Callbacks` with
  // `FailureReason::Reset`, the associated `on_error` callback is invoked with
  // a corresponding `datadog::tracing::Error`.

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
  Http::AsyncClient::Callbacks* callbacks;
  testing::MockFunction<void(int status, const datadog::tracing::DictReader& headers,
                             std::string body)>
      on_response;
  testing::MockFunction<void(datadog::tracing::Error)> on_error;

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));

  // `callbacks->onFailure(...)` will cause `on_error` to be called.
  // `on_response` will not be called.
  EXPECT_CALL(on_error, Call(_)).WillOnce(Invoke([](datadog::tracing::Error error) {
    EXPECT_EQ(error.code, datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE);
  }));
  EXPECT_CALL(on_response, Call(_, _, _)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been successfully fulfilled.
  EXPECT_CALL(request, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client.post(url, ignore, "{}", on_response.AsStdFunction(), on_error.AsStdFunction());
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  callbacks->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

TEST(DatadogAgentHttpClientTest, OnErrorOther) {
  // When `onFailure` is invoked on the `Http::AsyncClient::Callbacks` with any
  // value other than `FailureReason::Reset`, the associated `on_error` callback
  // is invoked with a corresponding `datadog::tracing::Error`.

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
  Http::AsyncClient::Callbacks* callbacks;
  testing::MockFunction<void(int status, const datadog::tracing::DictReader& headers,
                             std::string body)>
      on_response;
  testing::MockFunction<void(datadog::tracing::Error)> on_error;

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));

  // `callbacks->onFailure(...)` will cause `on_error` to be called.
  // `on_response` will not be called.
  EXPECT_CALL(on_error, Call(_)).WillOnce(Invoke([](datadog::tracing::Error error) {
    EXPECT_EQ(error.code, datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE);
  }));
  EXPECT_CALL(on_response, Call(_, _, _)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been successfully fulfilled.
  EXPECT_CALL(request, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client.post(url, ignore, "{}", on_response.AsStdFunction(), on_error.AsStdFunction());
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  const auto bogus_value = static_cast<Http::AsyncClient::FailureReason>(-1);
  callbacks->onFailure(request, bogus_value);
}

TEST(DatadogAgentHttpClientTest, SendFailReturnsError) {
  // If the underlying call to `httpAsyncClient().send(...)` returns an error,
  // then the enclosing call to `AgentHTTPClient::post(...)` returns an error.

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
  Http::AsyncClient::Callbacks* callbacks;

  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return nullptr; // indicates error
          }));

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client.post(url, ignore, "", ignore, ignore);
  ASSERT_FALSE(result);
  EXPECT_EQ(datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE, result.error().code);
  EXPECT_EQ(1, stats.reports_failed_.value());
  EXPECT_EQ(0, stats.reports_skipped_no_cluster_.value());
}

TEST(DatadogAgentHttpClientTest, DrainIsANoOp) {
  // `AgentHTTPClient::drain` doesn't do anything. It only makes sense in
  // multi-threaded contexts.
  // This test is for the sake of coverage.

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  cluster_manager.initializeClusters({"fake_cluster"}, {});
  cluster_manager.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
  cluster_manager.initializeThreadLocalClusters({"fake_cluster"});
  Stats::TestUtil::TestStore store;
  TracerStats stats = makeTracerStats(*store.rootScope());
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats);

  // `deadline` value doesn't matter; `drain` ignores it.
  const auto deadline = std::chrono::steady_clock::time_point::min();
  client.drain(deadline);
}

TEST(DatadogAgentHttpClientTest, ConfigJSONContainsTypeName) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  cluster_manager.initializeClusters({"fake_cluster"}, {});
  cluster_manager.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
  cluster_manager.initializeThreadLocalClusters({"fake_cluster"});
  Stats::TestUtil::TestStore store;
  TracerStats stats = makeTracerStats(*store.rootScope());
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats);

  nlohmann::json config = client.config_json();
  EXPECT_EQ("Envoy::Extensions::Tracers::Datadog::AgentHTTPClient", config["type"]);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
