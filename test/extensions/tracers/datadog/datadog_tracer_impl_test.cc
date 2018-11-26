#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "common/common/base64.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/datadog/datadog_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

class DatadogDriverTest : public testing::Test {
public:
  void setup(envoy::config::trace::v2::DatadogConfig& datadog_config, bool init_timer) {
    ON_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000)));
    }

    driver_ = std::make_unique<Driver>(datadog_config, cm_, stats_, tls_, runtime_);
  }

  void setupValidDriver() {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::DatadogConfig datadog_config;
    MessageUtil::loadFromYaml(yaml_string, datadog_config);

    setup(datadog_config, true);
  }

  const std::string operation_name_{"test"};
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;

  NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<Driver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;

  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(DatadogDriverTest, InitializeDriver) {
  {
    envoy::config::trace::v2::DatadogConfig datadog_config;

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::DatadogConfig datadog_config;
    MessageUtil::loadFromYaml(yaml_string, datadog_config);

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::DatadogConfig datadog_config;
    MessageUtil::loadFromYaml(yaml_string, datadog_config);

    setup(datadog_config, true);
  }
}

TEST_F(DatadogDriverTest, FlushSpansTimer) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(1));
  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/msgpack", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000)));

  timer_->callback_();

  EXPECT_EQ(1U, stats_.counter("tracing.datadog.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.datadog.traces_sent").value());

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  msg->body() = std::make_unique<Buffer::OwnedImpl>("");

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, stats_.counter("tracing.datadog.reports_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.datadog.reports_failed").value());
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
