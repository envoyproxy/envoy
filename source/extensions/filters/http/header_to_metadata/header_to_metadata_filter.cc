#include "source/extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"

#include "source/common/common/base64.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

HeaderToMetadataFilter::HeaderToMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

HeaderToMetadataFilter::~HeaderToMetadataFilter() = default;

Http::FilterHeadersStatus HeaderToMetadataFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                bool) {
  const auto* config = getConfig();
  if (config->doRequest()) {
    writeHeaderToMetadata(headers, config->requestRules(), *decoder_callbacks_,
                          HeaderDirection::Request);
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
    writeHeaderToMetadata(headers, config->responseRules(), *encoder_callbacks_,
                          HeaderDirection::Response);
  }
  return Http::FilterHeadersStatus::Continue;
}

void HeaderToMetadataFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool HeaderToMetadataFilter::addMetadata(StructMap& struct_map, const std::string& meta_namespace,
                                         const std::string& key, std::string value, ValueType type,
                                         ValueEncode encode, HeaderDirection direction) const {
  Protobuf::Value val;
  const auto* config = getConfig();

  ASSERT(!value.empty());

  if (value.size() >= MAX_HEADER_VALUE_LEN) {
    // Too long, go away.
    ENVOY_LOG(debug, "metadata value is too long");
    config->chargeStat(StatsEvent::HeaderValueTooLong, direction);
    return false;
  }

  if (encode == envoy::extensions::filters::http::header_to_metadata::v3::Config::BASE64) {
    value = Base64::decodeWithoutPadding(value);
    if (value.empty()) {
      ENVOY_LOG(debug, "Base64 decode failed");
      config->chargeStat(StatsEvent::Base64DecodeFailed, direction);
      return false;
    }
  }

  // Sane enough, add the key/value.
  switch (type) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::header_to_metadata::v3::Config::STRING:
    val.set_string_value(std::move(value));
    break;
  case envoy::extensions::filters::http::header_to_metadata::v3::Config::NUMBER: {
    double dval;
    if (absl::SimpleAtod(StringUtil::trim(value), &dval)) {
      val.set_number_value(dval);
    } else {
      ENVOY_LOG(debug, "value to number conversion failed");
      return false;
    }
    break;
  }
  case envoy::extensions::filters::http::header_to_metadata::v3::Config::PROTOBUF_VALUE: {
    if (!val.ParseFromString(value)) {
      ENVOY_LOG(debug, "parse from decoded string failed");
      return false;
    }
    break;
  }
  }

  auto& keyval = struct_map[meta_namespace];
  (*keyval.mutable_fields())[key] = std::move(val);

  // Increment metadata_added stat if stats are enabled.
  config->chargeStat(StatsEvent::MetadataAdded, direction);

  return true;
}

const std::string& HeaderToMetadataFilter::decideNamespace(const std::string& nspace) const {
  return nspace.empty() ? HttpFilterNames::get().HeaderToMetadata : nspace;
}

// add metadata['key']= value depending on header present or missing case
void HeaderToMetadataFilter::applyKeyValue(std::string&& value, const Rule& rule,
                                           const KeyValuePair& keyval, StructMap& np,
                                           HeaderDirection direction) {
  const auto* config = getConfig();

  if (!keyval.value().empty()) {
    value = keyval.value();
  } else {
    if (rule.regexReplace().has_value()) {
      const bool was_non_empty = !value.empty();
      value = rule.regexReplace()->apply(value);
      // If we had a non-empty input but got an empty result from regex, it could indicate a
      // failure.
      if (was_non_empty && value.empty()) {
        config->chargeStat(StatsEvent::RegexSubstitutionFailed, direction);
      }
    }
  }
  if (!value.empty()) {
    const auto& nspace = decideNamespace(keyval.metadata_namespace());
    addMetadata(np, nspace, keyval.key(), value, keyval.type(), keyval.encode(), direction);
  } else {
    ENVOY_LOG(debug, "value is empty, not adding metadata");
  }
}

void HeaderToMetadataFilter::writeHeaderToMetadata(Http::HeaderMap& headers,
                                                   const HeaderToMetadataRules& rules,
                                                   Http::StreamFilterCallbacks& callbacks,
                                                   HeaderDirection direction) {
  StructMap structs_by_namespace;
  const auto* config = getConfig();

  for (const auto& rule : rules) {
    const auto& proto_rule = rule.rule();
    absl::optional<std::string> value = rule.selector_->extract(headers);

    // Increment rules_processed stat if stats are enabled.
    config->chargeStat(StatsEvent::RulesProcessed, direction);

    if (value && proto_rule.has_on_header_present()) {
      applyKeyValue(std::move(value).value_or(""), rule, proto_rule.on_header_present(),
                    structs_by_namespace, direction);
    } else if (!value && proto_rule.has_on_header_missing()) {
      // Increment header_not_found stat if stats are enabled.
      config->chargeStat(StatsEvent::HeaderNotFound, direction);
      applyKeyValue(std::move(value).value_or(""), rule, proto_rule.on_header_missing(),
                    structs_by_namespace, direction);
    }
  }
  // Any matching rules?
  if (!structs_by_namespace.empty()) {
    for (auto const& entry : structs_by_namespace) {
      callbacks.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

// TODO(rgs1): this belongs in one of the filter interfaces, see issue #10164.
const Config* HeaderToMetadataFilter::getConfig() const {
  // Cached config pointer.
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<Config>(decoder_callbacks_);
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
