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
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/tracer.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"
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
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

template <typename Config> Config makeConfig(const std::string& yaml) {
  Config result;
  TestUtility::loadFromYaml(yaml, result);
  return result;
}

const std::chrono::milliseconds flush_interval(2000);

class DatadogConfigTest : public testing::Test {
public:
  void setup(envoy::config::trace::v3::DatadogConfig& datadog_config, bool init_timer) {
    cm_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cm_.initializeThreadLocalClusters({"fake_cluster"});

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(flush_interval, _));
    }

    tracer_ = std::make_unique<Tracer>(
        datadog_config.collector_cluster(),
        DatadogTracerFactory::makeCollectorReferenceHost(datadog_config),
        DatadogTracerFactory::makeConfig(datadog_config), cm_, *stats_.rootScope(), tls_, time_);
  }

  void setupValidDriver() {
    auto datadog_config =
        makeConfig<envoy::config::trace::v3::DatadogConfig>("collector_cluster: fake_cluster");

    cm_.initializeClusters({"fake_cluster"}, {});
    setup(datadog_config, true);
  }

  const std::string operation_name_{"test"};
  Tracing::TestTraceContextImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};

  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Upstream::MockClusterManager> cm_;

  NiceMock<Tracing::MockConfig> config_;
  std::unique_ptr<Tracer> tracer_;
  Event::SimulatedTimeSystem time_;
};

TEST_F(DatadogConfigTest, DefaultConfiguration) {
  envoy::config::trace::v3::DatadogConfig datadog_config;
  EXPECT_EQ(datadog_config.has_remote_config(), false);
}

TEST_F(DatadogConfigTest, ConfigureTracer) {
  {
    envoy::config::trace::v3::DatadogConfig datadog_config;

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    auto datadog_config =
        makeConfig<envoy::config::trace::v3::DatadogConfig>("collector_cluster: fake_cluster");

    EXPECT_THROW(setup(datadog_config, false), EnvoyException);
  }

  {
    const std::string yaml_conf = R"EOF(
      collector_cluster: fake_cluster
      remote_config: {}
    )EOF";

    auto datadog_config = makeConfig<envoy::config::trace::v3::DatadogConfig>(yaml_conf);

    cm_.initializeClusters({"fake_cluster"}, {});

    EXPECT_CALL(tls_.dispatcher_, createTimer_(testing::_)).Times(2);
    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callbacks;
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              callbacks = &callbacks_arg;
              return &request;
            }));

    setup(datadog_config, true);

    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));
    msg->body().add("{}");
    callbacks->onSuccess(request, std::move(msg));

    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              callbacks = &callbacks_arg;
              return &request;
            }));
    EXPECT_CALL(request, cancel());
    tracer_.reset();
  }
}

TEST_F(DatadogConfigTest, ConfigureViaFactory) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});

  auto configuration = makeConfig<envoy::config::trace::v3::Tracing>(R"EOF(
  http:
    name: datadog
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.DatadogConfig
      collector_cluster: fake_cluster
      service_name: fake_file
      remote_config:
        polling_interval: "10s"
   )EOF");

  DatadogTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto datadog_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, datadog_tracer);
}

TEST_F(DatadogConfigTest, AllowCollectorClusterToBeAddedViaApi) {
  cm_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, addedViaApi()).WillByDefault(Return(true));

  auto datadog_config =
      makeConfig<envoy::config::trace::v3::DatadogConfig>("collector_cluster: fake_cluster");

  EXPECT_CALL(tls_.dispatcher_, createTimer_(testing::_));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));

  setup(datadog_config, true);

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));
  msg->body().add("{}");
  callbacks->onSuccess(request, std::move(msg));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;
            return &request;
          }));
  EXPECT_CALL(request, cancel());
  tracer_.reset();
}

TEST_F(DatadogConfigTest, CollectorHostname) {
  // We expect "fake_host" to be the Host header value, instead of the default
  // "fake_cluster".
  auto datadog_config = makeConfig<envoy::config::trace::v3::DatadogConfig>(R"EOF(
  collector_cluster: fake_cluster
  collector_hostname: fake_host
  )EOF");
  cm_.initializeClusters({"fake_cluster"}, {});

  EXPECT_CALL(tls_.dispatcher_, createTimer_(testing::_));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;

            EXPECT_EQ("fake_host", message->headers().getHostValue());

            return &request;
          }));

  setup(datadog_config, true);

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));
  msg->body().add("{}");
  callbacks->onSuccess(request, std::move(msg));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;

            // This is the crux of this test.
            EXPECT_EQ("fake_host", message->headers().getHostValue());

            return &request;
          }));

  Tracing::SpanPtr span = tracer_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(flush_interval, _));

  timer_->invokeCallback();

  msg = std::make_unique<Http::ResponseMessageImpl>(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}});
  msg->body().add("{}");
  callbacks->onSuccess(request, std::move(msg));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks_arg,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &callbacks_arg;

            EXPECT_EQ("fake_host", message->headers().getHostValue());

            return &request;
          }));
  EXPECT_CALL(request, cancel());
  tracer_.reset();
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
