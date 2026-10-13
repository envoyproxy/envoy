#pragma once

#include "source/extensions/http/stateful_session/envelope/envelope.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Envelope {

class EnvelopeSessionStateFactoryConfig : public Envoy::Http::SessionStateFactoryConfig {
public:
  Envoy::Http::SessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<EnvelopeSessionStateProto>();
  }

  std::string name() const override { return "envoy.http.stateful_session.envelope"; }
};

} // namespace Envelope
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
