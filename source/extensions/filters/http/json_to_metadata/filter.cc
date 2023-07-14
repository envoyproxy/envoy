#include "source/extensions/filters/http/json_to_metadata/filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

Rule::Rule(const ProtoRule& rule) : rule_(rule) {
  if (!rule_.has_on_present() && !rule_.has_on_missing()) {
    throw EnvoyException("json to metadata filter: neither `on_present` nor `on_missing` set");
  }

  if (rule_.has_on_missing() && !rule_.on_missing().has_value()) {
    throw EnvoyException(
        "json to metadata filter: cannot specify on_missing rule without non-empty value");
  }

  if (rule_.has_on_error() && !rule_.on_error().has_value()) {
    throw EnvoyException(
        "json to metadata filter: cannot specify on_error rule without non-empty value");
  }

  // Support key selectors only.
  for (const auto& selector : rule.selectors()) {
    keys_.push_back(selector.key());
  }
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config,
    Stats::Scope& scope)
    : stats_{ALL_JSON_TO_METADATA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, "json_to_metadata."))},
      request_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, request_buffer_limit_bytes, 1024 * 1024)),
      request_allow_empty_content_type_(proto_config.request_allow_empty_content_type()) {
  for (const auto& rule : proto_config.request_rules()) {
    request_rules_.emplace_back(rule);
  }

  if (proto_config.request_allow_content_types().empty()) {
    request_allow_content_types_ = {Http::Headers::get().ContentTypeValues.Json};
    return;
  }

  for (const auto& request_allowed_content_type : proto_config.request_allow_content_types()) {
    request_allow_content_types_.insert(request_allowed_content_type);
  }
}

Filter::~Filter() = default;

bool FilterConfig::requestContentTypeAllowed(absl::string_view content_type) const {
  if (content_type.empty()) {
    return request_allow_empty_content_type_;
  }

  return request_allow_content_types_.contains(content_type);
}

void Filter::applyKeyValue(std::string value, const KeyValuePair& keyval, StructMap& struct_map) {
  ASSERT(!value.empty());

  ProtobufWkt::Value val;
  val.set_string_value(value);
  applyKeyValue(std::move(val), keyval, struct_map);
}

void Filter::applyKeyValue(double value, const KeyValuePair& keyval, StructMap& struct_map) {
  ProtobufWkt::Value val;
  val.set_number_value(value);
  applyKeyValue(std::move(val), keyval, struct_map);
}

void Filter::applyKeyValue(ProtobufWkt::Value value, const KeyValuePair& keyval,
                           StructMap& struct_map) {
  const auto& nspace = decideNamespace(keyval.metadata_namespace());
  addMetadata(nspace, keyval.key(), std::move(value), keyval.preserve_existing_metadata_value(),
              struct_map);
}

const std::string& Filter::decideNamespace(const std::string& nspace) const {
  static const std::string& jsonToMetadata = "envoy.filters.http.json_to_metadata";
  return nspace.empty() ? jsonToMetadata : nspace;
}

bool Filter::addMetadata(const std::string& meta_namespace, const std::string& key,
                         ProtobufWkt::Value val, const bool preserve_existing_metadata_value,
                         StructMap& struct_map) {

  if (preserve_existing_metadata_value) {
    // TODO(kuochunghsu): support encoding
    auto& filter_metadata = decoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto entry_it = filter_metadata.find(meta_namespace);

    if (entry_it != filter_metadata.end()) {
      const auto& metadata = entry_it->second;
      if (metadata.fields().contains(key)) {
        ENVOY_LOG(trace, "Found key {} in namespace {}. Preserve the existing metadata value.", key,
                  meta_namespace);
        return false;
      }
    }
  }

  ENVOY_LOG(trace, "add metadata ns:{} key:{}", meta_namespace, key);

  // Have we seen this namespace before?
  auto namespace_iter = struct_map.find(meta_namespace);
  if (namespace_iter == struct_map.end()) {
    struct_map[meta_namespace] = ProtobufWkt::Struct();
    namespace_iter = struct_map.find(meta_namespace);
  }

  auto& keyval = namespace_iter->second;
  (*keyval.mutable_fields())[key] = std::move(val);

  return true;
}

