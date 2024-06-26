#include "source/extensions/filters/http/json_to_metadata/filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

namespace {

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

absl::flat_hash_set<std::string> generateAllowContentTypes(
    const Protobuf::RepeatedPtrField<std::string>& proto_allow_content_types) {
  if (proto_allow_content_types.empty()) {
    return {Http::Headers::get().ContentTypeValues.Json};
  }

  absl::flat_hash_set<std::string> allow_content_types;
  for (const auto& allowed_content_type : proto_allow_content_types) {
    allow_content_types.insert(allowed_content_type);
  }
  return allow_content_types;
}

Regex::CompiledMatcherPtr generateAllowContentTypeRegexs(
    const envoy::type::matcher::v3::RegexMatcher& proto_allow_content_types_regex,
    Regex::Engine& regex_engine) {

  Regex::CompiledMatcherPtr allow_content_types_regex;
  allow_content_types_regex =
      Regex::Utility::parseRegex(proto_allow_content_types_regex, regex_engine);

  return allow_content_types_regex;
}

} // anonymous namespace

Rule::Rule(const ProtoRule& rule) : rule_(rule) {
  if (!rule_.has_on_present() && !rule_.has_on_missing()) {
    throw EnvoyException("json to metadata filter: neither `on_present` nor `on_missing` set");
  }

  if (rule_.has_on_missing() && !rule_.on_missing().has_value()) {
    throw EnvoyException(
        "json to metadata filter: cannot specify on_missing rule with empty value");
  }

  if (rule_.has_on_error() && !rule_.on_error().has_value()) {
    throw EnvoyException("json to metadata filter: cannot specify on_error rule with empty value");
  }

  // Support key selectors only.
  for (const auto& selector : rule.selectors()) {
    keys_.push_back(selector.key());
  }
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config,
    Stats::Scope& scope, Regex::Engine& regex_engine)
    : rqstats_{ALL_JSON_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "json_to_metadata.rq"))},
      respstats_{
          ALL_JSON_TO_METADATA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, "json_to_metadata.resp"))},
      request_rules_(generateRules(proto_config.request_rules().rules())),
      response_rules_(generateRules(proto_config.response_rules().rules())),
      request_allow_content_types_(
          generateAllowContentTypes(proto_config.request_rules().allow_content_types())),
      response_allow_content_types_(
          generateAllowContentTypes(proto_config.response_rules().allow_content_types())),
      request_allow_empty_content_type_(proto_config.request_rules().allow_empty_content_type()),
      response_allow_empty_content_type_(proto_config.response_rules().allow_empty_content_type()),
      request_allow_content_types_regex_(
          proto_config.request_rules().has_allow_content_types_regex()
              ? generateAllowContentTypeRegexs(
                    proto_config.request_rules().allow_content_types_regex(), regex_engine)
              : nullptr),
      response_allow_content_types_regex_(
          proto_config.response_rules().has_allow_content_types_regex()
              ? generateAllowContentTypeRegexs(
                    proto_config.response_rules().allow_content_types_regex(), regex_engine)
              : nullptr) {
  if (request_rules_.empty() && response_rules_.empty()) {
    throw EnvoyException("json_to_metadata_filter: Per filter configs must at least specify "
                         "either request or response rules");
  }
}

Rules FilterConfig::generateRules(const ProtobufRepeatedRule& proto_rules) const {
  Rules rules;
  for (const auto& rule : proto_rules) {
    rules.emplace_back(rule);
  }
  return rules;
}

bool FilterConfig::requestContentTypeAllowed(absl::string_view content_type) const {
  if (content_type.empty()) {
    return request_allow_empty_content_type_;
  }

  return request_allow_content_types_.contains(content_type) ||
         (request_allow_content_types_regex_ &&
          request_allow_content_types_regex_->match(content_type));
}

bool FilterConfig::responseContentTypeAllowed(absl::string_view content_type) const {
  if (content_type.empty()) {
    return response_allow_empty_content_type_;
  }

  return response_allow_content_types_.contains(content_type) ||
         (response_allow_content_types_regex_ &&
          response_allow_content_types_regex_->match(content_type));
}

void Filter::applyKeyValue(const std::string& value, const KeyValuePair& keyval,
                           StructMap& struct_map, Http::StreamFilterCallbacks& filter_callback) {
  ASSERT(!value.empty());

  ProtobufWkt::Value val;
  val.set_string_value(value);
  applyKeyValue(std::move(val), keyval, struct_map, filter_callback);
}

void Filter::applyKeyValue(double value, const KeyValuePair& keyval, StructMap& struct_map,
                           Http::StreamFilterCallbacks& filter_callback) {
  ProtobufWkt::Value val;
  val.set_number_value(value);
  applyKeyValue(std::move(val), keyval, struct_map, filter_callback);
}

