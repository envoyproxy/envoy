#include "source/extensions/filters/http/thrift_to_metadata/filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

Rule::Rule(const ProtoRule& rule, uint16_t rule_id, PayloadExtractor::TrieSharedPtr root)
    : rule_(rule), rule_id_(rule_id), method_name_(rule.method_name()) {
  if (!rule_.has_on_present() && !rule_.has_on_missing()) {
    throw EnvoyException("thrift to metadata filter: neither `on_present` nor `on_missing` set");
  }

  if (rule_.has_on_missing() && !rule_.on_missing().has_value()) {
    throw EnvoyException(
        "thrift to metadata filter: cannot specify on_missing rule with empty value");
  }

  if (rule_.has_field_selector()) {
    root->insert<envoy::extensions::filters::http::thrift_to_metadata::v3::FieldSelector>(
        &rule_.field_selector(), rule_id);
  } else {
    protobuf_value_extracter_ = getValueExtractorFromField(rule_.field());
  }
}

bool Rule::matches(const MessageMetadata& metadata) const {
  if (method_name_.empty()) {
    return true;
  }

  const std::string& metadata_method_name = metadata.hasMethodName() ? metadata.methodName() : "";
  const auto func_pos = metadata_method_name.find(':');
  if (func_pos != std::string::npos) {
    return metadata_method_name.substr(func_pos + 1) == method_name_;
  }
  return metadata_method_name == method_name_;
}

ThriftMetadataToProtobufValue Rule::getValueExtractorFromField(
    envoy::extensions::filters::http::thrift_to_metadata::v3::Field field) const {
  switch (field) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::METHOD_NAME:
    return [](MessageMetadataSharedPtr metadata,
              const ThriftDecoderHandler&) -> absl::optional<Protobuf::Value> {
      if (!metadata->hasMethodName()) {
        return absl::nullopt;
      }
      Protobuf::Value value;
      value.set_string_value(metadata->methodName());
      return value;
    };
  case envoy::extensions::filters::http::thrift_to_metadata::v3::PROTOCOL:
    return [](MessageMetadataSharedPtr,
              const ThriftDecoderHandler& handler) -> absl::optional<Protobuf::Value> {
      Protobuf::Value value;
      value.set_string_value(handler.protocolName());
      return value;
    };
  case envoy::extensions::filters::http::thrift_to_metadata::v3::TRANSPORT:
    return [](MessageMetadataSharedPtr,
              const ThriftDecoderHandler& handler) -> absl::optional<Protobuf::Value> {
      Protobuf::Value value;
      value.set_string_value(handler.transportName());
      return value;
    };
  case envoy::extensions::filters::http::thrift_to_metadata::v3::HEADER_FLAGS:
    return [](MessageMetadataSharedPtr metadata,
              const ThriftDecoderHandler&) -> absl::optional<Protobuf::Value> {
      if (!metadata->hasHeaderFlags()) {
        return absl::nullopt;
      }
      Protobuf::Value value;
      value.set_number_value(metadata->headerFlags());
      return value;
    };
  case envoy::extensions::filters::http::thrift_to_metadata::v3::SEQUENCE_ID:
    return [](MessageMetadataSharedPtr metadata,
              const ThriftDecoderHandler&) -> absl::optional<Protobuf::Value> {
      if (!metadata->hasSequenceId()) {
        return absl::nullopt;
      }
      Protobuf::Value value;
      value.set_number_value(metadata->sequenceId());
      return value;
    };
  case envoy::extensions::filters::http::thrift_to_metadata::v3::MESSAGE_TYPE:
    return [](MessageMetadataSharedPtr metadata,
              const ThriftDecoderHandler&) -> absl::optional<Protobuf::Value> {
      if (!metadata->hasMessageType()) {
        return absl::nullopt;
      }
      Protobuf::Value value;
      value.set_string_value(MessageTypeNames::get().fromType(metadata->messageType()));
      return value;
    };
  case envoy::extensions::filters::http::thrift_to_metadata::v3::REPLY_TYPE:
    return [](MessageMetadataSharedPtr metadata,
              const ThriftDecoderHandler&) -> absl::optional<Protobuf::Value> {
      if (!metadata->hasReplyType()) {
        return absl::nullopt;
      }
      Protobuf::Value value;
      value.set_string_value(ReplyTypeNames::get().fromType(metadata->replyType()));
      return value;
    };
  }
  PANIC("not reached");
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata& proto_config,
    Stats::Scope& scope)
    : rqstats_{ALL_THRIFT_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "thrift_to_metadata.rq"))},
      respstats_{ALL_THRIFT_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "thrift_to_metadata.resp"))},
      rq_trie_root_(std::make_shared<PayloadExtractor::Trie>()),
      resp_trie_root_(std::make_shared<PayloadExtractor::Trie>()),
      request_rules_(generateRules(proto_config.request_rules(), rq_trie_root_)),
      response_rules_(generateRules(proto_config.response_rules(), resp_trie_root_)),
      transport_(ProtoUtils::getTransportType(proto_config.transport())),
      protocol_(ProtoUtils::getProtocolType(proto_config.protocol())),
      allow_content_types_(generateAllowContentTypes(proto_config.allow_content_types())),
      allow_empty_content_type_(proto_config.allow_empty_content_type()) {
  if (request_rules_.empty() && response_rules_.empty()) {
    throw EnvoyException("thrift_to_metadata filter: Per filter configs must at least specify "
                         "either request or response rules");
  }

  if (proto_config.protocol() == envoy::extensions::filters::network::thrift_proxy::v3::TWITTER) {
    throw EnvoyException("thrift_to_metadata filter: Protocol TWITTER is not supported");
  }
}

