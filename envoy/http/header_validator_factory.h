#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/http/header_validator.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Http {

/**
 * Extension configuration for header validators.
 */
class HeaderValidatorFactoryConfig : public Config::TypedFactory {
public:
  virtual HeaderValidatorFactoryPtr
  createFromProto(const Protobuf::Message& config,
                  Server::Configuration::ServerFactoryContext& server_context) PURE;

  std::string category() const override { return "envoy.http.header_validators"; }
};

} // namespace Http
} // namespace Envoy
