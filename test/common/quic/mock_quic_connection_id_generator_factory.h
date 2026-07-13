#pragma once

#include <memory>
#include <string>

#include "source/common/protobuf/protobuf.h"
#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

#include "test/common/config/dummy_config.pb.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Quic {

class MockEnvoyQuicConnectionIdGeneratorFactory : public EnvoyQuicConnectionIdGeneratorFactory {
public:
  MOCK_METHOD(QuicConnectionIdGeneratorPtr, createQuicConnectionIdGenerator,
              (uint32_t worker_index), (override));
  MOCK_METHOD((absl::StatusOr<Network::Socket::OptionConstSharedPtr>),
              createCompatibleLinuxBpfSocketOption, (uint32_t concurrency), (override));
  MOCK_METHOD(QuicConnectionIdWorkerSelector, getCompatibleConnectionIdWorkerSelector,
              (uint32_t concurrency), (override));
};

class MockEnvoyQuicConnectionIdGeneratorContext : public EnvoyQuicConnectionIdGeneratorContext {
public:
  MOCK_METHOD(EnvoyQuicConnectionIdGeneratorFactoryPtr, createQuicConnectionIdGeneratorFactory, (),
              (override));
};

class MockEnvoyQuicConnectionIdGeneratorConfigFactory
    : public EnvoyQuicConnectionIdGeneratorConfigFactory {
public:
  std::string name() const override { return "envoy.quic.mock_connection_id_generator"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::common::config::DummyConfig>();
  }

  MOCK_METHOD(EnvoyQuicConnectionIdGeneratorContextPtr, createQuicConnectionIdGeneratorContext,
              (const Protobuf::Message&, ProtobufMessage::ValidationVisitor&,
               Server::Configuration::FactoryContext&),
              (override));
};

} // namespace Quic
} // namespace Envoy