bool FilterConfig::contentTypeAllowed(absl::string_view content_type) const {
  if (content_type.empty()) {
    return allow_empty_content_type_;
  }

  return allow_content_types_.contains(content_type);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  if (!config_->shouldParseRequestMetadata()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!config_->contentTypeAllowed(headers.getContentTypeValue())) {
    request_processing_finished_ = true;
    config_->rqstats().mismatched_content_type_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (end_stream) {
    handleAllOnMissing(config_->requestRules(), *decoder_callbacks_, request_processing_finished_);
    config_->rqstats().no_body_.inc();
    return Http::FilterHeadersStatus::Continue;
  }
  rq_trie_handler_ =
      std::make_unique<PayloadExtractor::TrieMatchHandler>(*this, config_->rqTrieRoot());
  rq_handler_ = config_->createThriftDecoderHandler(*rq_trie_handler_, true);
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!config_->shouldParseRequestMetadata() || request_processing_finished_) {
    return Http::FilterDataStatus::Continue;
  }

  // Set ``request_processing_finished_`` once we get enough data to trigger messageBegin
  // if there are no field selector rules.
  if (!processData(data, rq_buffer_, rq_handler_, *decoder_callbacks_)) {
    handleAllOnMissing(config_->requestRules(), *decoder_callbacks_, request_processing_finished_);
    config_->rqstats().invalid_thrift_body_.inc();
    return Http::FilterDataStatus::Continue;
  }

  // Handle incomplete request thrift message.
  if (end_stream && !request_processing_finished_) {
    ENVOY_LOG(trace,
              "thrift to metadata filter decodeData: handle incomplete request thrift message");

    handleAllOnMissing(config_->requestRules(), *decoder_callbacks_, request_processing_finished_);
    config_->rqstats().invalid_thrift_body_.inc();
    return Http::FilterDataStatus::Continue;
  }

  return request_processing_finished_ ? Http::FilterDataStatus::Continue
                                      : Http::FilterDataStatus::StopIterationAndBuffer;
}

