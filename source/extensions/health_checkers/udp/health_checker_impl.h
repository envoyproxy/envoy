#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/extensions/health_checkers/udp/v3/udp.pb.h"
#include "envoy/extensions/health_checkers/udp/v3/udp.pb.validate.h"
#include "envoy/network/socket.h"
#include "envoy/server/health_checker_config.h"

#include "source/extensions/health_checkers/common/health_checker_base_impl.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Upstream {

class UdpHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.udp"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::health_checkers::udp::v3::UdpHealthCheck>();
  }
};

DECLARE_FACTORY(UdpHealthCheckerFactory);

class UdpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  UdpHealthCheckerImpl(
      const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
      const envoy::extensions::health_checkers::udp::v3::UdpHealthCheck& udp_config,
      Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Random::RandomGenerator& random,
      HealthCheckEventLoggerPtr&& event_logger);

protected:
  virtual Network::SocketPtr
  createSocket(const Network::Address::InstanceConstSharedPtr& address) PURE;

private:
  class ActiveSession : public ActiveHealthCheckSession {
  public:
    ActiveSession(UdpHealthCheckerImpl& parent, const HostSharedPtr& host);

  private:
    enum class DeferredAction { None, Send, NetworkFailure };

    void completeFailure();
    void completeSuccess();
    void onDeferredAction();
    absl::Status onFileEvent(uint32_t events);
    void onReadReady();
    void retireSocket();
    void scheduleAction(DeferredAction action);
    void sendRequest();

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() override;

    UdpHealthCheckerImpl& parent_;
    Event::SchedulableCallbackPtr deferred_callback_;
    Network::SocketPtr socket_;
    Network::SocketPtr retired_socket_;
    std::vector<uint8_t> receive_buffer_;
    DeferredAction deferred_action_{DeferredAction::None};
    bool attempt_active_{false};
    bool file_event_initialized_{false};
    bool request_sent_{false};
  };

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<ActiveSession>(*this, host);
  }
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::UDP;
  }

  const std::vector<uint8_t> send_bytes_;
  const std::vector<uint8_t> receive_bytes_;
};

class ProdUdpHealthCheckerImpl final : public UdpHealthCheckerImpl {
public:
  using UdpHealthCheckerImpl::UdpHealthCheckerImpl;

private:
  Network::SocketPtr createSocket(const Network::Address::InstanceConstSharedPtr& address) override;
};

} // namespace Upstream
} // namespace Envoy
