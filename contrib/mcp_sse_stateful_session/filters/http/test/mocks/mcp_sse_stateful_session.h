#pragma once

#include "contrib/mcp_sse_stateful_session/http/source/mcp_sse_stateful_session.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class MockSessionState : public Envoy::Extensions::Http::McpSseSessionState::McpSseSessionState {
public:
  MOCK_METHOD(absl::optional<absl::string_view>, upstreamAddress, (), (const));
  MOCK_METHOD(void, onUpdateHeader,
              (absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers));
  MOCK_METHOD(Envoy::Http::FilterDataStatus, onUpdateData,
              (absl::string_view host_address, Buffer::Instance& data, bool end_stream));
};

class MockSessionStateFactory
    : public Envoy::Extensions::Http::McpSseSessionState::McpSseSessionStateFactory {
public:
  MockSessionStateFactory();

  MOCK_METHOD(Envoy::Extensions::Http::McpSseSessionState::McpSseSessionStatePtr, create,
              (Envoy::Http::RequestHeaderMap & headers), (const));
  MOCK_METHOD(bool, isStrict, (), (const));
};

class MockSessionStateFactoryConfig
    : public Envoy::Extensions::Http::McpSseSessionState::McpSseSessionStateFactoryConfig {
public:
  MockSessionStateFactoryConfig();

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  MOCK_METHOD(Envoy::Extensions::Http::McpSseSessionState::McpSseSessionStateFactorySharedPtr,
              createSessionStateFactory,
              (const Protobuf::Message&, Server::Configuration::GenericFactoryContext&));

  std::string name() const override { return "envoy.http.mcp_sse_stateful_session.mock"; }
};

} // namespace Http
} // namespace Envoy