void Filter::decodeComplete() {
  if (!config_->shouldParseRequestMetadata() || request_processing_finished_) {
    return;
  }

  ENVOY_LOG(trace,
            "thrift to metadata filter decodeComplete: handle incomplete request thrift message");

  // Handle incomplete request thrift message while we reach here.
  handleAllOnMissing(config_->requestRules(), *decoder_callbacks_, request_processing_finished_);
  config_->rqstats().invalid_thrift_body_.inc();
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  if (!config_->shouldParseResponseMetadata()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!config_->contentTypeAllowed(headers.getContentTypeValue())) {
    response_processing_finished_ = true;
    config_->respstats().mismatched_content_type_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (end_stream) {
    handleAllOnMissing(config_->responseRules(), *encoder_callbacks_,
                       response_processing_finished_);
    config_->respstats().no_body_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  const bool is_request = false;
  resp_trie_handler_ = std::make_unique<PayloadExtractor::TrieMatchHandler>(
      *this, config_->respTrieRoot(), is_request);
  resp_handler_ = config_->createThriftDecoderHandler(*resp_trie_handler_, is_request);
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!config_->shouldParseResponseMetadata() || response_processing_finished_) {
    return Http::FilterDataStatus::Continue;
  }

  // Set ``response_processing_finished_`` once we get enough data to trigger messageBegin
  // if there are no field selector rules.
  if (!processData(data, resp_buffer_, resp_handler_, *encoder_callbacks_)) {
    handleAllOnMissing(config_->responseRules(), *encoder_callbacks_,
                       response_processing_finished_);
    config_->respstats().invalid_thrift_body_.inc();
    return Http::FilterDataStatus::Continue;
  }

  // Handle incomplete response thrift message.
  if (end_stream && !response_processing_finished_) {
    ENVOY_LOG(trace,
              "thrift to metadata filter encodeData: handle incomplete response thrift message");
    handleAllOnMissing(config_->responseRules(), *encoder_callbacks_,
                       response_processing_finished_);
    config_->respstats().invalid_thrift_body_.inc();
    return Http::FilterDataStatus::Continue;
  }

  return response_processing_finished_ ? Http::FilterDataStatus::Continue
                                       : Http::FilterDataStatus::StopIterationAndBuffer;
}

void Filter::encodeComplete() {
  if (!config_->shouldParseResponseMetadata() || response_processing_finished_) {
    return;
  }

  ENVOY_LOG(trace,
            "thrift to metadata filter encodeComplete: handle incomplete response thrift message");

  // Handle incomplete response thrift message while we reach here.
  handleAllOnMissing(config_->responseRules(), *encoder_callbacks_, response_processing_finished_);
  config_->respstats().invalid_thrift_body_.inc();
}

bool Filter::processData(Buffer::Instance& incoming_data, Buffer::Instance& buffer,
                         ThriftDecoderHandlerPtr& handler,
                         Http::StreamFilterCallbacks& filter_callback) {
  // TODO: avoid copying payload https://github.com/envoyproxy/envoy/issues/23901
  buffer.add(incoming_data);
  bool underflow = false;
  TRY_NEEDS_AUDIT {
    // handler triggers messageBegin and other events if we get enough data.
    // And trie handler will call back via MetadataHandler interface.
    handler->onData(buffer, underflow);
    return true;
  }
  END_TRY catch (const AppException& ex) {
    // decodeComplete/encodeComplete will treat all rules as missing.
    ENVOY_LOG(error, "thrift application error: {}", ex.what());
  }
  catch (const EnvoyException& ex) {
    ENVOY_STREAM_LOG(error, "thrift error: {}", filter_callback, ex.what());
  }
  return false;
}

// PayloadExtractor::MetadataHandler
FilterStatus Filter::handleThriftMetadata(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "thrift to metadata filter: get messageBegin event. is_request: {}",
            metadata->isRequest());
  if (metadata->isRequest()) {
    processMetadata(metadata, config_->requestRules(), rq_handler_, *decoder_callbacks_,
                    config_->rqstats(), request_processing_finished_);
  } else {
    matched_field_selector_rule_ids_.clear();
    struct_map_.clear();
    processMetadata(metadata, config_->responseRules(), resp_handler_, *encoder_callbacks_,
                    config_->respstats(), response_processing_finished_);
  }

  // processMetadata matches method name for field selector rules, and
  // continue to extract thrift payload if any field selector rule is matched.
  return matched_field_selector_rule_ids_.empty() ? FilterStatus::StopIteration
                                                  : FilterStatus::Continue;
}

// PayloadExtractor::MetadataHandler
void Filter::handleOnPresent(absl::variant<absl::string_view, int64_t, double> value,
                             const std::vector<uint16_t>& rule_ids, bool is_request) {
  for (uint16_t rule_id : rule_ids) {
    if (matched_field_selector_rule_ids_.find(rule_id) == matched_field_selector_rule_ids_.end()) {
      ENVOY_LOG(trace, "rule_id {} is not matched.", rule_id);
      continue;
    }
    ENVOY_LOG(trace, "handleOnPresent rule_id {}", rule_id);

    matched_field_selector_rule_ids_.erase(rule_id);
    auto& rules = is_request ? config_->requestRules() : config_->responseRules();
    ASSERT(rule_id < rules.size());
    const Rule& rule = rules[rule_id];
    if (absl::holds_alternative<absl::string_view>(value)) {
      absl::string_view string_view_val = absl::get<absl::string_view>(value);
      if (string_view_val.empty()) {
        continue;
      }
      Protobuf::Value val;
      val.set_string_value(string_view_val);
      handleOnPresent(std::move(val), rule);
    } else if (absl::holds_alternative<int64_t>(value)) {
      Protobuf::Value val;
      val.set_number_value(absl::get<int64_t>(value));
      handleOnPresent(std::move(val), rule);
    } else {
      Protobuf::Value val;
      val.set_number_value(absl::get<double>(value));
      handleOnPresent(std::move(val), rule);
    }
  }
}

