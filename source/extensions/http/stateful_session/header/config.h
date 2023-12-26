#pragma once

#include "envoy/extensions/http/stateful_session/header/v3/header.pb.validate.h"

#include "source/extensions/http/stateful_session/header/header.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

class HeaderBasedSessionStateFactoryConfig : public Envoy::Http::SessionStateFactoryConfig {
public:
  Envoy::Http::SessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::stateful_session::header::v3::HeaderBasedSessionState>();
  }

  std::string name() const override { return "envoy.http.stateful_session.header"; }
};

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
