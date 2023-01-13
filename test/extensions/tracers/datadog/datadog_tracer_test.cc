#include <datadog/sampling_priority.h>
#include <datadog/span.h>
#include <datadog/trace_segment.h>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/trace/v3/datadog.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/config.h"
#include "source/extensions/tracers/datadog/dd.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/tracer.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
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

const auto flush_interval = std::chrono::milliseconds(2000);

class DatadogDriverTest : public testing::Test {
public:
  void setup(envoy::config::trace::v3::DatadogConfig& datadog_config, bool init_timer) {
    cm_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cm_.initializeThreadLocalClusters({"fake_cluster"});

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(flush_interval, _));
    }

    driver_ = std::make_unique<Tracer>(
        datadog_config.collector_cluster(),
        DatadogTracerFactory::makeCollectorReferenceHost(datadog_config),
        DatadogTracerFactory::makeConfig(datadog_config), cm_, *stats_.rootScope(), tls_);
  }

  void setupValidDriver() {
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::DatadogConfig datadog_config;
    TestUtility::loadFromYaml(yaml_string, datadog_config);

    cm_.initializeClusters({"fake_cluster"}, {});
    setup(datadog_config, true);
  }

  const std::string operation_name_{"test"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestResponseHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;

  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;

  NiceMock<Tracing::MockConfig> config_;
  std::unique_ptr<Tracer> driver_;
};

TEST_F(DatadogDriverTest, InitializeDriver) {
  {
    envoy::config::trace::v3::DatadogConfig datadog_config;

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::DatadogConfig datadog_config;
    TestUtility::loadFromYaml(yaml_string, datadog_config);

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::DatadogConfig datadog_config;
    TestUtility::loadFromYaml(yaml_string, datadog_config);

    cm_.initializeClusters({"fake_cluster"}, {});
    setup(datadog_config, true);
  }
}

TEST_F(DatadogDriverTest, AllowCollectorClusterToBeAddedViaApi) {
  cm_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, addedViaApi()).WillByDefault(Return(true));

  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  )EOF";
  envoy::config::trace::v3::DatadogConfig datadog_config;
  TestUtility::loadFromYaml(yaml_string, datadog_config);

  setup(datadog_config, true);
}

TEST_F(DatadogDriverTest, FlushSpansTimer) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
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
  EXPECT_CALL(*timer_, enableTimer(flush_interval, _));

  timer_->invokeCallback();

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

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
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
  EXPECT_CALL(*timer_, enableTimer(flush_interval, _));

  timer_->invokeCallback();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "0"}}}));
  callback->onSuccess(request, std::move(msg));

  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
  EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
}

TEST_F(DatadogDriverTest, CollectorHostname) {
  // We expect "fake_host" to be the Host header value, instead of the default
  // "fake_cluster".
  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  collector_hostname: fake_host
  )EOF";
  envoy::config::trace::v3::DatadogConfig datadog_config;
  TestUtility::loadFromYaml(yaml_string, datadog_config);
  cm_.initializeClusters({"fake_cluster"}, {});
  setup(datadog_config, true);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            // This is the crux of this test.
            EXPECT_EQ("fake_host", message->headers().getHostValue());

            return &request;
          }));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(flush_interval, _));

  timer_->invokeCallback();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "0"}}}));
  callback->onSuccess(request, std::move(msg));
}

TEST_F(DatadogDriverTest, SkipReportIfCollectorClusterHasBeenRemoved) {
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks;
  EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
      .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks), Return(nullptr)));

  setupValidDriver();

  EXPECT_CALL(*timer_, enableTimer(flush_interval, _)).Times(AnyNumber());

  // Verify the effect of onClusterAddOrUpdate()/onClusterRemoval() on reporting logic,
  // keeping in mind that they will be called both for relevant and irrelevant clusters.

  {
    // Simulate removal of the relevant cluster.
    cluster_update_callbacks->onClusterRemoval("fake_cluster");

    // Verify that no report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    timer_->invokeCallback();

    // Verify observability.
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
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    timer_->invokeCallback();

    // Verify observability.
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
  }

  {
    // Simulate addition of the relevant cluster.
    cluster_update_callbacks->onClusterAddOrUpdate(cm_.thread_local_cluster_);

    // Verify that report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
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
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_failed").value());
  }

  {
    // Simulate removal of an irrelevant cluster.
    cluster_update_callbacks->onClusterRemoval("unrelated_cluster");

    // Verify that report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
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
    EXPECT_EQ(2U, stats_.counter("tracing.datadog.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_sent").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_dropped").value());
    EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_failed").value());
  }
}

