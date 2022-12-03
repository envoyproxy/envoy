#include "source/extensions/health_checkers/thrift/thrift.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

namespace {

// Helper functions to get the correct hostname for an L7 health check.
const std::string& getHostname(const Upstream::HostSharedPtr& host,
                               const Upstream::ClusterInfoConstSharedPtr& cluster) {
  return host->hostnameForHealthChecks().empty() ? cluster->name()
                                                 : host->hostnameForHealthChecks();
}

} // namespace

ThriftHealthChecker::ThriftHealthChecker(
    const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    const envoy::extensions::health_checkers::thrift::v3::Thrift& thrift_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
    Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api,
    ClientFactory& client_factory)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, api.randomGenerator(),
                            std::move(event_logger)),
      method_name_(thrift_config.method_name()),
      transport_(ProtoUtils::getTransportType(thrift_config.transport())),
      protocol_(ProtoUtils::getProtocolType(thrift_config.protocol())),
      client_factory_(client_factory) {
  if (transport_ == TransportType::Auto || protocol_ == ProtocolType::Auto ||
      protocol_ == ProtocolType::Twitter) {
    throw EnvoyException(
        fmt::format("Unsupported transport or protocol in thrift health check configuration: {}",
                    thrift_config.DebugString()));
  }
}

ThriftHealthChecker::ThriftActiveHealthCheckSession::ThriftActiveHealthCheckSession(
    ThriftHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      hostname_(getHostname(host, parent_.cluster_.info())) {
  ENVOY_LOG(trace, "ThriftActiveHealthCheckSession construct hostname={}", hostname_);
}

ThriftHealthChecker::ThriftActiveHealthCheckSession::~ThriftActiveHealthCheckSession() {
  ENVOY_LOG(trace, "ThriftActiveHealthCheckSession destruct");
  ASSERT(client_ == nullptr);
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    expect_close_ = true;
    client_->close();
  }
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onInterval() {
  ENVOY_LOG(trace, "ThriftActiveHealthCheckSession onInterval");
  if (!client_) {
    ENVOY_LOG(trace, "ThriftActiveHealthCheckSession construct client");
    client_ = parent_.client_factory_.create(
        *this, parent_.transport_, parent_.protocol_, parent_.method_name_, host_,
        /* health checker seq id */ 0, /* fixed_seq_id */ true);
    client_->start();
    expect_close_ = false;
  }

  client_->sendRequest();
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onTimeout() {
  if (client_) {
    expect_close_ = true;
    client_->close();
  }
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onResponseResult(bool is_success) {
  if (is_success) {
    handleSuccess();
  } else {
    // TODO(kuochunghsu): We might want to define retriable response.
    handleFailure(envoy::data::core::v3::ACTIVE, /* retriable */ false);
  }

  if (client_ && !parent_.reuse_connection_) {
    expect_close_ = true;
    client_->close();
  }
}

Upstream::Host::CreateConnectionData
ThriftHealthChecker::ThriftActiveHealthCheckSession::createConnection() {
  return host_->createHealthCheckConnection(parent_.dispatcher_, parent_.transportSocketOptions(),
                                            parent_.transportSocketMatchMetadata().get());
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(trace, "on event close, is_local_close={} expect_close={}",
              event == Network::ConnectionEvent::LocalClose, expect_close_);
    if (!expect_close_) {
      handleFailure(envoy::data::core::v3::NETWORK);
    }

    if (client_) {
      // Report failure if the connection was closed without receiving a full response.
      parent_.dispatcher_.deferredDelete(std::move(client_));
    }
  }
}

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
