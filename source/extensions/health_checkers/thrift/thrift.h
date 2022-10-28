#pragma once

#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"

#include "source/common/upstream/health_checker_base_impl.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/health_checkers/thrift/client.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

using namespace Envoy::Extensions::NetworkFilters;
using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;

/**
 * Thrift health checker implementation.
 */
class ThriftHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  ThriftHealthChecker(const Upstream::Cluster& cluster,
                      const envoy::config::core::v3::HealthCheck& config,
                      const envoy::extensions::health_checkers::thrift::v3::Thrift& thrift_config,
                      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                      Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api,
                      ClientFactory& client_factory);

protected:
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::THRIFT;
  }

private:
  class ThriftActiveHealthCheckSession : public ActiveHealthCheckSession, public ClientCallback {
  public:
    ThriftActiveHealthCheckSession(ThriftHealthChecker& parent,
                                   const Upstream::HostSharedPtr& host);
    ~ThriftActiveHealthCheckSession() override;

    // ClientCallback
    void onResponseResult(bool is_success) override;
    Upstream::Host::CreateConnectionData createConnection() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Network::ConnectionCallbacks in ClientCallback
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

  private:
    ThriftHealthChecker& parent_;
    const std::string& hostname_;
    ClientPtr client_;
    bool expect_close_{};
  };

  using ThriftActiveHealthCheckSessionPtr = std::unique_ptr<ThriftActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<ThriftActiveHealthCheckSession>(*this, host);
  }

  const std::string method_name_;
  const TransportType transport_;
  const ProtocolType protocol_;
  ClientFactory& client_factory_;
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
