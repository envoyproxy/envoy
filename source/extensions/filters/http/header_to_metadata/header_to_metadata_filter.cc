#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"

#include "common/common/base64.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

Config::Config(const envoy::extensions::filters::http::header_to_metadata::v3::Config config,
               const bool per_route) {
  request_set_ = Config::configToVector(config.request_rules(), request_rules_);
  response_set_ = Config::configToVector(config.response_rules(), response_rules_);

  // Note: empty configs are fine for the global config, which would be the case for enabling
  //       the filter globally without rules and then applying them at the virtual host or
  //       route level. At the virtual or route level, it makes no sense to have an empty
  //       config so we throw an error.
  if (per_route && !response_set_ && !request_set_) {
    throw EnvoyException("header_to_metadata_filter: Per filter configs must at least specify "
                         "either request or response rules");
  }
}

bool Config::configToVector(const ProtobufRepeatedRule& proto_rules,
                            HeaderToMetadataRules& vector) {
  if (proto_rules.empty()) {
    ENVOY_LOG(debug, "no rules provided");
    return false;
  }

  for (const auto& entry : proto_rules) {
    std::pair<Http::LowerCaseString, Rule> rule = {Http::LowerCaseString(entry.header()), entry};

    // Rule must have at least one of the `on_header_*` fields set.
    if (!entry.has_on_header_present() && !entry.has_on_header_missing()) {
      const auto& error = fmt::format("header to metadata filter: rule for header '{}' has neither "
                                      "`on_header_present` nor `on_header_missing` set",
                                      entry.header());
      throw EnvoyException(error);
    }

    vector.push_back(rule);
  }

  return true;
}

HeaderToMetadataFilter::HeaderToMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

HeaderToMetadataFilter::~HeaderToMetadataFilter() = default;

Http::FilterHeadersStatus HeaderToMetadataFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                bool) {
  const auto* config = getConfig();
  if (config->doRequest()) {
    writeHeaderToMetadata(headers, config->requestRules(), *decoder_callbacks_);
  }

  return Http::FilterHeadersStatus::Continue;
}

void HeaderToMetadataFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus HeaderToMetadataFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                bool) {
  const auto* config = getConfig();
  if (config->doResponse()) {
    writeHeaderToMetadata(headers, config->responseRules(), *encoder_callbacks_);
  }
  return Http::FilterHeadersStatus::Continue;
}

void HeaderToMetadataFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool HeaderToMetadataFilter::addMetadata(StructMap& map, const std::string& meta_namespace,
                                         const std::string& key, absl::string_view value,
                                         ValueType type, ValueEncode encode) const {
  ProtobufWkt::Value val;

  if (value.empty()) {
    // No value, skip. we could allow this though.
    ENVOY_LOG(debug, "no metadata value provided");
    return false;
  }

  if (value.size() >= MAX_HEADER_VALUE_LEN) {
    // Too long, go away.
    ENVOY_LOG(debug, "metadata value is too long");
    return false;
  }

  std::string decodedValue = std::string(value);
  if (encode == envoy::extensions::filters::http::header_to_metadata::v3::Config::BASE64) {
    decodedValue = Base64::decodeWithoutPadding(value);
    if (decodedValue.empty()) {
      ENVOY_LOG(debug, "Base64 decode failed");
      return false;
    }
  }

  // Sane enough, add the key/value.
  switch (type) {
  case envoy::extensions::filters::http::header_to_metadata::v3::Config::STRING:
    val.set_string_value(std::move(decodedValue));
    break;
  case envoy::extensions::filters::http::header_to_metadata::v3::Config::NUMBER: {
    double dval;
    if (absl::SimpleAtod(StringUtil::trim(decodedValue), &dval)) {
      val.set_number_value(dval);
    } else {
      ENVOY_LOG(debug, "value to number conversion failed");
      return false;
    }
    break;
  }
  case envoy::extensions::filters::http::header_to_metadata::v3::Config::PROTOBUF_VALUE: {
    if (!val.ParseFromString(decodedValue)) {
      ENVOY_LOG(debug, "parse from decoded string failed");
      return false;
    }
    break;
  }
  default:
    ENVOY_LOG(debug, "unknown value type");
    return false;
  }

  // Have we seen this namespace before?
  auto namespace_iter = map.find(meta_namespace);
  if (namespace_iter == map.end()) {
    map[meta_namespace] = ProtobufWkt::Struct();
    namespace_iter = map.find(meta_namespace);
  }

  auto& keyval = namespace_iter->second;
  (*keyval.mutable_fields())[key] = val;

  return true;
}

const std::string& HeaderToMetadataFilter::decideNamespace(const std::string& nspace) const {
  return nspace.empty() ? HttpFilterNames::get().HeaderToMetadata : nspace;
}

void HeaderToMetadataFilter::writeHeaderToMetadata(Http::HeaderMap& headers,
                                                   const HeaderToMetadataRules& rules,
                                                   Http::StreamFilterCallbacks& callbacks) {
  StructMap structs_by_namespace;

  for (const auto& rulePair : rules) {
    const auto& header = rulePair.first;
    const auto& rule = rulePair.second;
    const Http::HeaderEntry* header_entry = headers.get(header);

    if (header_entry != nullptr && rule.has_on_header_present()) {
      const auto& keyval = rule.on_header_present();
      absl::string_view value = keyval.value().empty() ? header_entry->value().getStringView()
                                                       : absl::string_view(keyval.value());

      if (!value.empty()) {
        const auto& nspace = decideNamespace(keyval.metadata_namespace());
        addMetadata(structs_by_namespace, nspace, keyval.key(), value, keyval.type(),
                    keyval.encode());
      } else {
        ENVOY_LOG(debug, "value is empty, not adding metadata");
      }

      if (rule.remove()) {
        headers.remove(header);
      }
    } else if (rule.has_on_header_missing()) {
      // Add metadata for the header missing case.
      const auto& keyval = rule.on_header_missing();

      if (!keyval.value().empty()) {
        const auto& nspace = decideNamespace(keyval.metadata_namespace());
        addMetadata(structs_by_namespace, nspace, keyval.key(), keyval.value(), keyval.type(),
                    keyval.encode());
      } else {
        ENVOY_LOG(debug, "value is empty, not adding metadata");
      }
    }
  }

  // Any matching rules?
  if (!structs_by_namespace.empty()) {
    for (auto const& entry : structs_by_namespace) {
      callbacks.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

const Config* HeaderToMetadataFilter::getRouteConfig() const {
  if (!decoder_callbacks_->route() || !decoder_callbacks_->route()->routeEntry()) {
    return nullptr;
  }

  const auto* entry = decoder_callbacks_->route()->routeEntry();
  const auto* per_filter_config =
      entry->virtualHost().perFilterConfig(HttpFilterNames::get().HeaderToMetadata);

  return dynamic_cast<const Config*>(per_filter_config);
}

// TODO(rgs1): this belongs in one of the filter interfaces, see issue #10164.
const Config* HeaderToMetadataFilter::getConfig() const {
  // Cached config pointer.
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = getRouteConfig();
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = config_.get();
  return effective_config_;
}

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
