#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/codec.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * @param config the protocol options to parse.
 * @param context the factory context of whatever owns the options, which also supplies the
 * validation visitor used for any extension configuration referenced by them.
 * @param creation_status is set to an error when a configured extension - today only the stateful
 * header formatter - cannot be created, and default settings are returned in that case. It is left
 * untouched on success, so it can be threaded through a chain of member initializers.
 * @return Http1Settings An Http1Settings populated from the
 * envoy::config::core::v3::Http1ProtocolOptions config.
 */
Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 Server::Configuration::GenericFactoryContext& context,
                                 absl::Status& creation_status);

Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 Server::Configuration::GenericFactoryContext& context,
                                 const Protobuf::BoolValue& hcm_stream_error, bool validate_scheme,
                                 absl::Status& creation_status);

} // namespace Http1
} // namespace Http
} // namespace Envoy
