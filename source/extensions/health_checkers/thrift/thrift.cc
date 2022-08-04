#include "source/extensions/health_checkers/thrift/thrift.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

ThriftHealthChecker::ThriftHealthChecker(
    const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    const envoy::extensions::health_checkers::thrift::v3::Thrift& thrift_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
    Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, api.randomGenerator(),
                            std::move(event_logger)),
      method_name_(thrift_config.method_name()) {}

ThriftHealthChecker::ThriftActiveHealthCheckSession::ThriftActiveHealthCheckSession(
    ThriftHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {}

ThriftHealthChecker::ThriftActiveHealthCheckSession::~ThriftActiveHealthCheckSession() {}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onDeferredDelete() {}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onInterval() {}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onTimeout() {}

// Network::ConnectionCallbacks
void ThriftHealthChecker::ThriftActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // Report Failure
  }
}

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
