#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/network/socket.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/codec_client.h"
#include "source/common/router/header_parser.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/common/health_checker_base_impl.h"

#include "src/proto/grpc/health/v1/health.pb.h"

namespace Envoy {
namespace Upstream {

class TcpHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.tcp"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::core::v3::HealthCheck::TcpHealthCheck()};
  }
};

DECLARE_FACTORY(TcpHealthCheckerFactory);

/**
 * TCP health checker implementation.
 */
class TcpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  TcpHealthCheckerImpl(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                       Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                       Random::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

private:
  struct TcpActiveHealthCheckSession;

  struct TcpSessionCallbacks : public Network::ConnectionCallbacks,
                               public Network::ReadFilterBaseImpl {
    TcpSessionCallbacks(TcpActiveHealthCheckSession& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    TcpActiveHealthCheckSession& parent_;
  };

  struct TcpActiveHealthCheckSession : public ActiveHealthCheckSession {
    TcpActiveHealthCheckSession(TcpHealthCheckerImpl& parent, const HostSharedPtr& host)
        : ActiveHealthCheckSession(parent, host), parent_(parent) {}
    ~TcpActiveHealthCheckSession() override;

    void onData(Buffer::Instance& data);
    void onEvent(Network::ConnectionEvent event);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    TcpHealthCheckerImpl& parent_;
    Network::ClientConnectionPtr client_;
    std::shared_ptr<TcpSessionCallbacks> session_callbacks_;
    // If true, stream close was initiated by us, not e.g. remote close or TCP reset.
    // In this case healthcheck status already reported, only state cleanup required.
    bool expect_close_{};
  };

  using TcpActiveHealthCheckSessionPtr = std::unique_ptr<TcpActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<TcpActiveHealthCheckSession>(*this, host);
  }
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::TCP;
  }

  const PayloadMatcher::MatchSegments send_bytes_;
  PayloadMatcher::MatchSegments receive_bytes_;
  const std::unique_ptr<envoy::config::core::v3::ProxyProtocolConfig> proxy_protocol_config_;
};

} // namespace Upstream
} // namespace Envoy
