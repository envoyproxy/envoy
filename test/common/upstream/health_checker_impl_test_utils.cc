#include "test/common/upstream/health_checker_impl_test_utils.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

void HttpHealthCheckerImplTest::allocHealthChecker(const std::string& yaml, bool avoid_boosting) {
  health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *cluster_, parseHealthCheckFromV3Yaml(yaml, avoid_boosting), dispatcher_, runtime_, random_,
      HealthCheckEventLoggerPtr(event_logger_storage_.release()));
}

void HttpHealthCheckerImplTest::addCompletionCallback() {
  health_checker_->addHostCheckCompleteCb(
      [this](HostSharedPtr host, HealthTransition changed_state) -> void {
        onHostStatus(host, changed_state);
      });
}

void HttpHealthCheckerImplTest::setupNoServiceValidationHCWithHttp2() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
      codec_client_type: Http2
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupInitialJitter() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    initial_jitter: 5s
    interval_jitter_percent: 40
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupIntervalJitterPercent() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter_percent: 40
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupNoServiceValidationHC() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupNoServiceValidationHCOneUnhealthy() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupNoServiceValidationHCAlwaysLogFailure() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    always_log_health_check_failures: true
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupNoServiceValidationNoReuseConnectionHC() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    reuse_connection: false
    http_health_check:
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupHealthCheckIntervalOverridesHC() {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_interval: 2s
    unhealthy_edge_interval: 3s
    healthy_edge_interval: 4s
    no_traffic_interval: 5s
    interval_jitter: 0s
    unhealthy_threshold: 3
    healthy_threshold: 3
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupServiceValidationHC() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupDeprecatedServiceNameValidationHC(const std::string& prefix) {
  std::string yaml = fmt::format(R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: {0}
      path: /healthcheck
    )EOF",
                                 prefix);

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupServicePrefixPatternValidationHC() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupServiceExactPatternValidationHC() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        exact: locations-production-iad
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupServiceRegexPatternValidationHC() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        safe_regex:
          google_re2: {}
          regex: 'locations-.*-.*$'
      path: /healthcheck
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupServiceValidationWithCustomHostValueHC(
    const std::string& host) {
  std::string yaml = fmt::format(R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
      host: {0}
    )EOF",
                                 host);

  allocHealthChecker(yaml);
  addCompletionCallback();
}

const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig
HttpHealthCheckerImplTest::makeHealthCheckConfig(const uint32_t port_value) {
  envoy::config::endpoint::v3::Endpoint::HealthCheckConfig config;
  config.set_port_value(port_value);
  return config;
}

void HttpHealthCheckerImplTest::appendTestHosts(std::shared_ptr<MockClusterMockPrioritySet> cluster,
                                                const HostWithHealthCheckMap& hosts,
                                                const std::string& protocol,
                                                const uint32_t priority) {
  for (const auto& host : hosts) {
    cluster->prioritySet().getMockHostSet(priority)->hosts_.emplace_back(
        makeTestHost(cluster->info_, fmt::format("{}{}", protocol, host.first), host.second));
  }
}

void HttpHealthCheckerImplTest::setupServiceValidationWithAdditionalHeaders() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
      host: "www.envoyproxy.io"
      request_headers_to_add:
        - header:
            key: x-envoy-ok
            value: ok
        - header:
            key: x-envoy-cool
            value: cool
        - header:
            key: x-envoy-awesome
            value: awesome
        # The following entry replaces the current user-agent.
        - header:
            key: user-agent
            value: CoolEnvoy/HC
          append: false
        - header:
            key: x-protocol
            value: "%PROTOCOL%"
        - header:
            key: x-upstream-metadata
            value: "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
        - header:
            key: x-downstream-remote-address
            value: "%DOWNSTREAM_REMOTE_ADDRESS%"
        - header:
            key: x-downstream-remote-address-without-port
            value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
        - header:
            key: x-downstream-local-address
            value: "%DOWNSTREAM_LOCAL_ADDRESS%"
        - header:
            key: x-downstream-local-address-without-port
            value: "%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%"
        - header:
            key: x-start-time
            value: "%START_TIME(%s.%9f)%"
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::setupServiceValidationWithoutUserAgent() {
  std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
      host: "www.envoyproxy.io"
      # The following entry removes the default "user-agent" header.
      request_headers_to_remove: ["user-agent"]
    )EOF";

  allocHealthChecker(yaml);
  addCompletionCallback();
}

