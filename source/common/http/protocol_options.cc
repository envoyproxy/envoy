#include "source/common/http/protocol_options.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/http/exception.h"
#include "source/common/http/http_option_limits.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/str_join.h"
#include "quiche/http2/adapter/http2_protocol.h"

namespace Envoy {
namespace Http2 {
namespace ProtocolOptions {

using OptionsLimits = Utility::OptionsLimits;

namespace {

struct SettingsEntry {
  uint16_t settings_id;
  uint32_t value;
};

struct SettingsEntryHash {
  size_t operator()(const SettingsEntry& entry) const {
    return absl::Hash<decltype(entry.settings_id)>()(entry.settings_id);
  }
};

struct SettingsEntryEquals {
  bool operator()(const SettingsEntry& lhs, const SettingsEntry& rhs) const {
    return lhs.settings_id == rhs.settings_id;
  }
};

absl::Status
validateCustomSettingsParameters(const envoy::config::core::v3::Http2ProtocolOptions& options) {
  std::vector<std::string> parameter_collisions, custom_parameter_collisions;
  absl::node_hash_set<SettingsEntry, SettingsEntryHash, SettingsEntryEquals> custom_parameters;
  // User defined and named parameters with the same SETTINGS identifier can not both be set.
  for (const auto& it : options.custom_settings_parameters()) {
    ASSERT(it.identifier().value() <= std::numeric_limits<uint16_t>::max());
    // Check for custom parameter inconsistencies.
    const auto result = custom_parameters.insert(
        {static_cast<uint16_t>(it.identifier().value()), it.value().value()});
    if (!result.second) {
      if (result.first->value != it.value().value()) {
        custom_parameter_collisions.push_back(
            absl::StrCat("0x", absl::Hex(it.identifier().value(), absl::kZeroPad2)));
        // Fall through to allow unbatched exceptions to throw first.
      }
    }
    switch (it.identifier().value()) {
    case http2::adapter::ENABLE_PUSH:
      if (it.value().value() == 1) {
        return absl::InvalidArgumentError(
            "server push is not supported by Envoy and can not be enabled via a "
            "SETTINGS parameter.");
      }
      break;
    case http2::adapter::ENABLE_CONNECT_PROTOCOL:
      // An exception is made for `allow_connect` which can't be checked for presence due to the
      // use of a primitive type (bool).
      return absl::InvalidArgumentError(
          "the \"allow_connect\" SETTINGS parameter must only be configured "
          "through the named field");
    case http2::adapter::HEADER_TABLE_SIZE:
      if (options.has_hpack_table_size()) {
        parameter_collisions.push_back("hpack_table_size");
      }
      break;
    case http2::adapter::MAX_CONCURRENT_STREAMS:
      if (options.has_max_concurrent_streams()) {
        parameter_collisions.push_back("max_concurrent_streams");
      }
      break;
    case http2::adapter::INITIAL_WINDOW_SIZE:
      if (options.has_initial_stream_window_size()) {
        parameter_collisions.push_back("initial_stream_window_size");
      }
      break;
    default:
      // Ignore unknown parameters.
      break;
    }
  }

  if (!custom_parameter_collisions.empty()) {
    return absl::InvalidArgumentError(fmt::format(
        "inconsistent HTTP/2 custom SETTINGS parameter(s) detected; identifiers = {{{}}}",
        absl::StrJoin(custom_parameter_collisions, ",")));
  }
  if (!parameter_collisions.empty()) {
    return absl::InvalidArgumentError(fmt::format(
        "the {{{}}} HTTP/2 SETTINGS parameter(s) can not be configured through both named and "
        "custom parameters",
        absl::StrJoin(parameter_collisions, ",")));
  }
  return absl::OkStatus();
}

} // namespace

absl::StatusOr<envoy::config::core::v3::Http2ProtocolOptions>
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error) {
  auto ret = initializeAndValidateOptions(options);
  if (ret.status().ok() && !options.has_override_stream_error_on_invalid_http_message() &&
      hcm_stream_error_set) {
    ret->mutable_override_stream_error_on_invalid_http_message()->set_value(
        hcm_stream_error.value());
  }
  return ret;
}

absl::StatusOr<envoy::config::core::v3::Http2ProtocolOptions>
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options) {
  envoy::config::core::v3::Http2ProtocolOptions options_clone(options);
  RETURN_IF_NOT_OK(validateCustomSettingsParameters(options));

  if (!options.has_override_stream_error_on_invalid_http_message()) {
    options_clone.mutable_override_stream_error_on_invalid_http_message()->set_value(
        options.stream_error_on_invalid_http_messaging());
  }

  if (!options_clone.has_hpack_table_size()) {
    options_clone.mutable_hpack_table_size()->set_value(OptionsLimits::DEFAULT_HPACK_TABLE_SIZE);
  }
  ASSERT(options_clone.hpack_table_size().value() <= OptionsLimits::MAX_HPACK_TABLE_SIZE);
  if (!options_clone.has_max_concurrent_streams()) {
    options_clone.mutable_max_concurrent_streams()->set_value(
        OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS);
  }
  ASSERT(
      options_clone.max_concurrent_streams().value() >= OptionsLimits::MIN_MAX_CONCURRENT_STREAMS &&
      options_clone.max_concurrent_streams().value() <= OptionsLimits::MAX_MAX_CONCURRENT_STREAMS);
  if (!options_clone.has_initial_stream_window_size()) {
    options_clone.mutable_initial_stream_window_size()->set_value(
        OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  }
  ASSERT(options_clone.initial_stream_window_size().value() >=
             OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE &&
         options_clone.initial_stream_window_size().value() <=
             OptionsLimits::MAX_INITIAL_STREAM_WINDOW_SIZE);
  if (!options_clone.has_initial_connection_window_size()) {
    options_clone.mutable_initial_connection_window_size()->set_value(
        OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);
  }
  ASSERT(options_clone.initial_connection_window_size().value() >=
             OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE &&
         options_clone.initial_connection_window_size().value() <=
             OptionsLimits::MAX_INITIAL_CONNECTION_WINDOW_SIZE);
  if (!options_clone.has_max_outbound_frames()) {
    options_clone.mutable_max_outbound_frames()->set_value(
        OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES);
  }
  if (!options_clone.has_max_outbound_control_frames()) {
    options_clone.mutable_max_outbound_control_frames()->set_value(
        OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES);
  }
  if (!options_clone.has_max_consecutive_inbound_frames_with_empty_payload()) {
    options_clone.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(
        OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD);
  }
  if (!options_clone.has_max_inbound_priority_frames_per_stream()) {
    options_clone.mutable_max_inbound_priority_frames_per_stream()->set_value(
        OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM);
  }
  if (!options_clone.has_max_inbound_window_update_frames_per_data_frame_sent()) {
    options_clone.mutable_max_inbound_window_update_frames_per_data_frame_sent()->set_value(
        OptionsLimits::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT);
  }

  return options_clone;
}

} // namespace ProtocolOptions
} // namespace Http2

namespace Http3 {
namespace ProtocolOptions {

envoy::config::core::v3::Http3ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http3ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error) {
  if (options.has_override_stream_error_on_invalid_http_message()) {
    return options;
  }
  envoy::config::core::v3::Http3ProtocolOptions options_clone(options);
  if (hcm_stream_error_set) {
    options_clone.mutable_override_stream_error_on_invalid_http_message()->set_value(
        hcm_stream_error.value());
  } else {
    options_clone.mutable_override_stream_error_on_invalid_http_message()->set_value(false);
  }
  return options_clone;
}

} // namespace ProtocolOptions
} // namespace Http3
} // namespace Envoy
