#pragma once

#include "envoy/http/stateful_session.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class MockSessionState : public SessionState {
public:
  MOCK_METHOD(absl::optional<absl::string_view>, upstreamAddress, (), (const));
  MOCK_METHOD(void, onUpdate,
              (const Upstream::HostDescription& host, Http::ResponseHeaderMap& headers));
};

class MockSessionStateFactory : public Http::SessionStateFactory {
public:
  MockSessionStateFactory();

  MOCK_METHOD(Http::SessionStatePtr, create, (const Http::RequestHeaderMap& headers), (const));
  MOCK_METHOD(bool, isStrict, (), (const));
};

class MockSessionStateFactoryConfig : public Http::SessionStateFactoryConfig {
public:
  MockSessionStateFactoryConfig();

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  MOCK_METHOD(SessionStateFactorySharedPtr, createSessionStateFactory,
              (const Protobuf::Message&, Server::Configuration::GenericFactoryContext&));

  std::string name() const override { return "envoy.http.stateful_session.mock"; }
};

} // namespace Http
} // namespace Envoy
