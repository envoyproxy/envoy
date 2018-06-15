#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

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

MetadataType Config::toType(
    const envoy::config::filter::http::header_to_metadata::v2::Config::ValueType& vtype) {
  switch (vtype) {
  case envoy::config::filter::http::header_to_metadata::v2::Config_ValueType_STRING:
    return MetadataType::String;
  case envoy::config::filter::http::header_to_metadata::v2::Config_ValueType_NUMBER:
    return MetadataType::Number;
  default:
    return MetadataType::String;
  }
}

MetadataKeyValue Config::toKeyValue(
    const envoy::config::filter::http::header_to_metadata::v2::Config::KeyValuePair& keyValPair) {
  MetadataKeyValue ret;

  ret.metadataNamespace = keyValPair.metadata_namespace();
  ret.key = keyValPair.key();
  ret.value = keyValPair.value();
  ret.type = toType(keyValPair.type());

  return ret;
}

Rule Config::toRule(
    const envoy::config::filter::http::header_to_metadata::v2::Config::Rule& entry) {
  Rule rule(entry.header());

  rule.onHeaderPresent = toKeyValue(entry.on_header_present());
  rule.onHeaderMissing = toKeyValue(entry.on_header_missing());
  rule.remove = entry.remove();

  return rule;
}

bool Config::configToVector(const ProtobufRepeatedRule& protoRules, HeaderToMetadataRules& vector) {
  if (protoRules.size() == 0) {
    ENVOY_LOG(debug, "no rules provided");
    return false;
  }

  for (const auto& entry : protoRules) {
    vector.push_back(toRule(entry));
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

bool HeaderToMetadataFilter::addMetadata(StructMap& map, const std::string& metaNamespace,
                                         const std::string& key, const std::string& value,
                                         MetadataType type) const {
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
  case MetadataType::Number:
    try {
      val.set_number_value(std::stod(value));
    } catch (...) {
      ENVOY_LOG(debug, "value to number conversion failed");
      return false;
    }
    break;
  case MetadataType::String:
    val.set_string_value(value);
    break;
  }

  // Have we seen this namespace before?
  auto namespaceIter = map.find(metaNamespace);
  if (namespaceIter == map.end()) {
    map[metaNamespace] = ProtobufWkt::Struct();
    namespaceIter = map.find(metaNamespace);
  }

  auto& keyval = namespaceIter->second;
  (*keyval.mutable_fields())[key] = val;

  return true;
}

const std::string& HeaderToMetadataFilter::decideNamespace(const std::string& nspace) const {
  return nspace.empty() ? HttpFilterNames::get().HEADER_TO_METADATA : nspace;
}

void HeaderToMetadataFilter::writeHeaderToMetadata(Http::HeaderMap& headers,
                                                   const HeaderToMetadataRules& rules,
                                                   Http::StreamFilterCallbacks& callbacks) {
  StructMap structsByNamespace;

  for (const auto& rule : rules) {
    const Http::HeaderEntry* header_entry = headers.get(rule.header);

    if (header_entry != nullptr) {
      const auto& value = rule.onHeaderPresent.value.empty()
                              ? std::string(header_entry->value().getStringView())
                              : rule.onHeaderPresent.value;
      const auto& nspace = decideNamespace(rule.onHeaderPresent.metadataNamespace);

      addMetadata(structsByNamespace, nspace, rule.onHeaderPresent.key, value,
                  rule.onHeaderPresent.type);

      if (rule.remove) {
        headers.remove(rule.header);
      }
    } else {
      // Should we add metadata if the header is missing?
      if (rule.onHeaderMissing.key.empty()) {
        continue;
      }

      const auto& nspace = decideNamespace(rule.onHeaderMissing.metadataNamespace);
      addMetadata(structsByNamespace, nspace, rule.onHeaderMissing.key, rule.onHeaderMissing.value,
                  rule.onHeaderMissing.type);
    }
  }

  // Any matching rules?
  if (structsByNamespace.size() > 0) {
    for (auto const& entry : structsByNamespace) {
      callbacks.requestInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
