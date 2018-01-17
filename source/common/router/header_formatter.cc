#include "common/router/header_formatter.h"

#include <regex>
#include <string>

#include "envoy/common/optional.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/json/json_loader.h"
#include "common/request_info/utility.h"

#include "absl/strings/str_cat.h"
#include "fmt/format.h"

namespace Envoy {
namespace Router {

namespace {

std::string formatUpstreamMetadataParseException(const std::string& params,
                                                 const std::string& reason) {
  std::string formatted_reason;
  if (!reason.empty()) {
    formatted_reason = fmt::format(", because {}", reason);
  }

  return fmt::format("Incorrect header configuration. Expected format "
                     "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                     "UPSTREAM_METADATA{}{}",
                     params, formatted_reason);
}

std::string formatUpstreamMetadataParseException(const std::string& params,
                                                 const EnvoyException* cause = nullptr) {
  std::string reason;
  if (cause != nullptr) {
    reason = fmt::format("{}", cause->what());
  }

  return formatUpstreamMetadataParseException(params, reason);
}

// Parses the parameters for UPSTREAM_METADATA and returns a function suitable for accessing the
// specified metadata from an RequestInfo::RequestInfo. Expects a string formatted as:
//   (["a", "b", "c"])
// There must be at least 2 array elements (a metadata namespace and at least 1 key).
std::function<std::string(const Envoy::RequestInfo::RequestInfo&)>
parseUpstreamMetadataField(const std::string& params_str) {
  std::vector<std::string> params;
  try {
    Json::ObjectSharedPtr parsed_params =
        Json::Factory::loadFromString(StringUtil::subspan(params_str, 1, params_str.size() - 1));

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

  return [params](const Envoy::RequestInfo::RequestInfo& request_info) -> std::string {
    Upstream::HostDescriptionConstSharedPtr host = request_info.upstreamHost();
    if (!host) {
      return std::string();
    }

    const ProtobufWkt::Value* value =
        &Config::Metadata::metadataValue(host->metadata(), params[0], params[1]);
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

// Given a string staring with `(["a", "b", "c"])`, produce a function suitable for accessing the
// selected metadata and update the len reference to indicate how many characters were consumed.
std::function<std::string(const Envoy::RequestInfo::RequestInfo&)>
parseUpstreamMetadataParams(absl::string_view params, size_t& len) {
  // Use a regex to determine where the params end. Without encoding JSON string rules for the
  // parameters it's very difficult to distinguish the sequence ")%" within the params from the
  // sequence ")%" at the end of the params (especially given that another %-expression may
  // immediately follow).
  static std::string string_match = R"EOF("(?:[^"\\[:cntrl:]]|\\["\\bfnrtu])*")EOF";
  static std::regex params_regex(
      R"EOF(^\(\s*\[\s*)EOF" + string_match +
      R"EOF(\s*(?:,\s*)EOF" + string_match +
      R"EOF(\s*)*\]\s*\))EOF");

  std::string params_str = absl::StrCat(params);

  if (params_str.empty() || params_str[0] == '%') {
    throw EnvoyException(formatUpstreamMetadataParseException(""));
  }

  if (params_str.substr(0, 2) == "()") {
    throw EnvoyException(formatUpstreamMetadataParseException("()"));
  }

  std::smatch params_match;
  if (!std::regex_search(params_str, params_match, params_regex)) {
    throw EnvoyException(
        formatUpstreamMetadataParseException(params_str, "JSON supplied is not valid."));
  }

  len = params_match.length(0);
  if (len >= params.size() || params[len] != '%') {
    // No trailing %
    len = std::string::npos;
    return nullptr;
  }

  return parseUpstreamMetadataField(params_match.str(0));
}

} // namespace

#define CLIENT_IP_VAR "%CLIENT_IP%"
#define DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_VAR "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
#define PROTOCOL_VAR "%PROTOCOL%"
#define UPSTREAM_METADATA_VAR_PREFIX "%UPSTREAM_METADATA"

HeaderFormatterPtr RequestInfoHeaderFormatter::parse(absl::string_view var, bool append,
                                                     size_t& len) {
  if (var.size() < 2 || var[0] != '%') {
    len = std::string::npos;
    return nullptr;
  }

  if (var.compare(0, sizeof(PROTOCOL_VAR) - 1, PROTOCOL_VAR) == 0) {
    len = sizeof(PROTOCOL_VAR) - 1;
    return HeaderFormatterPtr{new RequestInfoHeaderFormatter(
        [](const Envoy::RequestInfo::RequestInfo& request_info) {
          return Envoy::AccessLog::AccessLogFormatUtils::protocolToString(request_info.protocol());
        },
        append)};
  }

  if (var.compare(0, sizeof(CLIENT_IP_VAR) - 1, CLIENT_IP_VAR) == 0) {
    // DEPRECATED: "CLIENT_IP" will be removed post 1.6.0.
    len = sizeof(CLIENT_IP_VAR) - 1;
    return HeaderFormatterPtr{new RequestInfoHeaderFormatter(
        [](const Envoy::RequestInfo::RequestInfo& request_info) {
          return RequestInfo::Utility::formatDownstreamAddressNoPort(
              *request_info.downstreamRemoteAddress());
        },
        append)};
  }

  if (var.compare(0, sizeof(DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_VAR) - 1,
                  DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_VAR) == 0) {
    len = sizeof(DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_VAR) - 1;
    return HeaderFormatterPtr{new RequestInfoHeaderFormatter(
        [](const Envoy::RequestInfo::RequestInfo& request_info) {
          return RequestInfo::Utility::formatDownstreamAddressNoPort(
              *request_info.downstreamRemoteAddress());
        },
        append)};
  }

  if (var.compare(0, sizeof(UPSTREAM_METADATA_VAR_PREFIX) - 1, UPSTREAM_METADATA_VAR_PREFIX) == 0) {
    auto extractor =
        parseUpstreamMetadataParams(var.substr(sizeof(UPSTREAM_METADATA_VAR_PREFIX) - 1), len);
    if (len != std::string::npos) {
      len += sizeof(UPSTREAM_METADATA_VAR_PREFIX);
    }
    if (extractor == nullptr) {
      return nullptr;
    }

    return HeaderFormatterPtr{new RequestInfoHeaderFormatter(extractor, append)};
  }

  // Find next %, if any.
  size_t pos = var.find("%", 1);
  if (pos == absl::string_view::npos) {
    len = std::string::npos;
  } else {
    len = pos + 1;
  }
  return nullptr;
}

const std::string
RequestInfoHeaderFormatter::format(const Envoy::RequestInfo::RequestInfo& request_info) const {
  return field_extractor_(request_info);
}

} // namespace Router
} // namespace Envoy