void Filter::applyKeyValue(ProtobufWkt::Value value, const KeyValuePair& keyval,
                           StructMap& struct_map, Http::StreamFilterCallbacks& filter_callback) {
  const auto& nspace = decideNamespace(keyval.metadata_namespace());
  addMetadata(nspace, keyval.key(), std::move(value), keyval.preserve_existing_metadata_value(),
              struct_map, filter_callback);
}

const std::string& Filter::decideNamespace(const std::string& nspace) const {
  return nspace.empty() ? HttpFilterNames::get().JsonToMetadata : nspace;
}

bool Filter::addMetadata(const std::string& meta_namespace, const std::string& key,
                         ProtobufWkt::Value val, const bool preserve_existing_metadata_value,
                         StructMap& struct_map, Http::StreamFilterCallbacks& filter_callback) {

  if (preserve_existing_metadata_value) {
    auto& filter_metadata = filter_callback.streamInfo().dynamicMetadata().filter_metadata();
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
  auto& keyval = struct_map[meta_namespace];
  (*keyval.mutable_fields())[key] = std::move(val);

  return true;
}

void Filter::finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                                     bool should_clear_route_cache, const StructMap& struct_map,
                                     bool& processing_finished_flag) {
  ASSERT(!processing_finished_flag);
  processing_finished_flag = true;
  if (!struct_map.empty()) {
    for (auto const& entry : struct_map) {
      filter_callback.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }

    if (should_clear_route_cache) {
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }
  }
}

void Filter::handleAllOnMissing(const Rules& rules, bool should_clear_route_cache,
                                Http::StreamFilterCallbacks& filter_callback,
                                bool& processing_finished_flag) {
  StructMap struct_map;
  for (const auto& rule : rules) {
    if (rule.rule_.has_on_missing()) {
      applyKeyValue(rule.rule_.on_missing().value(), rule.rule_.on_missing(), struct_map,
                    filter_callback);
    }
  }

  finalizeDynamicMetadata(filter_callback, should_clear_route_cache, struct_map,
                          processing_finished_flag);
}

void Filter::handleOnMissing(const Rule& rule, StructMap& struct_map,
                             Http::StreamFilterCallbacks& filter_callback) {
  if (rule.rule_.has_on_missing()) {
    applyKeyValue(rule.rule_.on_missing().value(), rule.rule_.on_missing(), struct_map,
                  filter_callback);
  }
}

void Filter::handleAllOnError(const Rules& rules, bool should_clear_route_cache,
                              Http::StreamFilterCallbacks& filter_callback,
                              bool& processing_finished_flag) {
  StructMap struct_map;
  for (const auto& rule : rules) {
    if (rule.rule_.has_on_error()) {
      applyKeyValue(rule.rule_.on_error().value(), rule.rule_.on_error(), struct_map,
                    filter_callback);
    }
  }
  finalizeDynamicMetadata(filter_callback, should_clear_route_cache, struct_map,
                          processing_finished_flag);
}

