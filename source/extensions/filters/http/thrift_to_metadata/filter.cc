#include "source/extensions/filters/http/thrift_to_metadata/filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

Rule::Rule(const ProtoRule& rule) : rule_(rule) {
  if (!rule_.has_on_present() && !rule_.has_on_missing()) {
    throw EnvoyException("thrift to metadata filter: neither `on_present` nor `on_missing` set");
  }

  if (rule_.has_on_missing() && !rule_.on_missing().has_value()) {
    throw EnvoyException(
        "thrift to metadata filter: cannot specify on_missing rule with empty value");
  }

  switch (rule_.field()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::METHOD_NAME:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr metadata,
           const ThriftDecoderHandler&) -> absl::optional<ProtobufWkt::Value> {
      if (!metadata->hasMethodName()) {
        return absl::nullopt;
      }
      ProtobufWkt::Value value;
      value.set_string_value(metadata->methodName());
      return value;
    };
    break;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::PROTOCOL:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr,
           const ThriftDecoderHandler& handler) -> absl::optional<ProtobufWkt::Value> {
      ProtobufWkt::Value value;
      value.set_string_value(handler.protocolName());
      return value;
    };
    break;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::TRANSPORT:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr,
           const ThriftDecoderHandler& handler) -> absl::optional<ProtobufWkt::Value> {
      ProtobufWkt::Value value;
      value.set_string_value(handler.transportName());
      return value;
    };
    break;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::HEADER_FLAGS:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr metadata,
           const ThriftDecoderHandler&) -> absl::optional<ProtobufWkt::Value> {
      if (!metadata->hasHeaderFlags()) {
        return absl::nullopt;
      }
      ProtobufWkt::Value value;
      value.set_number_value(metadata->headerFlags());
      return value;
    };
    break;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::SEQUENCE_ID:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr metadata,
           const ThriftDecoderHandler&) -> absl::optional<ProtobufWkt::Value> {
      if (!metadata->hasSequenceId()) {
        return absl::nullopt;
      }
      ProtobufWkt::Value value;
      value.set_number_value(metadata->sequenceId());
      return value;
    };
    break;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::MESSAGE_TYPE:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr metadata,
           const ThriftDecoderHandler&) -> absl::optional<ProtobufWkt::Value> {
      if (!metadata->hasMessageType()) {
        return absl::nullopt;
      }
      ProtobufWkt::Value value;
      value.set_string_value(MessageTypeNames::get().fromType(metadata->messageType()));
      return value;
    };
    break;
  case envoy::extensions::filters::http::thrift_to_metadata::v3::REPLY_TYPE:
    protobuf_value_extracter_ =
        [](MessageMetadataSharedPtr metadata,
           const ThriftDecoderHandler&) -> absl::optional<ProtobufWkt::Value> {
      if (!metadata->hasReplyType()) {
        return absl::nullopt;
      }
      ProtobufWkt::Value value;
      value.set_string_value(ReplyTypeNames::get().fromType(metadata->replyType()));
      return value;
    };
    break;
  }
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata& proto_config,
    Stats::Scope& scope)
    : rqstats_{ALL_THRIFT_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "thrift_to_metadata.rq"))},
      respstats_{ALL_THRIFT_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "thrift_to_metadata.resp"))},
      request_rules_(generateRules(proto_config.request_rules())),
      response_rules_(generateRules(proto_config.response_rules())),
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

  rq_handler_ = config_->createThriftDecoderHandler(*this, true);
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!config_->shouldParseRequestMetadata() || request_processing_finished_) {
    return Http::FilterDataStatus::Continue;
  }

  // Set ``request_processing_finished_`` once we get enough data to trigger messageBegin.
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

  resp_handler_ = config_->createThriftDecoderHandler(*this, false);
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!config_->shouldParseResponseMetadata() || response_processing_finished_) {
    return Http::FilterDataStatus::Continue;
  }

  // Set ``response_processing_finished_`` once we get enough data to trigger messageBegin.
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
    // handler triggers messageBegin if we get enough data.
    handler->onData(buffer, underflow);
    return true;
  }
  END_TRY catch (const AppException& ex) {
    ENVOY_LOG(error, "thrift application error: {}", ex.what());
  }
  catch (const EnvoyException& ex) {
    ENVOY_STREAM_LOG(error, "thrift error: {}", filter_callback, ex.what());
  }
  return false;
}

FilterStatus Filter::messageBegin(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "thrift to metadata filter: get messageBegin event. is_request: {}",
            metadata->isRequest());
  if (metadata->isRequest()) {
    processMetadata(metadata, config_->requestRules(), rq_handler_, *decoder_callbacks_,
                    config_->rqstats(), request_processing_finished_);
  } else {
    processMetadata(metadata, config_->responseRules(), resp_handler_, *encoder_callbacks_,
                    config_->respstats(), response_processing_finished_);
  }

  // We don't need to process the rest of the message.
  return FilterStatus::StopIteration;
}

void Filter::processMetadata(MessageMetadataSharedPtr metadata, const Rules& rules,
                             ThriftDecoderHandlerPtr& handler,
                             Http::StreamFilterCallbacks& filter_callback,
                             ThriftToMetadataStats& stats, bool& processing_finished_flag) {
  StructMap struct_map;
  for (const auto& rule : rules) {
    absl::optional<ProtobufWkt::Value> val_opt = rule.extract_value(metadata, *handler);

    if (val_opt.has_value()) {
      handleOnPresent(std::move(val_opt).value(), rule, struct_map);
    } else {
      handleOnMissing(rule, struct_map);
    }
  }
  stats.success_.inc();
  finalizeDynamicMetadata(filter_callback, struct_map, processing_finished_flag);
}

void Filter::handleOnPresent(ProtobufWkt::Value&& value, const Rule& rule, StructMap& struct_map) {
  if (!rule.rule().has_on_present()) {
    return;
  }

  const auto& on_present_keyval = rule.rule().on_present();
  applyKeyValue(on_present_keyval.has_value() ? on_present_keyval.value() : std::move(value),
                on_present_keyval, struct_map);
}

void Filter::handleOnMissing(const Rule& rule, StructMap& struct_map) {
  if (rule.rule().has_on_missing()) {
    applyKeyValue(rule.rule().on_missing().value(), rule.rule().on_missing(), struct_map);
  }
}

void Filter::handleAllOnMissing(const Rules& rules, Http::StreamFilterCallbacks& filter_callback,
                                bool& processing_finished_flag) {
  StructMap struct_map;
  for (const auto& rule : rules) {
    handleOnMissing(rule, struct_map);
  }
  finalizeDynamicMetadata(filter_callback, struct_map, processing_finished_flag);
}

void Filter::applyKeyValue(ProtobufWkt::Value value, const KeyValuePair& keyval,
                           StructMap& struct_map) {
  const auto& metadata_namespace = decideNamespace(keyval.metadata_namespace());
  const auto& key = keyval.key();

  ENVOY_LOG(trace, "add metadata namespace:{} key:{}", metadata_namespace, key);

  auto& struct_proto = struct_map[metadata_namespace];
  (*struct_proto.mutable_fields())[key] = std::move(value);
}

void Filter::finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                                     const StructMap& struct_map, bool& processing_finished_flag) {
  ASSERT(!processing_finished_flag);
  processing_finished_flag = true;

  if (!struct_map.empty()) {
    for (auto const& entry : struct_map) {
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