TEST_F(DatadogDriverTest, CancelInflightRequestsOnDestruction) {
  setupValidDriver();

  StrictMock<Http::MockAsyncClientRequest> request1(&cm_.thread_local_cluster_.async_client_),
      request2(&cm_.thread_local_cluster_.async_client_),
      request3(&cm_.thread_local_cluster_.async_client_),
      request4(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback{};
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));

  // Expect 4 separate report requests to be made.
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request1)))
      .WillOnce(Return(&request2))
      .WillOnce(Return(&request3))
      .WillOnce(Return(&request4));
  // Expect timer to be re-enabled on each tick.
  EXPECT_CALL(*timer_, enableTimer(flush_interval, _)).Times(4);

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

TEST_F(DatadogDriverTest, RequestHeaderWriter) {
  Http::TestRequestHeaderMapImpl headers;
  RequestHeaderWriter writer{headers};

  writer.set("foo", "bar");
  writer.set("FOO", "baz");
  writer.set("sniff", "wiggle");

  auto result = headers.get(Http::LowerCaseString{"foo"});
  ASSERT_EQ(1, result.size());
  EXPECT_EQ("baz", result[0]->value().getStringView());

  result = headers.get(Http::LowerCaseString{"sniff"});
  ASSERT_EQ(1, result.size());
  EXPECT_EQ("wiggle", result[0]->value().getStringView());

  result = headers.get(Http::LowerCaseString{"missing"});
  EXPECT_EQ(0, result.size());
}

TEST_F(DatadogDriverTest, ResponseHeaderReader) {
  Http::TestResponseHeaderMapImpl headers{
      {"fish", "face"},
      {"fish", "flakes"},
      {"UPPER", "case"},
  };
  ResponseHeaderReader reader{headers};

  auto result = reader.lookup("fish");
  EXPECT_EQ("face, flakes", result);

  result = reader.lookup("upper");
  EXPECT_EQ("case", result);

  result = reader.lookup("missing");
  EXPECT_EQ(dd::nullopt, result);

  std::vector<std::pair<std::string, std::string>> expected_visitation{
      // These entries are in reverse. We'll `pop_back` as we go.
      {"upper", "case"},
      // `lookup` comma-separates duplicate headers, but `visit` does not.
      {"fish", "flakes"},
      {"fish", "face"},
  };
  reader.visit([&](const auto& key, const auto& value) {
    ASSERT_FALSE(expected_visitation.empty());
    const auto& [expected_key, expected_value] = expected_visitation.back();
    EXPECT_EQ(expected_key, key);
    EXPECT_EQ(expected_value, value);
    expected_visitation.pop_back();
  });
}

TEST_F(DatadogDriverTest, TraceContextReader) {
  // `dd-trace-cpp` doesn't call `visit` when it's extracting trace context, but
  // the method is nonetheless required by the `DictReader` interface.
  const Tracing::TestTraceContextImpl context{{"foo", "bar"}, {"boo", "yah"}};
  const TraceContextReader reader{context};
  reader.visit([&](const auto& key, const auto& value) {
    const auto found = context.context_map_.find(key);
    ASSERT_NE(context.context_map_.end(), found);
    EXPECT_EQ(found->second, value);
  });
}

class EnvVarGuard {
  const std::string name_;
  const absl::optional<std::string> previous_value_;

public:
  EnvVarGuard(const std::string& name, const std::string& value)
      : name_(name), previous_value_(TestEnvironment::getOptionalEnvVar(name)) {
    const bool overwrite = true;
    TestEnvironment::setEnvVar(name_, value, overwrite);
  }

  ~EnvVarGuard() {
    if (previous_value_) {
      const bool overwrite = true;
      TestEnvironment::setEnvVar(name_, *previous_value_, overwrite);
    } else {
      TestEnvironment::unsetEnvVar(name_);
    }
  }
};

TEST_F(DatadogDriverTest, NoOpMode) {
  // Set an environment variable that results in a bogus Datadog tracing
  // configuration. The tracer goes into no-op mode after logging a diagnostic.
  EnvVarGuard guard{"DD_TRACE_SAMPLING_RULES", "this is not JSON at all"};

  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  )EOF";
  envoy::config::trace::v3::DatadogConfig datadog_config;
  TestUtility::loadFromYaml(yaml_string, datadog_config);

  cm_.initializeClusters({"fake_cluster"}, {});
  const bool setup_timer = false;
  setup(datadog_config, setup_timer);

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  ASSERT_NE(nullptr, span);

  const auto as_expected_type = dynamic_cast<Tracing::NullSpan*>(span.get());
  ASSERT_NE(nullptr, as_expected_type);

  span->finishSpan();
}

