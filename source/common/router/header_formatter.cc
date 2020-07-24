#include "common/router/header_formatter.h"

#include <string>

#include "envoy/router/string_accessor.h"

#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/formatter/substitution_formatter.h"
#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"
#include "common/stream_info/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

namespace {

std::string formatUpstreamMetadataParseException(absl::string_view params,
                                                 const EnvoyException* cause = nullptr) {
  std::string reason;
  if (cause != nullptr) {
    reason = absl::StrCat(", because ", cause->what());
  }

  return absl::StrCat("Invalid header configuration. Expected format "
                      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                      "UPSTREAM_METADATA",
                      params, reason);
}

std::string formatPerRequestStateParseException(absl::string_view params) {
  return absl::StrCat("Invalid header configuration. Expected format "
                      "PER_REQUEST_STATE(<data_name>), actual format "
                      "PER_REQUEST_STATE",
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
        &::Envoy::Config::Metadata::metadataValue(host->metadata().get(), params[0], params[1]);
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
      return fmt::format("{:g}", value->number_value());

    case ProtobufWkt::Value::kStringValue:
      return value->string_value();

    case ProtobufWkt::Value::kBoolValue:
      return value->bool_value() ? "true" : "false";

    default:
      // Unsupported type or null value.
      ENVOY_LOG_MISC(debug, "unsupported value type for metadata [{}]",
                     absl::StrJoin(params, ", "));
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
  if (modified_param_str.empty()) {
    throw EnvoyException(formatPerRequestStateParseException(param_str));
  }

  std::string param(modified_param_str);
  return [param](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
    const Envoy::StreamInfo::FilterState& filter_state = stream_info.filterState();

    // No such value means don't output anything.
    if (!filter_state.hasDataWithName(param)) {
      return std::string();
    }

    // Value exists but isn't string accessible is a contract violation; throw an error.
    if (!filter_state.hasData<StringAccessor>(param)) {
      ENVOY_LOG_MISC(debug,
                     "Invalid header information: PER_REQUEST_STATE value \"{}\" "
                     "exists but is not string accessible",
                     param);
      return std::string();
    }

    return std::string(filter_state.getDataReadOnly<StringAccessor>(param).asString());
  };
}

// Parses the parameter for REQ and returns a function suitable for accessing the specified
// request header from an StreamInfo::StreamInfo. Expects a string formatted as:
//   (<header_name>)
std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>
parseRequestHeader(absl::string_view param) {
  param = StringUtil::trim(param);
  if (param.empty() || param.front() != '(') {
    throw EnvoyException(fmt::format("Invalid header configuration. Expected format "
                                     "REQ(<header-name>), actual format REQ{}",
                                     param));
  }
  ASSERT(param.back() == ')');               // Ensured by header_parser.
  param = param.substr(1, param.size() - 2); // Trim parens.
  Http::LowerCaseString header_name{std::string(param)};
  return [header_name](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
    if (const auto* request_headers = stream_info.getRequestHeaders()) {
      if (const auto* entry = request_headers->get(header_name)) {
        return std::string(entry->value().getStringView());
      }
    }
    return std::string();
  };
}

// Helper that handles the case when the ConnectionInfo is missing or if the desired value is
// empty.
StreamInfoHeaderFormatter::FieldExtractor sslConnectionInfoStringHeaderExtractor(
    std::function<std::string(const Ssl::ConnectionInfo& connection_info)> string_extractor) {
  return [string_extractor](const StreamInfo::StreamInfo& stream_info) {
    if (stream_info.downstreamSslConnection() == nullptr) {
      return std::string();
    }

    return string_extractor(*stream_info.downstreamSslConnection());
  };
}

// Helper that handles the case when the desired time field is empty.
StreamInfoHeaderFormatter::FieldExtractor sslConnectionInfoStringTimeHeaderExtractor(
    std::function<absl::optional<SystemTime>(const Ssl::ConnectionInfo& connection_info)>
        time_extractor) {
  return sslConnectionInfoStringHeaderExtractor(
      [time_extractor](const Ssl::ConnectionInfo& connection_info) {
        absl::optional<SystemTime> time = time_extractor(connection_info);
        if (!time.has_value()) {
          return std::string();
        }

        return AccessLogDateTimeFormatter::fromTime(time.value());
      });
}

} // namespace

StreamInfoHeaderFormatter::StreamInfoHeaderFormatter(absl::string_view field_name, bool append)
    : append_(append) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return Envoy::Formatter::SubstitutionFormatUtils::protocolToString(stream_info.protocol());
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamRemoteAddress()->asString();
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
  } else if (field_name == "DOWNSTREAM_LOCAL_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressJustPort(
          *stream_info.downstreamLocalAddress());
    };
  } else if (field_name == "DOWNSTREAM_PEER_URI_SAN") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_URI_SAN") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
        });
  } else if (field_name == "DOWNSTREAM_PEER_ISSUER") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.issuerPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_PEER_SUBJECT") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.subjectPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_SUBJECT") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.subjectLocalCertificate();
        });
  } else if (field_name == "DOWNSTREAM_TLS_SESSION_ID") {
    field_extractor_ = sslConnectionInfoStringHeaderExtractor(
        [](const Ssl::ConnectionInfo& connection_info) { return connection_info.sessionId(); });
  } else if (field_name == "DOWNSTREAM_TLS_CIPHER") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.ciphersuiteString();
        });
  } else if (field_name == "DOWNSTREAM_TLS_VERSION") {
    field_extractor_ = sslConnectionInfoStringHeaderExtractor(
        [](const Ssl::ConnectionInfo& connection_info) { return connection_info.tlsVersion(); });
  } else if (field_name == "DOWNSTREAM_PEER_FINGERPRINT_256") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.sha256PeerCertificateDigest();
        });
  } else if (field_name == "DOWNSTREAM_PEER_FINGERPRINT_1") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.sha1PeerCertificateDigest();
        });
  } else if (field_name == "DOWNSTREAM_PEER_SERIAL") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.serialNumberPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_PEER_CERT") {
    field_extractor_ =
        sslConnectionInfoStringHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.urlEncodedPemEncodedPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_PEER_CERT_V_START") {
    field_extractor_ =
        sslConnectionInfoStringTimeHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.validFromPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_PEER_CERT_V_END") {
    field_extractor_ =
        sslConnectionInfoStringTimeHeaderExtractor([](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.expirationPeerCertificate();
        });
  } else if (field_name == "UPSTREAM_REMOTE_ADDRESS") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (stream_info.upstreamHost()) {
        return stream_info.upstreamHost()->address()->asString();
      }
      return "";
    };
  } else if (absl::StartsWith(field_name, "START_TIME")) {
    const std::string pattern = fmt::format("%{}%", field_name);
    if (start_time_formatters_.find(pattern) == start_time_formatters_.end()) {
      start_time_formatters_.emplace(
          std::make_pair(pattern, Formatter::SubstitutionFormatParser::parse(pattern)));
    }
    field_extractor_ = [this, pattern](const Envoy::StreamInfo::StreamInfo& stream_info) {
      const auto& formatters = start_time_formatters_.at(pattern);
      std::string formatted;
      for (const auto& formatter : formatters) {
        absl::StrAppend(&formatted,
                        formatter->format(*Http::StaticEmptyHeaders::get().request_headers,
                                          *Http::StaticEmptyHeaders::get().response_headers,
                                          *Http::StaticEmptyHeaders::get().response_trailers,
                                          stream_info, absl::string_view()));
      }
      return formatted;
    };
  } else if (absl::StartsWith(field_name, "UPSTREAM_METADATA")) {
    field_extractor_ =
        parseUpstreamMetadataField(field_name.substr(STATIC_STRLEN("UPSTREAM_METADATA")));
  } else if (absl::StartsWith(field_name, "PER_REQUEST_STATE")) {
    field_extractor_ =
        parsePerRequestStateField(field_name.substr(STATIC_STRLEN("PER_REQUEST_STATE")));
  } else if (absl::StartsWith(field_name, "REQ")) {
    field_extractor_ = parseRequestHeader(field_name.substr(STATIC_STRLEN("REQ")));
  } else if (field_name == "HOSTNAME") {
    std::string hostname = Envoy::Formatter::SubstitutionFormatUtils::getHostname();
    field_extractor_ = [hostname](const StreamInfo::StreamInfo&) { return hostname; };
  } else if (field_name == "RESPONSE_FLAGS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::ResponseFlagUtils::toShortString(stream_info);
    };
  } else if (field_name == "RESPONSE_CODE_DETAILS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) -> std::string {
      if (stream_info.responseCodeDetails().has_value()) {
        return stream_info.responseCodeDetails().value();
      }
      return "";
    };
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
