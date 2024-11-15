#pragma once

#include "envoy/common/regex.h"

namespace Envoy {
namespace Server {

Regex::EnginePtr createRegexEngine(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                   ProtobufMessage::ValidationVisitor& validation_visitor,
                                   Configuration::ServerFactoryContext& server_factory_context);

} // namespace Server
} // namespace Envoy
