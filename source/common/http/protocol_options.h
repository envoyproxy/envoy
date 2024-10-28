#pragma once

#include "envoy/config/core/v3/protocol.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Http2 {
namespace ProtocolOptions {

/**
 * Validates settings/options already set in |options| and initializes any remaining fields with
 * defaults.
 */
absl::StatusOr<envoy::config::core::v3::Http2ProtocolOptions>
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options);

absl::StatusOr<envoy::config::core::v3::Http2ProtocolOptions>
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error);
} // namespace ProtocolOptions
} // namespace Http2
namespace Http3 {
namespace ProtocolOptions {

envoy::config::core::v3::Http3ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http3ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error);

} // namespace ProtocolOptions
} // namespace Http3
} // namespace Envoy