// PayloadExtractor::MetadataHandler
void Filter::handleComplete(bool is_request) {
  ENVOY_LOG(trace, "{} rules missing for field selector", matched_field_selector_rule_ids_.size());

  for (uint16_t rule_id : matched_field_selector_rule_ids_) {
    ENVOY_LOG(trace, "handling on_missing rule_id {}", rule_id);

    auto& rules = is_request ? config_->requestRules() : config_->responseRules();
    ASSERT(rule_id < rules.size());
    const Rule& rule = rules[rule_id];
    handleOnMissing(rule);
  }

  ENVOY_LOG(trace, "finalize dynamic metadata. is_request: {}", is_request);
  if (is_request) {
    config_->rqstats().success_.inc();
    finalizeDynamicMetadata(*decoder_callbacks_, request_processing_finished_);
  } else {
    config_->respstats().success_.inc();
    finalizeDynamicMetadata(*encoder_callbacks_, response_processing_finished_);
  }
}

void Filter::processMetadata(MessageMetadataSharedPtr metadata, const Rules& rules,
                             ThriftDecoderHandlerPtr& handler,
                             Http::StreamFilterCallbacks& filter_callback,
                             ThriftToMetadataStats& stats, bool& processing_finished_flag) {
  for (const auto& rule : rules) {
    if (!rule.shouldExtractMetadata()) {
      if (rule.matches(*metadata)) {
        ENVOY_LOG(trace, "rule_id {} is matched", rule.ruleId());
        matched_field_selector_rule_ids_.insert(rule.ruleId());
      }
      continue;
    }
    absl::optional<Protobuf::Value> val_opt = rule.extractValue(metadata, *handler);

    if (val_opt.has_value()) {
      handleOnPresent(std::move(val_opt).value(), rule);
    } else {
      handleOnMissing(rule);
    }
  }
  if (!matched_field_selector_rule_ids_.empty()) {
    // Continue to extract thrift payload.
    return;
  }

  // If there's no field selector rule, we'll let decoder stop iteration so
  // that we don't need to buffer the entire payload.
  // handleComplete won't be called because messageEnd won't be triggered.
  // Hence, we can safely finalize the dynamic metadata here.
  stats.success_.inc();
  finalizeDynamicMetadata(filter_callback, processing_finished_flag);
}

void Filter::handleOnPresent(Protobuf::Value&& value, const Rule& rule) {
  if (!rule.rule().has_on_present()) {
    return;
  }

  const auto& on_present_keyval = rule.rule().on_present();
  applyKeyValue(on_present_keyval.has_value() ? on_present_keyval.value() : std::move(value),
                on_present_keyval);
}

void Filter::handleOnMissing(const Rule& rule) {
  if (rule.rule().has_on_missing()) {
    applyKeyValue(rule.rule().on_missing().value(), rule.rule().on_missing());
  }
}

void Filter::handleAllOnMissing(const Rules& rules, Http::StreamFilterCallbacks& filter_callback,
                                bool& processing_finished_flag) {
  for (const auto& rule : rules) {
    handleOnMissing(rule);
  }
  finalizeDynamicMetadata(filter_callback, processing_finished_flag);
}

void Filter::applyKeyValue(Protobuf::Value value, const KeyValuePair& keyval) {
  const auto& metadata_namespace = decideNamespace(keyval.metadata_namespace());
  const auto& key = keyval.key();

  ENVOY_LOG(trace, "add metadata namespace:{} key:{}", metadata_namespace, key);

  auto& struct_proto = struct_map_[metadata_namespace];
  (*struct_proto.mutable_fields())[key] = std::move(value);
}

void Filter::finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                                     bool& processing_finished_flag) {
  ASSERT(!processing_finished_flag);
  processing_finished_flag = true;

  if (!struct_map_.empty()) {
    for (auto const& entry : struct_map_) {
      filter_callback.streamInfo().setDynamicMetadata(entry.first, entry.second);
    }
  }
}

const std::string& Filter::decideNamespace(const std::string& nspace) const {
  return nspace.empty() ? HttpFilterNames::get().ThriftToMetadata : nspace;
}

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
