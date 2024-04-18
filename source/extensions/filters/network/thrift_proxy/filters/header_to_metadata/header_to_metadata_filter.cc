#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/header_to_metadata_filter.h"

#include "source/common/common/base64.h"
#include "source/common/common/regex.h"
#include "source/common/http/headers.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace HeaderToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;

// Extract the value of the header.
absl::optional<std::string> HeaderValueSelector::extract(Http::HeaderMap& map) const {
  const auto header_entry = map.get(header_);
  if (header_entry.empty()) {
    return absl::nullopt;
  }
  // Catch the value in the header before removing.
  absl::optional<std::string> value = std::string(header_entry[0]->value().getStringView());
  if (remove_) {
    map.remove(header_);
  }
  return value;
}

Rule::Rule(const ProtoRule& rule, Regex::Engine& regex_engine) : rule_(rule) {
  selector_ =
      std::make_shared<HeaderValueSelector>(Http::LowerCaseString(rule.header()), rule.remove());

  // Rule must have at least one of the `on_*` fields set.
  if (!rule.has_on_present() && !rule.has_on_missing()) {
    const auto& error = fmt::format("header to metadata filter: rule for {} has neither "
                                    "`on_present` nor `on_missing` set",
                                    selector_->toString());
    throw EnvoyException(error);
  }

  if (rule.has_on_missing() && rule.on_missing().value().empty()) {
    throw EnvoyException("Cannot specify on_missing rule with empty value");
  }

  if (rule.has_on_present() && rule.on_present().has_regex_value_rewrite()) {
    const auto& rewrite_spec = rule.on_present().regex_value_rewrite();
    regex_rewrite_ = Regex::Utility::parseRegex(rewrite_spec.pattern(), regex_engine);
    regex_rewrite_substitution_ = rewrite_spec.substitution();
  }
}

Config::Config(const envoy::extensions::filters::network::thrift_proxy::filters::
                   header_to_metadata::v3::HeaderToMetadata& config,
               Regex::Engine& regex_engine) {
  for (const auto& entry : config.request_rules()) {
    request_rules_.emplace_back(entry, regex_engine);
  }
}

HeaderToMetadataFilter::HeaderToMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

ThriftProxy::FilterStatus
HeaderToMetadataFilter::transportBegin(ThriftProxy::MessageMetadataSharedPtr metadata) {
  auto& headers = metadata->requestHeaders();

  writeHeaderToMetadata(headers, config_->requestRules(), *decoder_callbacks_);

  return ThriftProxy::FilterStatus::Continue;
}

const std::string& HeaderToMetadataFilter::decideNamespace(const std::string& nspace) const {
  static const std::string& headerToMetadata = "envoy.filters.thrift.header_to_metadata";
  return nspace.empty() ? headerToMetadata : nspace;
}

bool HeaderToMetadataFilter::addMetadata(StructMap& struct_map, const std::string& meta_namespace,
                                         const std::string& key, std::string value, ValueType type,
                                         ValueEncode encode) const {
  ProtobufWkt::Value val;

  ASSERT(!value.empty());

  if (value.size() >= MAX_HEADER_VALUE_LEN) {
    // Too long, go away.
    ENVOY_LOG(debug, "metadata value is too long");
    return false;
  }

  if (encode == envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
                    HeaderToMetadata::BASE64) {
    value = Base64::decodeWithoutPadding(value);
    if (value.empty()) {
      ENVOY_LOG(debug, "Base64 decode failed");
      return false;
    }
  }

  // Sane enough, add the key/value.
  switch (type) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
      HeaderToMetadata::STRING:
    val.set_string_value(std::move(value));
    break;
  case envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
      HeaderToMetadata::NUMBER: {
    double dval;
    if (absl::SimpleAtod(StringUtil::trim(value), &dval)) {
      val.set_number_value(dval);
    } else {
      ENVOY_LOG(debug, "value to number conversion failed");
      return false;
    }
    break;
  }
  case envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
      HeaderToMetadata::PROTOBUF_VALUE: {
    if (!val.ParseFromString(value)) {
      ENVOY_LOG(debug, "parse from decoded string failed");
      return false;
    }
    break;
  }
  }

  auto& keyval = struct_map[meta_namespace];
  (*keyval.mutable_fields())[key] = std::move(val);

  return true;
}

// add metadata['key']= value depending on header present or missing case
void HeaderToMetadataFilter::applyKeyValue(std::string&& value, const Rule& rule,
                                           const KeyValuePair& keyval, StructMap& np) const {
  if (keyval.has_regex_value_rewrite()) {
    const auto& matcher = rule.regexRewrite();
    value = matcher->replaceAll(value, rule.regexSubstitution());
  } else if (!keyval.value().empty()) {
    value = keyval.value();
  }
  if (!value.empty()) {
    const auto& nspace = decideNamespace(keyval.metadata_namespace());
    addMetadata(np, nspace, keyval.key(), value, keyval.type(), keyval.encode());
  } else {
    ENVOY_LOG(debug, "value is empty, not adding metadata");
  }
}

void HeaderToMetadataFilter::writeHeaderToMetadata(
    Http::HeaderMap& headers, const HeaderToMetadataRules& rules,
    ThriftProxy::ThriftFilters::DecoderFilterCallbacks& callbacks) const {
  StructMap structs_by_namespace;

  for (const auto& rule : rules) {
    const auto& proto_rule = rule.rule();
    absl::optional<std::string> value = rule.selector_->extract(headers);

    if (value && proto_rule.has_on_present()) {
      applyKeyValue(std::move(value).value_or(""), rule, proto_rule.on_present(),
                    structs_by_namespace);
    } else if (!value && proto_rule.has_on_missing()) {
      applyKeyValue(std::move(value).value_or(""), rule, proto_rule.on_missing(),
                    structs_by_namespace);
    }
  }
  // Any matching rules?
  if (!structs_by_namespace.empty()) {
    for (auto const& entry : structs_by_namespace) {
      callbacks.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

} // namespace HeaderToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
