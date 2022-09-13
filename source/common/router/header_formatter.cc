#include "source/common/router/header_formatter.h"

#include <string>

#include "envoy/router/string_accessor.h"

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/stream_info/utility.h"

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

// Parses a substitution format field and returns a function that formats it.
std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>
parseSubstitutionFormatField(absl::string_view field_name,
                             StreamInfoHeaderFormatter::FormatterPtrMap& formatter_map) {
  const std::string pattern = fmt::format("%{}%", field_name);
  if (formatter_map.find(pattern) == formatter_map.end()) {
    formatter_map.emplace(
        std::make_pair(pattern, Formatter::FormatterPtr(new Formatter::FormatterImpl(
                                    pattern, /*omit_empty_values=*/true))));
  }
  return [&formatter_map, pattern](const Envoy::StreamInfo::StreamInfo& stream_info) {
    const auto& formatter = formatter_map.at(pattern);
    return formatter->format(*Http::StaticEmptyHeaders::get().request_headers,
                             *Http::StaticEmptyHeaders::get().response_headers,
                             *Http::StaticEmptyHeaders::get().response_trailers, stream_info,
                             absl::string_view());
  };
}

// Parses the parameters for UPSTREAM_METADATA and returns a function suitable for accessing the
// specified metadata from an StreamInfo::StreamInfo. Expects a string formatted as:
//   (["a", "b", "c"])
// There must be at least 2 array elements (a metadata namespace and at least 1 key).
std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>
parseMetadataField(absl::string_view params_str, bool upstream = true) {
  params_str = StringUtil::trim(params_str);
  if (params_str.empty() || params_str.front() != '(' || params_str.back() != ')') {
    throw EnvoyException(formatUpstreamMetadataParseException(params_str));
  }

  absl::string_view json = params_str.substr(1, params_str.size() - 2); // trim parens

  std::vector<std::string> params;
  TRY_ASSERT_MAIN_THREAD {
    Json::ObjectSharedPtr parsed_params = Json::Factory::loadFromString(std::string(json));

    // The given json string may be an invalid object.
    if (!parsed_params) {
      throw EnvoyException(formatUpstreamMetadataParseException(json));
    }

    for (const auto& param : parsed_params->asObjectArray()) {
      params.emplace_back(param->asString());
    }
  }
  END_TRY
  catch (Json::Exception& e) {
    throw EnvoyException(formatUpstreamMetadataParseException(params_str, &e));
  }

  // Minimum parameters are a metadata namespace (e.g. "envoy.lb") and a metadata key.
  if (params.size() < 2) {
    throw EnvoyException(formatUpstreamMetadataParseException(params_str));
  }

  return [upstream, params](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
    const envoy::config::core::v3::Metadata* metadata = nullptr;
    if (upstream && stream_info.upstreamInfo()) {
      Upstream::HostDescriptionConstSharedPtr host = stream_info.upstreamInfo()->upstreamHost();
      if (!host) {
        return std::string();
      }
      metadata = host->metadata().get();
    } else {
      metadata = &(stream_info.dynamicMetadata());
    }

    const ProtobufWkt::Value* value =
        &::Envoy::Config::Metadata::metadataValue(metadata, params[0], params[1]);
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

    auto typed_state = filter_state.getDataReadOnly<StringAccessor>(param);

    // Value exists but isn't string accessible is a contract violation; throw an error.
    if (typed_state == nullptr) {
      ENVOY_LOG_MISC(debug,
                     "Invalid header information: PER_REQUEST_STATE value \"{}\" "
                     "exists but is not string accessible",
                     param);
      return std::string();
    }

    return std::string(typed_state->asString());
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
      const auto entry = request_headers->get(header_name);
      if (!entry.empty()) {
        // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially use all header
        // values.
        return std::string(entry[0]->value().getStringView());
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
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return std::string();
    }

    return string_extractor(*stream_info.downstreamAddressProvider().sslConnection());
  };
}

} // namespace

