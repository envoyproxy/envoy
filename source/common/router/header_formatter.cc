#include "common/router/header_formatter.h"

#include <string>

#include "envoy/router/string_accessor.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"
#include "common/stream_info/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

namespace {

std::string formatUpstreamMetadataParseException(absl::string_view params,
                                                 const EnvoyException* cause = nullptr) {
  std::string reason;
  if (cause != nullptr) {
    reason = fmt::format(", because {}", cause->what());
  }

  return fmt::format("Invalid header configuration. Expected format "
                     "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                     "UPSTREAM_METADATA{}{}",
                     params, reason);
}

std::string formatPerRequestStateParseException(absl::string_view params) {
  return fmt::format("Invalid header configuration. Expected format "
                     "PER_REQUEST_STATE(<data_name>), actual format "
                     "PER_REQUEST_STATE{}",
                     params);
}

// Parses the parameters for UPSTREAM_METADATA and returns a function suitable for accessing the
// specified metadata from an StreamInfo::StreamInfo. Expects a string formatted as:
//   (["a", "b", "c"])
// There must be at least 2 array elements (a metadata namespace and at least 1 key).
std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>
parseUpstreamMetadataField(absl::string_view params_str) {
  params_str = StringUtil::trim(params_str);
  if (params_str.empty() || params_str.front() != '(' || params_str.back() != ')') {
    throw EnvoyException(formatUpstreamMetadataParseException(params_str));
  }

  absl::string_view json = params_str.substr(1, params_str.size() - 2); // trim parens

  std::vector<std::string> params;
  try {
    Json::ObjectSharedPtr parsed_params = Json::Factory::loadFromString(std::string(json));

    for (const auto& param : parsed_params->asObjectArray()) {
      params.emplace_back(param->asString());
    }
  } catch (Json::Exception& e) {
    throw EnvoyException(formatUpstreamMetadataParseException(params_str, &e));
  }

  // Minimum parameters are a metadata namespace (e.g. "envoy.lb") and a metadata key.
  if (params.size() < 2) {
    throw EnvoyException(formatUpstreamMetadataParseException(params_str));
  }

  return [params](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
    Upstream::HostDescriptionConstSharedPtr host = stream_info.upstreamHost();
    if (!host) {
      return std::string();
    }

    const ProtobufWkt::Value* value =
        &Config::Metadata::metadataValue(*host->metadata(), params[0], params[1]);
    if (value->kind_case() == ProtobufWkt::Value::KIND_NOT_SET) {
      // No kind indicates default ProtobufWkt::Value which means namespace or key not
      // found.
      return std::string();
    }

    size_t i = 2;
    while (i < params.size()) {
      if (!value->has_struct_value()) {
        break;
      }

      const auto field_it = value->struct_value().fields().find(params[i]);
      if (field_it == value->struct_value().fields().end()) {
        return std::string();
      }

      value = &field_it->second;
      i++;
    }

    if (i < params.size()) {
      // Didn't find all the keys.
      return std::string();
    }

    switch (value->kind_case()) {
    case ProtobufWkt::Value::kNumberValue:
      return fmt::format("{}", value->number_value());

    case ProtobufWkt::Value::kStringValue:
      return value->string_value();

    case ProtobufWkt::Value::kBoolValue:
      return value->bool_value() ? "true" : "false";

    default:
      // Unsupported type or null value.
      ENVOY_LOG_MISC(debug, "unsupported value type for metadata [{}]",
                     StringUtil::join(params, ", "));
      return std::string();
    }
  };
}

// Parses the parameters for PER_REQUEST_STATE and returns a function suitable for accessing the
// specified metadata from an StreamInfo::StreamInfo. Expects a string formatted as:
//   (<state_name>)
// The state name is expected to be in reverse DNS format, though this is not enforced by
// this function.
std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>
parsePerRequestStateField(absl::string_view param_str) {
  absl::string_view modified_param_str = StringUtil::trim(param_str);
  if (modified_param_str.empty() || modified_param_str.front() != '(' ||
      modified_param_str.back() != ')') {
    throw EnvoyException(formatPerRequestStateParseException(param_str));
  }
  modified_param_str = modified_param_str.substr(1, modified_param_str.size() - 2); // trim parens
  if (modified_param_str.size() == 0) {
    throw EnvoyException(formatPerRequestStateParseException(param_str));
  }

  std::string param(modified_param_str);
  return [param](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
    const Envoy::StreamInfo::FilterState& per_request_state = stream_info.perRequestState();

    // No such value means don't output anything.
    if (!per_request_state.hasDataWithName(param)) {
      return std::string();
    }

    // Value exists but isn't string accessible is a contract violation; throw an error.
    if (!per_request_state.hasData<StringAccessor>(param)) {
      ENVOY_LOG_MISC(debug,
                     "Invalid header information: PER_REQUEST_STATE value \"{}\" "
                     "exists but is not string accessible",
                     param);
      return std::string();
    }

    return std::string(per_request_state.getData<StringAccessor>(param).asString());
  };
}

} // namespace

StreamInfoHeaderFormatter::StreamInfoHeaderFormatter(absl::string_view field_name, bool append)
    : append_(append) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return Envoy::AccessLog::AccessLogFormatUtils::protocolToString(stream_info.protocol());
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamRemoteAddress());
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamLocalAddress()->asString();
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamLocalAddress());
    };
  } else if (field_name.find("START_TIME") == 0) {
    const std::string pattern = fmt::format("%{}%", field_name);
    if (start_time_formatters_.find(pattern) == start_time_formatters_.end()) {
      start_time_formatters_.emplace(
          std::make_pair(pattern, AccessLog::AccessLogFormatParser::parse(pattern)));
    }
    field_extractor_ = [this, pattern](const Envoy::StreamInfo::StreamInfo& stream_info) {
      const auto& formatters = start_time_formatters_.at(pattern);
      Http::HeaderMapImpl empty_map;
      std::string formatted;
      for (const auto& formatter : formatters) {
        absl::StrAppend(&formatted,
                        formatter->format(empty_map, empty_map, empty_map, stream_info));
      }
      return formatted;
    };
  } else if (field_name.find("UPSTREAM_METADATA") == 0) {
    field_extractor_ =
        parseUpstreamMetadataField(field_name.substr(STATIC_STRLEN("UPSTREAM_METADATA")));
  } else if (field_name.find("PER_REQUEST_STATE") == 0) {
    field_extractor_ =
        parsePerRequestStateField(field_name.substr(STATIC_STRLEN("PER_REQUEST_STATE")));
  } else {
    throw EnvoyException(fmt::format("field '{}' not supported as custom header", field_name));
  }
}

const std::string
StreamInfoHeaderFormatter::format(const Envoy::StreamInfo::StreamInfo& stream_info) const {
  return field_extractor_(stream_info);
}

} // namespace Router
} // namespace Envoy
