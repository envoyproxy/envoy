#pragma once

#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_lifecycle_info.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseTunnelUpstreamLifecycleFilter : public Network::ReadFilter,
                                             public Network::ConnectionCallbacks,
                                             public Logger::Loggable<Logger::Id::filter> {
public:
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};
  absl::optional<ReverseTunnelLifecycleInfo> lifecycle_;
  int fd_{-1};
  bool keepalive_timeout_logged_{false};
};

class ReverseTunnelUpstreamLifecycleConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::UpstreamFactoryContext&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.filters.upstream_network.reverse_tunnel_lifecycle";
  }
  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return false;
  }
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
