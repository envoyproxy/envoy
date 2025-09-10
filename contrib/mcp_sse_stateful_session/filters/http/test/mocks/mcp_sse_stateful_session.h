#pragma once

#include "envoy/http/sse_stateful_session.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class MockSseSessionState : public Envoy::Http::SseSessionState {
public:
  MOCK_METHOD(absl::optional<absl::string_view>, upstreamAddress, (), (const));
  MOCK_METHOD(void, onUpdateHeader,
              (absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers));
  MOCK_METHOD(Envoy::Http::FilterDataStatus, onUpdateData,
              (absl::string_view host_address, Buffer::Instance& data, bool end_stream));
  MOCK_METHOD(bool, sessionIdFound, (), (const));
  MOCK_METHOD(void, resetSessionIdFound, ());
};

class MockSseSessionStateFactory : public Envoy::Http::SseSessionStateFactory {
public:
  MockSseSessionStateFactory();

  MOCK_METHOD(Envoy::Http::SseSessionStatePtr, create, (Envoy::Http::RequestHeaderMap & headers),
              (const));
  MOCK_METHOD(bool, isStrict, (), (const));
};

class MockSseSessionStateFactoryConfig : public Envoy::Http::SseSessionStateFactoryConfig {
public:
  MockSseSessionStateFactoryConfig();

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  MOCK_METHOD(Envoy::Http::SseSessionStateFactorySharedPtr, createSseSessionStateFactory,
              (const Protobuf::Message&, Server::Configuration::GenericFactoryContext&));

  std::string name() const override { return "envoy.http.sse_stateful_session.mock"; }
};

} // namespace Http
} // namespace Envoy