StreamInfoHeaderFormatter::StreamInfoHeaderFormatter(absl::string_view field_name) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return Envoy::Formatter::SubstitutionFormatUtils::protocolToStringOrDefault(
          stream_info.protocol());
    };
  } else if (field_name == "REQUESTED_SERVER_NAME") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) -> std::string {
      return std::string(stream_info.downstreamAddressProvider().requestedServerName());
    };
  } else if (field_name == "VIRTUAL_CLUSTER_NAME") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      return stream_info.virtualClusterName().value_or("");
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamAddressProvider().remoteAddress()->asString();
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamAddressProvider().remoteAddress());
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressJustPort(
          *stream_info.downstreamAddressProvider().remoteAddress());
    };
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_ADDRESS") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamAddressProvider().directRemoteAddress()->asString();
    };
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamAddressProvider().directRemoteAddress());
    };
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressJustPort(
          *stream_info.downstreamAddressProvider().directRemoteAddress());
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamAddressProvider().localAddress()->asString();
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamAddressProvider().localAddress());
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressJustPort(
          *stream_info.downstreamAddressProvider().localAddress());
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
  } else if (absl::StartsWith(field_name, "DOWNSTREAM_PEER_CERT_V_START")) {
    field_extractor_ = parseSubstitutionFormatField(field_name, formatter_map_);
  } else if (absl::StartsWith(field_name, "DOWNSTREAM_PEER_CERT_V_END")) {
    field_extractor_ = parseSubstitutionFormatField(field_name, formatter_map_);
  } else if (field_name == "UPSTREAM_LOCAL_ADDRESS") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.correct_remote_address")) {
        if (stream_info.upstreamInfo().has_value() &&
            stream_info.upstreamInfo()->upstreamHost()->address()) {
          return stream_info.upstreamInfo()->upstreamHost()->address()->asString();
        }
        return "";
      }
      if (stream_info.upstreamInfo().has_value() &&
          stream_info.upstreamInfo()->upstreamLocalAddress()) {
        return stream_info.upstreamInfo()->upstreamLocalAddress()->asString();
      }
      return "";
    };
  } else if (field_name == "UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.correct_remote_address")) {
        if (stream_info.upstreamInfo().has_value() &&
            stream_info.upstreamInfo()->upstreamHost()->address()) {
          return StreamInfo::Utility::formatDownstreamAddressNoPort(
              *stream_info.upstreamInfo()->upstreamHost()->address());
        }
        return "";
      }
      if (stream_info.upstreamInfo().has_value() &&
          stream_info.upstreamInfo()->upstreamLocalAddress()) {
        return StreamInfo::Utility::formatDownstreamAddressNoPort(
            *stream_info.upstreamInfo()->upstreamLocalAddress());
      }
      return "";
    };
  } else if (field_name == "UPSTREAM_LOCAL_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.correct_remote_address")) {
        if (stream_info.upstreamInfo().has_value() &&
            stream_info.upstreamInfo()->upstreamHost()->address()) {
          return StreamInfo::Utility::formatDownstreamAddressJustPort(
              *stream_info.upstreamInfo()->upstreamHost()->address());
        }
        return "";
      }
      if (stream_info.upstreamInfo().has_value() &&
          stream_info.upstreamInfo()->upstreamLocalAddress()) {
        return StreamInfo::Utility::formatDownstreamAddressJustPort(
            *stream_info.upstreamInfo()->upstreamLocalAddress());
      }
      return "";
    };
  } else if (field_name == "UPSTREAM_REMOTE_ADDRESS") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamRemoteAddress()) {
        return stream_info.upstreamInfo()->upstreamRemoteAddress()->asString();
      }
      return "";
    };
  } else if (field_name == "UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamRemoteAddress()) {
        return StreamInfo::Utility::formatDownstreamAddressNoPort(
            *stream_info.upstreamInfo()->upstreamRemoteAddress());
      }
      return "";
    };
  } else if (field_name == "UPSTREAM_REMOTE_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) -> std::string {
      if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
        return StreamInfo::Utility::formatDownstreamAddressJustPort(
            *stream_info.upstreamInfo()->upstreamHost()->address());
      }
      return "";
    };
  } else if (absl::StartsWith(field_name, "START_TIME")) {
    field_extractor_ = parseSubstitutionFormatField(field_name, formatter_map_);
  } else if (absl::StartsWith(field_name, "UPSTREAM_METADATA")) {
    field_extractor_ = parseMetadataField(field_name.substr(STATIC_STRLEN("UPSTREAM_METADATA")));
  } else if (absl::StartsWith(field_name, "DYNAMIC_METADATA")) {
    field_extractor_ =
        parseMetadataField(field_name.substr(STATIC_STRLEN("DYNAMIC_METADATA")), false);
  } else if (absl::StartsWith(field_name, "PER_REQUEST_STATE")) {
    field_extractor_ =
        parsePerRequestStateField(field_name.substr(STATIC_STRLEN("PER_REQUEST_STATE")));
  } else if (absl::StartsWith(field_name, "REQ")) {
    field_extractor_ = parseRequestHeader(field_name.substr(STATIC_STRLEN("REQ")));
  } else if (field_name == "HOSTNAME") {
    std::string hostname = Envoy::Formatter::SubstitutionFormatUtils::getHostnameOrDefault();
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