void Filter::finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                                     const StructMap& struct_map, bool& reported_flag) {
  ASSERT(!reported_flag);
  reported_flag = true;
  if (!struct_map.empty()) {
    for (auto const& entry : struct_map) {
      filter_callback.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

void Filter::handleAllOnMissing(const Rules& rules, bool& reported_flag) {
  StructMap struct_map;
  for (const auto& rule : rules) {
    if (rule.rule().has_on_missing()) {
      applyKeyValue(rule.rule().on_missing().value(), rule.rule().on_missing(), struct_map);
    }
  }

  finalizeDynamicMetadata(*decoder_callbacks_, struct_map, reported_flag);
}

void Filter::handleOnMissing(const Rule& rule, StructMap& struct_map) {
  if (rule.rule().has_on_missing()) {
    applyKeyValue(rule.rule().on_missing().value(), rule.rule().on_missing(), struct_map);
  }
}

void Filter::handleAllOnError(const Rules& rules, bool& reported_flag) {
  StructMap struct_map;
  for (const auto& rule : rules) {
    if (rule.rule().has_on_error()) {
      applyKeyValue(rule.rule().on_error().value(), rule.rule().on_error(), struct_map);
    }
  }
  finalizeDynamicMetadata(*decoder_callbacks_, struct_map, reported_flag);
}

struct JsonValueToStringConverter {
  std::string operator()(bool&& val) { return std::to_string(val); }
  std::string operator()(int64_t&& val) { return std::to_string(val); }
  std::string operator()(double&& val) { return std::to_string(val); }
  std::string operator()(std::string&& val) { return std::move(val); }
};

struct JsonValueToDoubleConverter {
  absl::StatusOr<double> operator()(bool&& val) { return static_cast<double>(val); }
  absl::StatusOr<double> operator()(int64_t&& val) { return static_cast<double>(val); }
  absl::StatusOr<double> operator()(double&& val) { return val; }
  absl::StatusOr<double> operator()(std::string&& val) {
    double dval;
    if (absl::SimpleAtod(StringUtil::trim(val), &dval)) {
      return dval;
    }
    return absl::InternalError(fmt::format("value {} to number conversion failed", val));
  }
};

struct JsonValueToProtobufValueConverter {
  absl::StatusOr<ProtobufWkt::Value> operator()(bool&& val) {
    ProtobufWkt::Value protobuf_value;
    protobuf_value.set_bool_value(val);
    return protobuf_value;
  }
  absl::StatusOr<ProtobufWkt::Value> operator()(int64_t&& val) {
    ProtobufWkt::Value protobuf_value;
    protobuf_value.set_number_value(val);
    return protobuf_value;
  }
  absl::StatusOr<ProtobufWkt::Value> operator()(double&& val) {
    ProtobufWkt::Value protobuf_value;
    protobuf_value.set_number_value(val);
    return protobuf_value;
  }
  absl::StatusOr<ProtobufWkt::Value> operator()(std::string&& val) {
    if (val.size() > MAX_PAYLOAD_VALUE_LEN) {
      return absl::InternalError(
          fmt::format("metadata value is too long. value.length: {}", val.size()));
    }
    ProtobufWkt::Value protobuf_value;
    protobuf_value.set_string_value(std::move(val));
    return protobuf_value;
  }
};

absl::Status Filter::handleOnPresent(Json::ObjectSharedPtr parent_node, const std::string& key,
                                     const Rule& rule, StructMap& struct_map) {
  if (!rule.rule().has_on_present()) {
    return absl::OkStatus();
  }

  auto& on_present_keyval = rule.rule().on_present();
  if (on_present_keyval.has_value()) {
    applyKeyValue(on_present_keyval.value(), on_present_keyval, struct_map);
    return absl::OkStatus();
  }

  absl::StatusOr<Envoy::Json::ValueType> result = parent_node->getValue(key);
  if (!result.ok()) {
    return result.status();
  }

  // Exception is handled by caller, which leads on_missing.
  switch (on_present_keyval.type()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::PROTOBUF_VALUE:
    if (auto value_result =
            absl::visit(JsonValueToProtobufValueConverter(), std::move(result.value()));
        value_result.ok()) {
      applyKeyValue(value_result.value(), on_present_keyval, struct_map);
    } else {
      return value_result.status();
    }
    break;
  case envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER:
    if (auto double_result = absl::visit(JsonValueToDoubleConverter(), std::move(result.value()));
        double_result.ok()) {
      applyKeyValue(double_result.value(), on_present_keyval, struct_map);
    } else {
      return double_result.status();
    }
    break;
  case envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::STRING:
    std::string str = absl::visit(JsonValueToStringConverter(), std::move(result.value()));
    if (str.size() > MAX_PAYLOAD_VALUE_LEN) {
      return absl::InvalidArgumentError(
          fmt::format("metadata value is too long. value.length: {}", str.size()));
    }

    // Note that this applies to on_present by not adding any metadata.
    if (str.empty()) {
      ENVOY_LOG(debug, "value is empty, not adding metadata. key: {}", on_present_keyval.key());
      return absl::OkStatus();
    }

    applyKeyValue(std::move(str), on_present_keyval, struct_map);
    break;
  }
  return absl::OkStatus();
}

void Filter::processBody(const Buffer::Instance* body, const Rules& rules, bool& reported_flag,
                         Stats::Counter& success, Stats::Counter& no_body,
                         Stats::Counter& non_json) {
  if (!body) {
    handleAllOnMissing(rules, is_request_reported_);
    no_body.inc();
    return;
  }

  absl::StatusOr<Json::ObjectSharedPtr> result =
      Json::Factory::loadFromStringNoThrow(body->toString());
  if (!result.ok()) {
    ENVOY_LOG(debug, result.status().message());
    non_json.inc();
    handleAllOnError(rules, reported_flag);
    return;
  }

  Json::ObjectSharedPtr body_json = std::move(result.value());
  StructMap struct_map;
  for (const auto& rule : rules) {
    const auto& keys = rule.keys();
    Json::ObjectSharedPtr node = body_json;
    for (unsigned long i = 0; i < keys.size(); i++) {
      if (i < keys.size() - 1) {
        absl::StatusOr<Json::ObjectSharedPtr> next_node_result = node->getObjectNoThrow(keys[i]);
        if (!next_node_result.ok()) {
          ENVOY_LOG(warn, result.status().message());
          handleOnMissing(rule, struct_map);
          break;
        }
        node = std::move(next_node_result.value());
      } else {
        absl::Status result = handleOnPresent(std::move(node), keys[i], rule, struct_map);
        if (!result.ok()) {
          ENVOY_LOG(warn, fmt::format("{} key: {}", result.message(), keys[i]));
          handleOnMissing(rule, struct_map);
        }
      }
    }
  }
  success.inc();

  finalizeDynamicMetadata(*decoder_callbacks_, struct_map, reported_flag);
}

void Filter::processRequestBody() {
  processBody(decoder_callbacks_->decodingBuffer(), config_->requestRules(), is_request_reported_,
              config_->stats().rq_success_, config_->stats().rq_no_body_,
              config_->stats().rq_invalid_json_body_);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  ASSERT(config_->doRequest());
  if (!config_->requestContentTypeAllowed(headers.getContentTypeValue())) {
    is_request_reported_ = true;
    config_->stats().rq_mismatched_content_type_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (end_stream) {
    handleAllOnMissing(config_->requestRules(), is_request_reported_);
    config_->stats().rq_no_body_.inc();
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool end_stream) {
  ASSERT(config_->doRequest());
  if (is_request_reported_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    if (!decoder_callbacks_->decodingBuffer() ||
        decoder_callbacks_->decodingBuffer()->length() == 0) {
      handleAllOnMissing(config_->requestRules(), is_request_reported_);
      config_->stats().rq_no_body_.inc();
      return Http::FilterDataStatus::Continue;
    }
    processRequestBody();
    return Http::FilterDataStatus::Continue;
  }

  if (decoder_callbacks_->decodingBuffer() &&
      decoder_callbacks_->decodingBuffer()->length() > config_->requestBufferLimitBytes()) {
    handleAllOnError(config_->requestRules(), is_request_reported_);
    ENVOY_LOG(debug, "Request body is too large. buffer_size {} buffer_limit {}",
              decoder_callbacks_->decodingBuffer()->length(), config_->requestBufferLimitBytes());
    config_->stats().rq_too_large_body_.inc();
    return Http::FilterDataStatus::Continue;
  }
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
  ASSERT(config_->doRequest());
  if (!is_request_reported_) {
    processRequestBody();
  }
  return Http::FilterTrailersStatus::Continue;
}

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
