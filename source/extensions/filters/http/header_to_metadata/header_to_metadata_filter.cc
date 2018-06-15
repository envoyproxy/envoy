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

bool Config::configToVector(const ProtobufRepeatedRule& protoRules, HeaderToMetadataRules& vector) {
  if (protoRules.size() == 0) {
    ENVOY_LOG(debug, "no rules provided");
    return false;
  }

  for (const auto& entry : protoRules) {
    std::pair<Http::LowerCaseString, Rule> rule = {Http::LowerCaseString(entry.header()), entry};
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

bool HeaderToMetadataFilter::addMetadata(StructMap& map, const std::string& metaNamespace,
                                         const std::string& key, const std::string& value,
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
    val.set_string_value(value);
    break;
  case envoy::config::filter::http::header_to_metadata::v2::Config_ValueType_NUMBER:
    try {
      val.set_number_value(std::stod(value));
    } catch (...) {
      ENVOY_LOG(debug, "value to number conversion failed");
      return false;
    }
    break;
  default:
    ENVOY_LOG(debug, "unknown value type");
    return false;
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

  for (const auto& rulePair : rules) {
    const auto& header = rulePair.first;
    const auto& rule = rulePair.second;
    const Http::HeaderEntry* header_entry = headers.get(header);

    if (header_entry != nullptr) {
      const auto& keyval = rule.on_header_present();
      const auto& value = keyval.value().empty()
                              ? std::string(header_entry->value().getStringView())
                              : keyval.value();
      const auto& nspace = decideNamespace(keyval.metadata_namespace());
      addMetadata(structsByNamespace, nspace, keyval.key(), value, keyval.type());

      if (rule.remove()) {
        headers.remove(header);
      }
    } else {
      // Should we add metadata if the header is missing?

      const auto& keyval = rule.on_header_missing();

      // Key empty means keyval wasn't set.
      // TODO(rgs): is there a more idiomatic way to do this?
      if (keyval.key().empty()) {
        continue;
      }

      const auto& nspace = decideNamespace(keyval.metadata_namespace());
      addMetadata(structsByNamespace, nspace, keyval.key(), keyval.value(), keyval.type());
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
