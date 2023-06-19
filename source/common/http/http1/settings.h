#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/codec.h"
#include "envoy/protobuf/message_validator.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * @return Http1Settings An Http1Settings populated from the
 * envoy::config::core::v3::Http1ProtocolOptions config.
 */
Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 ProtobufMessage::ValidationVisitor& validation_visitor);

Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 ProtobufMessage::ValidationVisitor& validation_visitor,
                                 const ProtobufWkt::BoolValue& hcm_stream_error,
                                 bool validate_scheme);

} // namespace Http1
} // namespace Http
} // namespace Envoy
