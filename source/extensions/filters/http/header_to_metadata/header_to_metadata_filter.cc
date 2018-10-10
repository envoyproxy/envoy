#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {
namespace {

const uint32_t MAX_HEADER_VALUE_LEN = 100;

} // namespace

Config::Config(const envoy::config::filter::http::header_to_metadata::v2::Config config) {
  request_set_ = Config::configToVector(config.request_rules(), request_rules_);
  response_set_ = Config::configToVector(config.response_rules(), response_rules_);

  // don't allow an empty configuration
  if (!response_set_ && !request_set_) {
    throw new EnvoyException("Must at least specify either response or request config");
  }
}

bool Config::configToVector(const ProtobufRepeatedRule& proto_rules,
                            HeaderToMetadataRules& vector) {
  if (proto_rules.size() == 0) {
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

HeaderToMetadataFilter::~HeaderToMetadataFilter() {}

Http::FilterHeadersStatus HeaderToMetadataFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (config_->doRequest()) {
    writeHeaderToMetadata(headers, config_->requestRules(), *decoder_callbacks_);
  }

  return Http::FilterHeadersStatus::Continue;
}

void HeaderToMetadataFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus HeaderToMetadataFilter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (config_->doResponse()) {
    writeHeaderToMetadata(headers, config_->responseRules(), *encoder_callbacks_);
  }
  return Http::FilterHeadersStatus::Continue;
}

void HeaderToMetadataFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool HeaderToMetadataFilter::addMetadata(StructMap& map, const std::string& meta_namespace,
                                         const std::string& key, absl::string_view value,
                                         ValueType type) const {
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

  // Sane enough, add the key/value.
  switch (type) {
  case envoy::config::filter::http::header_to_metadata::v2::Config_ValueType_STRING:
    val.set_string_value(ProtobufTypes::String(value));
    break;
  case envoy::config::filter::http::header_to_metadata::v2::Config_ValueType_NUMBER: {
    double dval;
    if (absl::SimpleAtod(StringUtil::trim(value), &dval)) {
      val.set_number_value(dval);
    } else {
      ENVOY_LOG(debug, "value to number conversion failed");
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
      absl::string_view value =
          keyval.value().empty() ? header_entry->value().getStringView() : keyval.value();

      if (!value.empty()) {
        const auto& nspace = decideNamespace(keyval.metadata_namespace());
        addMetadata(structs_by_namespace, nspace, keyval.key(), value, keyval.type());
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
        addMetadata(structs_by_namespace, nspace, keyval.key(), keyval.value(), keyval.type());
      } else {
        ENVOY_LOG(debug, "value is empty, not adding metadata");
      }
    }
  }

  // Any matching rules?
  if (structs_by_namespace.size() > 0) {
    for (auto const& entry : structs_by_namespace) {
      callbacks.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
