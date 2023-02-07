#pragma once

#include "test/integration/testing_server_preferred_address_config.pb.h"
#include "test/integration/testing_server_preferred_address_config.pb.validate.h"

namespace Envoy {

class TestingServerPreferredAddressConfig : public Quic::EnvoyQuicServerPreferredAddressConfig {
public:
  TestingServerPreferredAddressConfig(const quic::QuicIpAddress& ipv4,
                                      const quic::QuicIpAddress& ipv6)
      : ip_v4_(ipv4), ip_v6_(ipv6) {}

  std::pair<quic::QuicSocketAddress, quic::QuicSocketAddress> getServerPreferredAddresses(
      const Network::Address::InstanceConstSharedPtr& local_address) override {
    int32_t port = local_address->ip()->port();
    return {quic::QuicSocketAddress(ip_v4_, port), quic::QuicSocketAddress(ip_v6_, port)};
  }

private:
  quic::QuicIpAddress ip_v4_;
  quic::QuicIpAddress ip_v6_;
};

class TestingServerPreferredAddressConfigFactory
    : public Quic::EnvoyQuicServerPreferredAddressConfigFactory {
public:
  std::string name() const override { return "quic.server_preferred_address.test"; }

  Quic::EnvoyQuicServerPreferredAddressConfigPtr createServerPreferredAddressConfig(
      const Protobuf::Message& message,
      ProtobufMessage::ValidationVisitor& validation_visitor) override {
    auto& config = MessageUtil::downcastAndValidate<
        const test::integration::TestingServerPreferredAddressConfig&>(message, validation_visitor);
    quic::QuicIpAddress ip_v4, ip_v6;
    const std::string err("bad server preferred address");
    if (!config.ipv4_address().empty()) {
      if (!ip_v4.FromString(config.ipv4_address())) {
        ProtoExceptionUtil::throwProtoValidationException(err, message);
      }
    }
    if (!config.ipv6_address().empty()) {
      if (!ip_v6.FromString(config.ipv6_address())) {
        ProtoExceptionUtil::throwProtoValidationException(err, message);
      }
    }
    return std::make_unique<TestingServerPreferredAddressConfig>(ip_v4, ip_v6);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new test::integration::TestingServerPreferredAddressConfig()};
  }
};

} // namespace Envoy