void HttpHealthCheckerImplTest::expectSessionCreate(
    const HostWithHealthCheckMap& health_check_map) {
  // Expectations are in LIFO order.
  TestSessionPtr new_test_session(new TestSession());
  test_sessions_.emplace_back(std::move(new_test_session));
  TestSession& test_session = *test_sessions_.back();
  test_session.timeout_timer_ = new Event::MockTimer(&dispatcher_);
  test_session.interval_timer_ = new Event::MockTimer(&dispatcher_);
  expectClientCreate(test_sessions_.size() - 1, health_check_map);
}

void HttpHealthCheckerImplTest::expectClientCreate(size_t index,
                                                   const HostWithHealthCheckMap& health_check_map) {
  TestSession& test_session = *test_sessions_[index];
  test_session.codec_ = new NiceMock<Http::MockClientConnection>();
  ON_CALL(*test_session.codec_, protocol()).WillByDefault(testing::Return(Http::Protocol::Http11));
  test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
  connection_index_.push_back(index);
  codec_index_.push_back(index);

  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::InvokeWithoutArgs([&]() -> Network::ClientConnection* {
        uint32_t index = connection_index_.front();
        connection_index_.pop_front();
        return test_sessions_[index]->client_connection_;
      }));
  EXPECT_CALL(*health_checker_, createCodecClient_(_))
      .WillRepeatedly(
          Invoke([&](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
            if (!health_check_map.empty()) {
              const auto& health_check_config =
                  health_check_map.at(conn_data.host_description_->address()->asString());
              // To make sure health checker checks the correct port.
              EXPECT_EQ(health_check_config.port_value(),
                        conn_data.host_description_->healthCheckAddress()->ip()->port());
            }
            uint32_t index = codec_index_.front();
            codec_index_.pop_front();
            TestSession& test_session = *test_sessions_[index];
            std::shared_ptr<Upstream::MockClusterInfo> cluster{
                new NiceMock<Upstream::MockClusterInfo>()};
            Event::MockDispatcher dispatcher_;
            return new CodecClientForTest(
                Http::CodecClient::Type::HTTP1, std::move(conn_data.connection_),
                test_session.codec_, nullptr,
                Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000"), dispatcher_);
          }));
}

void HttpHealthCheckerImplTest::expectStreamCreate(size_t index) {
  test_sessions_[index]->request_encoder_.stream_.callbacks_.clear();
  EXPECT_CALL(*test_sessions_[index]->codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&test_sessions_[index]->stream_response_callbacks_),
                      ReturnRef(test_sessions_[index]->request_encoder_)));
}

void HttpHealthCheckerImplTest::respond(size_t index, const std::string& code, bool conn_close,
                                        bool proxy_close, bool body, bool trailers,
                                        const absl::optional<std::string>& service_cluster,
                                        bool degraded) {
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", code}});

  if (degraded) {
    response_headers->setEnvoyDegraded(1);
  }

  if (service_cluster) {
    response_headers->addCopy(Http::Headers::get().EnvoyUpstreamHealthCheckedCluster,
                              service_cluster.value());
  }
  if (conn_close) {
    response_headers->addCopy("connection", "close");
  }
  if (proxy_close) {
    response_headers->addCopy("proxy-connection", "close");
  }

  test_sessions_[index]->stream_response_callbacks_->decodeHeaders(std::move(response_headers),
                                                                   !body && !trailers);
  if (body) {
    Buffer::OwnedImpl response_data;
    test_sessions_[index]->stream_response_callbacks_->decodeData(response_data, !trailers);
  }

  if (trailers) {
    test_sessions_[index]->stream_response_callbacks_->decodeTrailers(
        Http::ResponseTrailerMapPtr{new Http::TestResponseTrailerMapImpl{{"some", "trailer"}}});
  }
}

void HttpHealthCheckerImplTest::expectSessionCreate() { expectSessionCreate(health_checker_map_); }
void HttpHealthCheckerImplTest::expectClientCreate(size_t index) {
  expectClientCreate(index, health_checker_map_);
}

void HttpHealthCheckerImplTest::expectSuccessStartFailedFailFirst(
    const absl::optional<std::string>& health_checked_cluster) {
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  health_checker_->start();

  // Test that failing first disables fast success.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_, _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true));
  respond(0, "503", false, false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_, _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logAddHealthy(_, _, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_, _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, false, false, false, health_checked_cluster);
  EXPECT_EQ(Host::Health::Healthy, cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());
}

} // namespace Upstream
} // namespace Envoy
