#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/config/trace/v3/datadog.pb.h"

#include "common/common/base64.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/datadog/datadog_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class DatadogDriverTest : public testing::Test {
public:
  void setup(envoy::config::trace::v3::DatadogConfig& datadog_config, bool init_timer) {
    cm_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    ON_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(900), _));
    }

    driver_ = std::make_unique<Driver>(datadog_config, cm_, stats_, tls_, runtime_);
  }

  void setupValidDriver() {
    EXPECT_CALL(cm_, get(Eq("fake_cluster"))).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::DatadogConfig datadog_config;
    TestUtility::loadFromYaml(yaml_string, datadog_config);

    setup(datadog_config, true);
  }

  const std::string operation_name_{"test"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestResponseHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;

  NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<Driver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;

  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(DatadogDriverTest, InitializeDriver) {
  {
    envoy::config::trace::v3::DatadogConfig datadog_config;

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get(Eq("fake_cluster"))).WillOnce(Return(nullptr));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::DatadogConfig datadog_config;
    TestUtility::loadFromYaml(yaml_string, datadog_config);

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, get(Eq("fake_cluster"))).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::DatadogConfig datadog_config;
    TestUtility::loadFromYaml(yaml_string, datadog_config);

    setup(datadog_config, true);
  }
}

TEST_F(DatadogDriverTest, AllowCollectorClusterToBeAddedViaApi) {
  EXPECT_CALL(cm_, get(Eq("fake_cluster"))).WillRepeatedly(Return(&cm_.thread_local_cluster_));
  ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, addedViaApi()).WillByDefault(Return(true));

  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  )EOF";
  envoy::config::trace::v3::DatadogConfig datadog_config;
  TestUtility::loadFromYaml(yaml_string, datadog_config);

  setup(datadog_config, true);
}

TEST_F(DatadogDriverTest, FlushSpansTimer) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));
  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/msgpack", message->headers().getContentTypeValue());

            return &request;
          }));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(900), _));

  timer_->invokeCallback();

  EXPECT_EQ(1U, stats_.counter("tracing.datadog.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.datadog.traces_sent").value());

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));

  callback->onSuccess(request, std::move(msg));

  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
  EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
}

TEST_F(DatadogDriverTest, NoBody) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));
  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/msgpack", message->headers().getContentTypeValue());

            return &request;
          }));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(900), _));

  timer_->invokeCallback();

  EXPECT_EQ(1U, stats_.counter("tracing.datadog.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.datadog.traces_sent").value());

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "0"}}}));
  callback->onSuccess(request, std::move(msg));

  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
  EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
}

TEST_F(DatadogDriverTest, SkipReportIfCollectorClusterHasBeenRemoved) {
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks;
  EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
      .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks), Return(nullptr)));

  setupValidDriver();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(900), _)).Times(AnyNumber());

  // Verify the effect of onClusterAddOrUpdate()/onClusterRemoval() on reporting logic,
  // keeping in mind that they will be called both for relevant and irrelevant clusters.

  {
    // Simulate removal of the relevant cluster.
    cluster_update_callbacks->onClusterRemoval("fake_cluster");

    // Verify that no report will be sent.
    EXPECT_CALL(cm_, httpAsyncClientForCluster(_)).Times(0);
    EXPECT_CALL(cm_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    timer_->invokeCallback();

    // Verify observability.
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.timer_flushed").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.traces_sent").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
  }

  {
    // Simulate addition of an irrelevant cluster.
    NiceMock<Upstream::MockThreadLocalCluster> unrelated_cluster;
    unrelated_cluster.cluster_.info_->name_ = "unrelated_cluster";
    cluster_update_callbacks->onClusterAddOrUpdate(unrelated_cluster);

    // Verify that no report will be sent.
    EXPECT_CALL(cm_, httpAsyncClientForCluster(_)).Times(0);
    EXPECT_CALL(cm_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    timer_->invokeCallback();

    // Verify observability.
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.timer_flushed").value());
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.traces_sent").value());
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
  }

  {
    // Simulate addition of the relevant cluster.
    cluster_update_callbacks->onClusterAddOrUpdate(cm_.thread_local_cluster_);

    // Verify that report will be sent.
    EXPECT_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillOnce(ReturnRef(cm_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    timer_->invokeCallback();

    // Complete in-flight request.
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

    // Verify observability.
    EXPECT_EQ(3U, stats_.counter("tracing.datadog.timer_flushed").value());
    EXPECT_EQ(3U, stats_.counter("tracing.datadog.traces_sent").value());
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_failed").value());
  }

  {
    // Simulate removal of an irrelevant cluster.
    cluster_update_callbacks->onClusterRemoval("unrelated_cluster");

    // Verify that report will be sent.
    EXPECT_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillOnce(ReturnRef(cm_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    timer_->invokeCallback();

    // Complete in-flight request.
    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));
    callback->onSuccess(request, std::move(msg));

    // Verify observability.
    EXPECT_EQ(4U, stats_.counter("tracing.datadog.timer_flushed").value());
    EXPECT_EQ(4U, stats_.counter("tracing.datadog.traces_sent").value());
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_failed").value());
  }
}

TEST_F(DatadogDriverTest, CancelInflightRequestsOnDestruction) {
  setupValidDriver();

  StrictMock<Http::MockAsyncClientRequest> request1(&cm_.async_client_),
      request2(&cm_.async_client_), request3(&cm_.async_client_), request4(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback{};
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));

  // Expect 4 separate report requests to be made.
  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request1)))
      .WillOnce(Return(&request2))
      .WillOnce(Return(&request3))
      .WillOnce(Return(&request4));
  // Expect timer to be re-enabled on each tick.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(900), _)).Times(4);

  // Trigger 1st report request.
  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  timer_->invokeCallback();
  // Trigger 2nd report request.
  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  timer_->invokeCallback();
  // Trigger 3rd report request.
  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  timer_->invokeCallback();
  // Trigger 4th report request.
  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  timer_->invokeCallback();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));
  // Simulate completion of the 2nd report request.
  callback->onSuccess(request2, std::move(msg));

  // Simulate failure of the 3rd report request.
  callback->onFailure(request3, Http::AsyncClient::FailureReason::Reset);

  // Expect 1st and 4th requests to be cancelled on destruction.
  EXPECT_CALL(request1, cancel());
  EXPECT_CALL(request4, cancel());

  // Trigger destruction.
  driver_.reset();
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