absl::Status Filter::handleOnPresent(Json::ObjectSharedPtr parent_node, const std::string& key,
                                     const Rule& rule, StructMap& struct_map,
                                     Http::StreamFilterCallbacks& filter_callback) {
  if (!rule.rule_.has_on_present()) {
    return absl::OkStatus();
  }

  auto& on_present_keyval = rule.rule_.on_present();
  if (on_present_keyval.has_value()) {
    applyKeyValue(on_present_keyval.value(), on_present_keyval, struct_map, filter_callback);
    return absl::OkStatus();
  }

  absl::StatusOr<Envoy::Json::ValueType> result = parent_node->getValue(key);
  if (!result.ok()) {
    return result.status();
  }

  switch (on_present_keyval.type()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::PROTOBUF_VALUE:
    if (auto value_result =
            absl::visit(JsonValueToProtobufValueConverter(), std::move(result.value()));
        value_result.ok()) {
      applyKeyValue(value_result.value(), on_present_keyval, struct_map, filter_callback);
    } else {
      return value_result.status();
    }
    break;
  case envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER:
    if (auto double_result = absl::visit(JsonValueToDoubleConverter(), std::move(result.value()));
        double_result.ok()) {
      applyKeyValue(double_result.value(), on_present_keyval, struct_map, filter_callback);
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

    applyKeyValue(std::move(str), on_present_keyval, struct_map, filter_callback);
    break;
  }
  return absl::OkStatus();
}

void Filter::processBody(const Buffer::Instance* body, const Rules& rules,
                         bool should_clear_route_cache, JsonToMetadataStats& stats,
                         Http::StreamFilterCallbacks& filter_callback,
                         bool& processing_finished_flag) {
  // In case we have trailers but no body.
  if (!body || body->length() == 0) {
    handleAllOnMissing(rules, should_clear_route_cache, filter_callback, processing_finished_flag);
    stats.no_body_.inc();
    return;
  }

  absl::StatusOr<Json::ObjectSharedPtr> result =
      Json::Factory::loadFromStringNoThrow(body->toString());
  if (!result.ok()) {
    ENVOY_LOG(debug, result.status().message());
    stats.invalid_json_body_.inc();
    handleAllOnError(rules, should_clear_route_cache, filter_callback, processing_finished_flag);
    return;
  }

  Json::ObjectSharedPtr body_json = std::move(result.value());
  // A pure string or number is considered a valid application/json body, but it is not a JSON
  // object. Therefore, we treat this case as 'on_missing' for all rules in the absence of any
  // key-value pairs to match.
  if (!body_json) {
    ENVOY_LOG(
        debug,
        "Apply on_missing for all rules on a valid application/json body but not a json object.");
    handleAllOnMissing(rules, should_clear_route_cache, filter_callback, processing_finished_flag);
    // This JSON body is valid and successfully parsed.
    stats.success_.inc();
    return;
  }

  StructMap struct_map;
  for (const auto& rule : rules) {
    const auto& keys = rule.keys_;
    Json::ObjectSharedPtr node = body_json;
    bool on_missing = false;
    for (unsigned long i = 0; i < keys.size() - 1; i++) {
      absl::StatusOr<Json::ObjectSharedPtr> next_node_result = node->getObjectNoThrow(keys[i]);
      if (!next_node_result.ok()) {
        ENVOY_LOG(warn, result.status().message());
        handleOnMissing(rule, struct_map, filter_callback);
        on_missing = true;
        break;
      }
      node = std::move(next_node_result.value());
    }
    if (on_missing) {
      continue;
    }
    absl::Status result =
        handleOnPresent(std::move(node), keys.back(), rule, struct_map, filter_callback);
    if (!result.ok()) {
      ENVOY_LOG(warn, fmt::format("{} key: {}", result.message(), keys.back()));
      handleOnMissing(rule, struct_map, filter_callback);
    }
  }
  stats.success_.inc();

  finalizeDynamicMetadata(filter_callback, should_clear_route_cache, struct_map,
                          processing_finished_flag);
}

void Filter::processRequestBody() {
  processBody(decoder_callbacks_->decodingBuffer(), config_->requestRules(), true,
              config_->rqstats(), *decoder_callbacks_, request_processing_finished_);
}

void Filter::processResponseBody() {
  processBody(encoder_callbacks_->encodingBuffer(), config_->responseRules(), false,
              config_->respstats(), *encoder_callbacks_, response_processing_finished_);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  if (!config_->doRequest()) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (!config_->requestContentTypeAllowed(headers.getContentTypeValue())) {
    handleAllOnError(config_->requestRules(), true, *decoder_callbacks_,
                     request_processing_finished_);
    config_->rqstats().mismatched_content_type_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (end_stream) {
    handleAllOnMissing(config_->requestRules(), true, *decoder_callbacks_,
                       request_processing_finished_);
    config_->rqstats().no_body_.inc();
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  if (!config_->doResponse()) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (!config_->responseContentTypeAllowed(headers.getContentTypeValue())) {
    handleAllOnError(config_->responseRules(), false, *encoder_callbacks_,
                     response_processing_finished_);
    config_->respstats().mismatched_content_type_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (end_stream) {
    handleAllOnMissing(config_->responseRules(), false, *encoder_callbacks_,
                       response_processing_finished_);
    config_->respstats().no_body_.inc();
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!config_->doRequest()) {
    return Http::FilterDataStatus::Continue;
  }
  if (request_processing_finished_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    decoder_callbacks_->addDecodedData(data, true);

    if (!decoder_callbacks_->decodingBuffer() ||
        decoder_callbacks_->decodingBuffer()->length() == 0) {
      handleAllOnMissing(config_->requestRules(), true, *decoder_callbacks_,
                         request_processing_finished_);
      config_->rqstats().no_body_.inc();
      return Http::FilterDataStatus::Continue;
    }
    processRequestBody();
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!config_->doResponse()) {
    return Http::FilterDataStatus::Continue;
  }
  if (response_processing_finished_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    encoder_callbacks_->addEncodedData(data, true);

    if (!encoder_callbacks_->encodingBuffer() ||
        encoder_callbacks_->encodingBuffer()->length() == 0) {
      handleAllOnMissing(config_->responseRules(), false, *encoder_callbacks_,
                         response_processing_finished_);
      config_->respstats().no_body_.inc();
      return Http::FilterDataStatus::Continue;
    }
    processResponseBody();
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
  if (!config_->doRequest()) {
    return Http::FilterTrailersStatus::Continue;
  }
  if (!request_processing_finished_) {
    processRequestBody();
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (!config_->doResponse()) {
    return Http::FilterTrailersStatus::Continue;
  }
  if (!response_processing_finished_) {
    processResponseBody();
  }
  return Http::FilterTrailersStatus::Continue;
}

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
