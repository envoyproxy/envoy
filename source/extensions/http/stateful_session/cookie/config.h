#pragma once

#include "envoy/extensions/http/stateful_session/cookie/v3/cookie.pb.validate.h"

#include "source/extensions/http/stateful_session/cookie/cookie.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Cookie {

class CookieBasedSessionStateFactoryConfig : public Envoy::Http::SessionStateFactoryConfig {
public:
  Envoy::Http::SessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::stateful_session::cookie::v3::CookieBasedSessionState>();
  }

  std::string name() const override { return "envoy.http.stateful_session.cookie"; }
};

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
