#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/http/sse_stateful_session.h"
#include "envoy/server/factory_context.h"

#include "contrib/mcp_sse_stateful_session/http/source/envelope.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace SseSessionState {
namespace Envelope {

class EnvelopeSessionStateFactoryConfig : public Envoy::Http::SseSessionStateFactoryConfig {
public:
  Envoy::Http::SseSessionStateFactorySharedPtr
  createSseSessionStateFactory(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::sse_stateful_session::envelope::v3alpha::EnvelopeSessionState>();
  }

  std::string name() const override { return "envoy.http.sse_stateful_session.envelope"; }
};

} // namespace Envelope
} // namespace SseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
