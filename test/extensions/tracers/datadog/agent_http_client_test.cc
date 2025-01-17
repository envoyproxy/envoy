#include <chrono>

#include "envoy/http/header_map.h"

#include "source/common/http/message_impl.h"
#include "source/common/tracing/null_span_impl.h"
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

using testing::DoAll;
using testing::Return;
using testing::WithArg;

struct InitializedMockClusterManager {
  InitializedMockClusterManager() {
    EXPECT_CALL(instance_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks_), Return(nullptr)));

    instance_.initializeClusters({"fake_cluster"}, {});
    instance_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    instance_.initializeThreadLocalClusters({"fake_cluster"});
  }

  NiceMock<Upstream::MockClusterManager> instance_;
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks_;
};

class DatadogAgentHttpClientTest : public testing::Test {
public:
  DatadogAgentHttpClientTest()
      : request_(&cluster_manager_.instance_.thread_local_cluster_.async_client_),
        stats_(makeTracerStats(*store_.rootScope())),
        client_(cluster_manager_.instance_, "fake_cluster", "test_host", stats_, time_) {
    url_.scheme = "http";
    url_.authority = "localhost:8126";
    url_.path = "/foo/bar";
  }

protected:
  InitializedMockClusterManager cluster_manager_;
  Http::MockAsyncClientRequest request_;
  Stats::TestUtil::TestStore store_;
  TracerStats stats_;
  AgentHTTPClient client_;
  datadog::tracing::HTTPClient::URL url_;
  Http::AsyncClient::Callbacks* callbacks_;
  testing::MockFunction<void(int status, const datadog::tracing::DictReader& headers,
                             std::string body)>
      on_response_;
  testing::MockFunction<void(datadog::tracing::Error)> on_error_;
  Event::SimulatedTimeSystem time_;
};