TEST_F(DatadogDriverTest, Span) {
  // May 6, 2010 2:32 PM EST
  const auto now = std::chrono::system_clock::from_time_t(std::time_t(1273170720));
  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  )EOF";
  envoy::config::trace::v3::DatadogConfig datadog_config;
  TestUtility::loadFromYaml(yaml_string, datadog_config);

  cm_.initializeClusters({"fake_cluster"}, {});
  const bool setup_timer = false;
  setup(datadog_config, setup_timer);

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, false});
  ASSERT_NE(nullptr, span);

  const auto span_impl = dynamic_cast<Span*>(span.get());
  ASSERT_NE(nullptr, span_impl);

  const dd::Optional<dd::Span>& dd_span = span_impl->impl();
  ASSERT_TRUE(dd_span);

  // Neither `Tracing::Span` nor `dd::Span` exposes a "get span" method.
  span->setOperation("pig.bristle");

  span->setTag("foo", "bar");
  auto result = dd_span->lookup_tag("foo");
  EXPECT_TRUE(result);
  EXPECT_EQ("bar", *result);

  // `log` doesn't do anything.
  span->log(now, "message");

  Tracing::TestTraceContextImpl context{};
  span->injectContext(context, nullptr);
  EXPECT_EQ("", context.context_protocol_);
  EXPECT_EQ("", context.context_authority_);
  EXPECT_EQ("", context.context_path_);
  EXPECT_EQ("", context.context_method_);

  auto found = context.context_map_.find("x-datadog-trace-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(dd_span->trace_id()), found->second);
  found = context.context_map_.find("x-datadog-parent-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(dd_span->id()), found->second);
  found = context.context_map_.find("x-datadog-sampling-priority");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(int(dd::SamplingPriority::USER_DROP)), found->second);

  auto child = span->spawnChild(config_, "do.foo", now);
  ASSERT_NE(nullptr, child);
  const auto child_impl = dynamic_cast<Span*>(child.get());
  ASSERT_NE(nullptr, child_impl);
  const dd::Optional<dd::Span>& dd_child = child_impl->impl();
  ASSERT_TRUE(dd_child);

  EXPECT_EQ(dd_child->trace_id(), dd_span->trace_id());
  EXPECT_NE(dd_child->id(), dd_span->id());

  span->setSampled(false);
  auto decision = dd_span->trace_segment().sampling_decision();
  ASSERT_TRUE(decision);
  // The sampling priority is "user drop," i.e. -1.
  EXPECT_EQ(int(dd::SamplingPriority::USER_DROP), decision->priority);

  // `setBaggage` does nothing (there's no way to check, really).
  span->setBaggage("foo", "bar");

  std::string expected_trace_id_hex = absl::StrFormat("%x", dd_span->trace_id());
  EXPECT_EQ(expected_trace_id_hex, span->getTraceIdAsHex());

  span->finishSpan();

  // After the span is finished, its methods become no-ops, because its
  // implementation (the dd::Span inside) has been destroyed.
  EXPECT_FALSE(dd_span);

  span->finishSpan();
  span->setTag("bar", "foo");
  span->setOperation("done");
  span->setSampled(true);

  Tracing::TestTraceContextImpl empty_context{};
  span->injectContext(empty_context, nullptr);
  EXPECT_EQ("", empty_context.context_protocol_);
  EXPECT_EQ("", empty_context.context_authority_);
  EXPECT_EQ("", empty_context.context_path_);
  EXPECT_EQ("", empty_context.context_method_);
  EXPECT_EQ(0, empty_context.context_map_.size());

  child = span->spawnChild(config_, "null", now);
  ASSERT_NE(nullptr, child);
  const auto as_expected_type = dynamic_cast<Tracing::NullSpan*>(child.get());
  ASSERT_NE(nullptr, as_expected_type);
}

TEST_F(DatadogDriverTest, ExtractionFailure) {
  Http::TestRequestHeaderMapImpl headers{request_headers_};
  // In addition to the headers in `request_headers_`, add a malformed
  // X-Datadog-Trace-ID header. Extraction of trace context will fail, and
  // the tracer will instead fall back to starting a new trace.
  //
  //     Unable to extract span context. Creating a new trace instead.
  //     Error [error 16]: Could not extract Datadog-style traceID from
  //     x-datadog-trace-id: 12345whoops! Integer has trailing characters in:
  //     "12345whoops!"
  headers.setByKey("x-datadog-trace-id", "12345whoops!");

  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  )EOF";
  envoy::config::trace::v3::DatadogConfig datadog_config;
  TestUtility::loadFromYaml(yaml_string, datadog_config);

  cm_.initializeClusters({"fake_cluster"}, {});
  const bool setup_timer = false;
  setup(datadog_config, setup_timer);

  Tracing::SpanPtr span = driver_->startSpan(config_, headers, operation_name_, start_time_,
                                             {Tracing::Reason::Sampling, true});
  ASSERT_NE(nullptr, span);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
