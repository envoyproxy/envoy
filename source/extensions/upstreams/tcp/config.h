#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/upstreams/tcp/v3/tcp_protocol_options.pb.h"
#include "envoy/extensions/upstreams/tcp/v3/tcp_protocol_options.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {

class ProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::upstreams::tcp::v3::TcpProtocolOptions& options);

  absl::optional<std::chrono::milliseconds> idleTimeout() const { return idle_timeout_; }

private:
  absl::optional<std::chrono::milliseconds> idle_timeout_;
};

class ProtocolOptionsConfigFactory : public Server::Configuration::ProtocolOptionsFactory {
public:
  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::upstreams::tcp::v3::TcpProtocolOptions&>(
        config, context.messageValidationVisitor());
    return std::make_shared<ProtocolOptionsConfigImpl>(typed_config);
  }
  std::string category() const override { return "envoy.upstream_options"; }
  std::string name() const override {
    return "envoy.extensions.upstreams.tcp.v3.TcpProtocolOptions";
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::tcp::v3::TcpProtocolOptions>();
  }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<envoy::extensions::upstreams::tcp::v3::TcpProtocolOptions>();
  }
};

DECLARE_FACTORY(ProtocolOptionsConfigFactory);

} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
