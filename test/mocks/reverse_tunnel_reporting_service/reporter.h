#pragma once

#include "envoy/extensions/bootstrap/reverse_tunnel/reverse_tunnel_reporter.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

const std::string MOCK_REPORTER = "envoy.reverse_tunnel.reporters.mock";

class MockReverseTunnelReporter : public ReverseTunnelReporter {
public:
  MOCK_METHOD(void, onServerInitialized, (), (override));
  MOCK_METHOD(void, reportConnectionEvent,
              (absl::string_view, absl::string_view, absl::string_view), (override));
  MOCK_METHOD(void, reportDisconnectionEvent, (absl::string_view, absl::string_view), (override));
};

class MockReporterFactory : public ReverseTunnelReporterFactory {
public:
  std::string name() const override { return MOCK_REPORTER; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }

  ReverseTunnelReporterPtr createReporter(Server::Configuration::ServerFactoryContext&,
                                          ProtobufTypes::MessagePtr) override {
    return createReporter();
  }

  MOCK_METHOD(ReverseTunnelReporterPtr, createReporter, ());
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
