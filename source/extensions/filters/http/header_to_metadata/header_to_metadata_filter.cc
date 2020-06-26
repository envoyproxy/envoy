#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"

#include "common/common/base64.h"
#include "common/common/regex.h"
#include "common/config/well_known_names.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

Rule::Rule(const std::string& header, const ProtoRule& rule) : header_(header), rule_(rule) {
  if (rule.on_header_present().has_regex_value_rewrite()) {
    const auto& rewrite_spec = rule.on_header_present().regex_value_rewrite();
    regex_rewrite_ = Regex::Utility::parseRegex(rewrite_spec.pattern());
    regex_rewrite_substitution_ = rewrite_spec.substitution();
  }
}

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
    // Rule must have at least one of the `on_header_*` fields set.
    if (!entry.has_on_header_present() && !entry.has_on_header_missing()) {
      const auto& error = fmt::format("header to metadata filter: rule for header '{}' has neither "
                                      "`on_header_present` nor `on_header_missing` set",
                                      entry.header());
      throw EnvoyException(error);
    }

    // Ensure value and regex_value_rewrite are not mixed.
    // TODO(rgs1): remove this once we are on v4 and these fields are folded into a oneof.
    if (!entry.on_header_present().value().empty() &&
        entry.on_header_present().has_regex_value_rewrite()) {
      throw EnvoyException("Cannot specify both value and regex_value_rewrite");
    }

    if (entry.has_on_header_missing() && entry.on_header_missing().value().empty()) {
      throw EnvoyException("Cannot specify on_header_missing rule with an empty value");
    }

    vector.emplace_back(entry.header(), entry);
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

  ASSERT(!value.empty());

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
    NOT_REACHED_GCOVR_EXCL_LINE;
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

  for (const auto& rule : rules) {
    const auto& header = rule.header();
    const auto& proto_rule = rule.rule();
    const Http::HeaderEntry* header_entry = headers.get(header);

    if (header_entry != nullptr && proto_rule.has_on_header_present()) {
      const auto& keyval = proto_rule.on_header_present();
      absl::string_view value = header_entry->value().getStringView();
      // This is used to hold the rewritten header value, so that it can
      // be bound to value without going out of scope.
      std::string rewritten_value;

      if (!keyval.value().empty()) {
        value = absl::string_view(keyval.value());
      } else {
        const auto& matcher = rule.regexRewrite();
        if (matcher != nullptr) {
          rewritten_value = matcher->replaceAll(value, rule.regexSubstitution());
          value = rewritten_value;
        }
      }

      if (!value.empty()) {
        const auto& nspace = decideNamespace(keyval.metadata_namespace());
        addMetadata(structs_by_namespace, nspace, keyval.key(), value, keyval.type(),
                    keyval.encode());
      } else {
        ENVOY_LOG(debug, "value is empty, not adding metadata");
      }

      if (proto_rule.remove()) {
        headers.remove(header);
      }
    }
    if (header_entry == nullptr && proto_rule.has_on_header_missing()) {
      // Add metadata for the header missing case.
      const auto& keyval = proto_rule.on_header_missing();

      ASSERT(!keyval.value().empty());
      const auto& nspace = decideNamespace(keyval.metadata_namespace());
      addMetadata(structs_by_namespace, nspace, keyval.key(), keyval.value(), keyval.type(),
                  keyval.encode());
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

  effective_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<Config>(
      HttpFilterNames::get().HeaderToMetadata, decoder_callbacks_->route());
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
