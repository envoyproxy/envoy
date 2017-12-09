#include "common/router/header_formatter.h"

#include <cctype>
#include <string>

#include "common/access_log/access_log_formatter.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"

#include "fmt/format.h"

namespace Envoy {
namespace Router {

namespace {

// Parse list of namespace and keys into a vector. Supports backslash escaping.
std::vector<std::string> parseUpstreamMetadataParams(const std::string& field) {
  std::string field_params(field);

  std::vector<std::string> params;
  size_t pos = 0;

  // Skip leading spaces.
  while (std::isspace(field_params[pos])) {
    pos++;
  }

  size_t start = pos;
  while (pos < field_params.size()) {
    switch (field_params[pos]) {
    case ',': {
      std::string param = StringUtil::subspan(field_params, start, pos);
      StringUtil::rtrim(param);
      if (param.size() > 0) {
        params.push_back(param);
      }

      // Move to next char, skipping leading spaces.
      do {
        pos++;
      } while (std::isspace(field_params[pos]));

      start = pos;
      break;
    }

    case '\\':
      // Remove the backslash, shifting the escaped char to pos.
      field_params.erase(pos, 1);
      pos++;
      break;

    default:
      pos++;
      break;
    }
  }

  if (pos - start > 0) {
    std::string param = StringUtil::subspan(field_params, start, pos);
    StringUtil::rtrim(param);
    if (param.size() > 0) {
      params.push_back(param);
    }
  }

  return params;
}

std::function<std::string(const Envoy::AccessLog::RequestInfo&)>
parseUpstreamMetadataField(const std::string& params_str) {
  // Minimum length of valid params is "(a,b)" (5 characters).
  if (params_str.size() >= 5 && params_str[0] == '(' && params_str[params_str.size() - 1] == ')') {
    const std::vector<std::string> params =
        parseUpstreamMetadataParams(StringUtil::subspan(params_str, 1, params_str.size() - 1));

    // Minimum parameters are a metadata namespace (e.g. "envoy.lb") and a metadata key.
    if (params.size() >= 2) {
      return [params](const Envoy::AccessLog::RequestInfo& request_info) -> std::string {
        Upstream::HostDescriptionConstSharedPtr host = request_info.upstreamHost();
        if (!host) {
          return "";
        }

        const ProtobufWkt::Value* value =
            &Config::Metadata::metadataValue(host->metadata(), params[0], params[1]);
        if (value->kind_case() == ProtobufWkt::Value::KIND_NOT_SET) {
          // No kind indicates default ProtobufWkt::Value which means namespace or key not
          // found.
          return "";
        }

        size_t i = 2;
        while (i < params.size()) {
          if (!value->has_struct_value()) {
            break;
          }

          const auto field_it = value->struct_value().fields().find(params[i]);
          if (field_it == value->struct_value().fields().end()) {
            return "";
          }

          value = &field_it->second;
          i++;
        }

        if (i < params.size()) {
          // Didn't find all the keys.
          return "";
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
          return "";
        }
      };
    }
  }

  throw EnvoyException(fmt::format("Incorrect header configuration. Expected format "
                                   "UPSTREAM_METADATA(namespace, k, ...), actual format "
                                   "UPSTREAM_METADATA{}",
                                   params_str));
}

} // namespace

RequestInfoHeaderFormatter::RequestInfoHeaderFormatter(const std::string& field_name, bool append)
    : append_(append) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::AccessLog::RequestInfo& request_info) {
      return Envoy::AccessLog::AccessLogFormatUtils::protocolToString(request_info.protocol());
    };
  } else if (field_name == "CLIENT_IP") {
    field_extractor_ = [](const Envoy::AccessLog::RequestInfo& request_info) {
      return request_info.getDownstreamAddress();
    };
  } else if (StringUtil::startsWith(field_name.c_str(), "UPSTREAM_METADATA")) {
    field_extractor_ = parseUpstreamMetadataField(field_name.substr(17));
  } else {
    throw EnvoyException(fmt::format("field '{}' not supported as custom header", field_name));
  }
}

const std::string
RequestInfoHeaderFormatter::format(const Envoy::AccessLog::RequestInfo& request_info) const {
  return field_extractor_(request_info);
}

} // namespace Router
} // namespace Envoy