TEST_F(DatadogAgentHttpClientTest, PathFromURL) {
  // The `.path` portion of the `URL` argument to `AgentHTTPClient::post` ends
  // up as the "reference path" of the `Http::RequestHeaderMap`.
  // That is, the URL "http://foobar.com/trace/v04" results in "/trace/v04".

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ(url_.path, message->headers().getPathValue());
            return &request_;
          }));

  // `~AgentHTTPClient()` will cancel the request since we don't finish it here.
  EXPECT_CALL(request_, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client_.post(
      url_, ignore, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
}

TEST_F(DatadogAgentHttpClientTest, MissingThreadLocalCluster) {
  // If ...`threadLocalCluster().has_value()` is false, then `post` cannot
  // create a request and so will immediately return successfully but increment
  // the "reports skipped no cluster" counter.

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats_, time_);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client.post(
      url_, ignore, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(1, stats_.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
}

TEST_F(DatadogAgentHttpClientTest, RequestHeaders) {
  // The `set_headers` argument to `post(...)` results in the corresponding
  // headers being set in `Http::RequestMessage::headers()`.
  // Additionally, the "Host" header will always be the same as the
  // corresponding parameter of `AgentHTTPClient`'s constructor.

  const auto set_headers = [&](datadog::tracing::DictWriter& headers) {
    headers.set("foo", "bar");
    headers.set("baz-boing", "boing boing");
    headers.set("boing-boing", "boing boing");
    headers.set("boing-boing", "boing boing boing");
  };

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([this](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                              const Http::AsyncClient::RequestOptions&)
                           -> Http::AsyncClient::Request* {
        EXPECT_EQ("test_host", message->headers().getHostValue());

        EXPECT_EQ("bar",
                  message->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
        EXPECT_EQ(
            "boing boing",
            message->headers().get(Http::LowerCaseString("baz-boing"))[0]->value().getStringView());
        EXPECT_EQ("boing boing boing", message->headers()
                                           .get(Http::LowerCaseString("boing-boing"))[0]
                                           ->value()
                                           .getStringView());

        return &request_;
      }));

  // `~AgentHTTPClient()` will cancel the request since we don't finish it here.
  EXPECT_CALL(request_, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client_.post(
      url_, set_headers, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
}

TEST_F(DatadogAgentHttpClientTest, RequestBody) {
  // The `body` parameter to `AgentHTTPClient::post` corresponds to the
  // resulting `Http::RequestMessage::body()`.

  const std::string body = R"body(
    Butterfly in the sky
    I can go twice as high
    Take a look
    It's in a book
    A reading rainbow

    I can go anywhere
    Friends to know
    And ways to grow
    A reading rainbow

    I can be anything
    Take a look
    It's in a book
    A reading rainbow)body";

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [this, &body](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ(body, message->body().toString());
            return &request_;
          }));

  // `~AgentHTTPClient()` will cancel the request since we don't finish it here.
  EXPECT_CALL(request_, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result = client_.post(
      url_, ignore, body, ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
}

TEST_F(DatadogAgentHttpClientTest, OnResponse200) {
  // When `onSuccess` is invoked on the `Http::AsyncClient::Callbacks`, the
  // associated `on_response` callback is invoked with corresponding arguments.
  // Additionally, if the HTTP response status is 200, `stats_.reports_sent_` is
  // incremented.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  // `callbacks_->onSuccess(...)` will cause `on_response_` to be called.
  // `on_error_` will not be called.
  EXPECT_CALL(on_response_, Call(200, _, "{}"));
  EXPECT_CALL(on_error_, Call(_)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been successfully fulfilled.
  EXPECT_CALL(request_, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  callbacks_->onSuccess(request_, std::move(msg));
  EXPECT_EQ(1, stats_.reports_sent_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
}

TEST_F(DatadogAgentHttpClientTest, OnResponseNot200) {
  // When `onSuccess` is invoked on the `Http::AsyncClient::Callbacks`, the
  // associated `on_response` callback is invoked with corresponding arguments.
  // Additionally, if the HTTP response status is not 200,
  // `stats_.reports_dropped_` is incremented.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  // `callbacks_->onSuccess(...)` will cause `on_response_` to be called.
  // The `404` value corresponds to the response sent below.
  // `on_error_` will not be called.
  EXPECT_CALL(on_response_, Call(404, _, "{}"));
  EXPECT_CALL(on_error_, Call(_)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been successfully fulfilled.
  EXPECT_CALL(request_, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  // The "404" below is what causes `stats.reports_failed_` to be incremented
  // instead of `stats.reports_sent_`.
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));
  msg->body().add("{}");

  callbacks_->onSuccess(request_, std::move(msg));
  EXPECT_EQ(1, stats_.reports_dropped_.value());
  EXPECT_EQ(0, stats_.reports_sent_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
}

TEST_F(DatadogAgentHttpClientTest, OnResponseBogusRequest) {
  // When `onSuccess` is invoked on the `Http::AsyncClient::Callbacks` with a
  // request that is not registered with the HTTP client, then no callback is
  // invoked (how would we look it up?).

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  // `callbacks_->onSuccess(...)` will not invoke any callbacks, because the
  // request argument passed in is not registered with the HTTP client.
  EXPECT_CALL(on_response_, Call(_, _, _)).Times(0);
  EXPECT_CALL(on_error_, Call(_)).Times(0);

  // The request will will canceled by `~AgentHTTPClient` because `onSuccess`
  // was passed the wrong request, and so the real request is never removed from
  // the HTTP client's registry.
  EXPECT_CALL(request_, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  // The first argument to `onSuccess` should be `request_`, but instead we pass
  // `bogus_request`.
  Http::MockAsyncClientRequest bogus_request(
      &cluster_manager_.instance_.thread_local_cluster_.async_client_);
  callbacks_->onSuccess(bogus_request, std::move(msg));
}

TEST_F(DatadogAgentHttpClientTest, OnErrorStreamReset) {
  // When `onFailure` is invoked on the `Http::AsyncClient::Callbacks` with
  // `FailureReason::Reset`, the associated `on_error` callback is invoked with
  // a corresponding `datadog::tracing::Error`.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  // `callbacks_->onFailure(...)` will cause `on_error_` to be called.
  // `on_response_` will not be called.
  EXPECT_CALL(on_error_, Call(_)).WillOnce(Invoke([](datadog::tracing::Error error) {
    EXPECT_EQ(error.code, datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE);
  }));
  EXPECT_CALL(on_response_, Call(_, _, _)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been fulfilled.
  EXPECT_CALL(request_, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  callbacks_->onFailure(request_, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(DatadogAgentHttpClientTest, OnErrorExceedResponseBufferLimit) {
  // When `onFailure` is invoked on the `Http::AsyncClient::Callbacks` with
  // `FailureReason::ExceedResponseBufferLimit`, the associated `on_error` callback is invoked
  // with a corresponding `datadog::tracing::Error`.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  // `callbacks_->onFailure(...)` will cause `on_error_` to be called.
  // `on_response_` will not be called.
  EXPECT_CALL(on_error_, Call(_)).WillOnce(Invoke([](datadog::tracing::Error error) {
    EXPECT_EQ(error.code, datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE);
  }));
  EXPECT_CALL(on_response_, Call(_, _, _)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been fulfilled.
  EXPECT_CALL(request_, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  callbacks_->onFailure(request_, Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
}

TEST_F(DatadogAgentHttpClientTest, OnErrorOther) {
  // When `onFailure` is invoked on the `Http::AsyncClient::Callbacks` with any
  // value other than `FailureReason::Reset`, the associated `on_error` callback
  // is invoked with a corresponding `datadog::tracing::Error`.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  // `callbacks->onFailure(...)` will cause `on_error_` to be called.
  // `on_response_` will not be called.
  EXPECT_CALL(on_error_, Call(_)).WillOnce(Invoke([](datadog::tracing::Error error) {
    EXPECT_EQ(error.code, datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE);
  }));
  EXPECT_CALL(on_response_, Call(_, _, _)).Times(0);

  // The request will not be canceled; neither explicitly nor in
  // `~AgentHTTPClient`, because it will have been fulfilled.
  EXPECT_CALL(request_, cancel()).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  const auto bogus_value = static_cast<Http::AsyncClient::FailureReason>(-1);
  callbacks_->onFailure(request_, bogus_value);
}

TEST_F(DatadogAgentHttpClientTest, OnErrorBogusRequest) {
  // When `onFailure` is invoked with a request that's not registered with the
  // HTTP client, no callbacks are invoked.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            return &request_;
          }));

  EXPECT_CALL(on_error_, Call(_)).Times(0);
  EXPECT_CALL(on_response_, Call(_, _, _)).Times(0);

  // The request will will canceled by `~AgentHTTPClient` because `onFailure`
  // was passed the wrong request, and so the real request is never removed from
  // the HTTP client's registry.
  EXPECT_CALL(request_, cancel());

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "{}", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  EXPECT_TRUE(result) << result.error();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  msg->body().add("{}");

  // The first argument to `onFailure` should be `request_`, but instead we pass
  // `bogus_request`.
  Http::MockAsyncClientRequest bogus_request(
      &cluster_manager_.instance_.thread_local_cluster_.async_client_);
  callbacks_->onFailure(bogus_request, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(DatadogAgentHttpClientTest, SendFailReturnsError) {
  // If the underlying call to `httpAsyncClient().send(...)` returns an error,
  // then the enclosing call to `AgentHTTPClient::post(...)` returns an error.

  EXPECT_CALL(cluster_manager_.instance_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_ = &callbacks_arg;
            // As of this writing, any time that `send` returns `nullptr`,
            // `onSuccess` will also be called with a status of 503, even though
            // no request was sent and so no response was received.
            // `AgentHTTPClient` does not depend on this behavior, but we
            // reproduce it here for authenticity.
            // The relevant branch in `AgentHTTPClient::onSuccess` is the one
            // where `handlers_.find` returns `handlers.end()`.
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
            callbacks_arg.onSuccess(request_, std::move(response));
            return nullptr; // indicates error
          }));

  // Neither callback will be invoked, because `post` fails immediately (synchronously).
  EXPECT_CALL(on_error_, Call(_)).Times(0);
  EXPECT_CALL(on_response_, Call(_, _, _)).Times(0);

  const auto ignore = [](auto&&...) {};
  datadog::tracing::Expected<void> result =
      client_.post(url_, ignore, "", on_response_.AsStdFunction(), on_error_.AsStdFunction(),
                   time_.monotonicTime() + std::chrono::seconds(1));
  ASSERT_FALSE(result);
  EXPECT_EQ(datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE, result.error().code);
  EXPECT_EQ(1, stats_.reports_failed_.value());
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
}

TEST_F(DatadogAgentHttpClientTest, SendCalculatedTimeoutIsZero) {
  // If the `deadline` argument to `AgentHTTPClient::post` is such that the
  // timeout calculated by `post` truncates to exactly zero milliseconds, then
  // `post` will return an error and increment `stats_.reports_dropped_`.

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats_, time_);

  const auto ignore = [](auto&&...) {};
  const auto deadline = time_.monotonicTime() + std::chrono::seconds(1);
  time_.setMonotonicTime(deadline);
  datadog::tracing::Expected<void> result = client.post(url_, ignore, "", ignore, ignore, deadline);
  ASSERT_FALSE(result);
  EXPECT_EQ(datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE, result.error().code);
  EXPECT_EQ(1, stats_.reports_dropped_.value());
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
}

TEST_F(DatadogAgentHttpClientTest, SendCalculatedTimeoutIsNegative) {
  // If the `deadline` argument to `AgentHTTPClient::post` is such that the
  // timeout calculated by `post` truncates to a negative number of
  // milliseconds, then `post` will return an error and increment
  // `stats_.reports_dropped_`.

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  AgentHTTPClient client(cluster_manager, "fake_cluster", "test_host", stats_, time_);

  const auto ignore = [](auto&&...) {};
  const auto deadline = time_.monotonicTime() + std::chrono::seconds(1);
  time_.setMonotonicTime(deadline + std::chrono::seconds(1));
  datadog::tracing::Expected<void> result = client.post(url_, ignore, "", ignore, ignore, deadline);
  ASSERT_FALSE(result);
  EXPECT_EQ(datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE, result.error().code);
  EXPECT_EQ(1, stats_.reports_dropped_.value());
  EXPECT_EQ(0, stats_.reports_skipped_no_cluster_.value());
  EXPECT_EQ(0, stats_.reports_failed_.value());
}

TEST_F(DatadogAgentHttpClientTest, DrainIsANoOp) {
  // `AgentHTTPClient::drain` doesn't do anything. It only makes sense in
  // multi-threaded contexts.
  // This test is for the sake of coverage.

  // `deadline` value doesn't matter; `drain` ignores it.
  const auto deadline = std::chrono::steady_clock::time_point::min();
  client_.drain(deadline);
}

TEST_F(DatadogAgentHttpClientTest, ConfigJSONContainsTypeName) {
  nlohmann::json config = client_.config_json();
  EXPECT_EQ("Envoy::Extensions::Tracers::Datadog::AgentHTTPClient", config["type"]);
}

TEST_F(DatadogAgentHttpClientTest, OnBeforeFinalizeUpstreamSpanIsANoOp) {
  // `AgentHTTPClient::onBeforeFinalizeUpstreamSpan` doesn't do anything.
  // This test is for the sake of coverage.
  Tracing::NullSpan null_span;
  client_.onBeforeFinalizeUpstreamSpan(null_span, nullptr);
}

TEST_F(DatadogAgentHttpClientTest, SkipReportIfCollectorClusterHasBeenRemoved) {
  // Verify the effect of onClusterAddOrUpdate()/onClusterRemoval() on reporting logic,
  // keeping in mind that they will be called both for relevant and irrelevant clusters.
  NiceMock<Upstream::MockClusterManager>& cm = cluster_manager_.instance_;
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks =
      cluster_manager_.cluster_update_callbacks_;

  {
    // Simulate removal of the relevant cluster.
    cluster_update_callbacks->onClusterRemoval("fake_cluster");

    // Verify that no report will be sent.
    EXPECT_CALL(cm.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Attempt to send a request.
    const auto ignore = [](auto&&...) {};
    datadog::tracing::Expected<void> result = client_.post(
        url_, ignore, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
    EXPECT_TRUE(result);

    // Verify observability.
    EXPECT_EQ(1U, stats_.reports_skipped_no_cluster_.value());
    EXPECT_EQ(0U, stats_.reports_sent_.value());
    EXPECT_EQ(0U, stats_.reports_dropped_.value());
    EXPECT_EQ(0U, stats_.reports_failed_.value());
  }

  {
    // Simulate addition of an irrelevant cluster.
    NiceMock<Upstream::MockThreadLocalCluster> unrelated_cluster;
    unrelated_cluster.cluster_.info_->name_ = "unrelated_cluster";
    Upstream::ThreadLocalClusterCommand command =
        [&unrelated_cluster]() -> Upstream::ThreadLocalCluster& { return unrelated_cluster; };
    cluster_update_callbacks->onClusterAddOrUpdate(unrelated_cluster.cluster_.info_->name_,
                                                   command);
    // Verify that no report will be sent.
    EXPECT_CALL(cm.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Attempt to send a request.
    const auto ignore = [](auto&&...) {};
    datadog::tracing::Expected<void> result = client_.post(
        url_, ignore, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
    EXPECT_TRUE(result);

    // Verify observability.
    EXPECT_EQ(2U, stats_.reports_skipped_no_cluster_.value());
    EXPECT_EQ(0U, stats_.reports_sent_.value());
    EXPECT_EQ(0U, stats_.reports_dropped_.value());
    EXPECT_EQ(0U, stats_.reports_failed_.value());
  }

  {
    // Simulate addition of the relevant cluster.
    Upstream::ThreadLocalClusterCommand command = [&cm]() -> Upstream::ThreadLocalCluster& {
      return cm.thread_local_cluster_;
    };
    cluster_update_callbacks->onClusterAddOrUpdate(cm.thread_local_cluster_.info()->name(),
                                                   command);

    // Verify that report will be sent.
    EXPECT_CALL(cm.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Attempt to send a request.
    const auto ignore = [](auto&&...) {};
    datadog::tracing::Expected<void> result = client_.post(
        url_, ignore, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
    EXPECT_TRUE(result);

    // Complete in-flight request.
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

    // Verify observability.
    EXPECT_EQ(2U, stats_.reports_skipped_no_cluster_.value());
    EXPECT_EQ(0U, stats_.reports_sent_.value());
    EXPECT_EQ(0U, stats_.reports_dropped_.value());
    EXPECT_EQ(1U, stats_.reports_failed_.value());
  }

  {
    // Simulate removal of an irrelevant cluster.
    cluster_update_callbacks->onClusterRemoval("unrelated_cluster");

    // Verify that report will be sent.
    EXPECT_CALL(cm.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Attempt to send a request.
    const auto ignore = [](auto&&...) {};
    datadog::tracing::Expected<void> result = client_.post(
        url_, ignore, "", ignore, ignore, time_.monotonicTime() + std::chrono::seconds(1));
    EXPECT_TRUE(result);

    // Complete in-flight request.
    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));
    callback->onSuccess(request, std::move(msg));

    // Verify observability.
    EXPECT_EQ(2U, stats_.reports_skipped_no_cluster_.value());
    EXPECT_EQ(0U, stats_.reports_sent_.value());
    EXPECT_EQ(1U, stats_.reports_dropped_.value());
    EXPECT_EQ(1U, stats_.reports_failed_.value());
  }
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
