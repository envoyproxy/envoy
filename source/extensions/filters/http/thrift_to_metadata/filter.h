#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/thrift_to_metadata/v3/thrift_to_metadata.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/network/thrift_proxy/auto_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/auto_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/decoder.h"
#include "source/extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/header_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/passthrough_decoder_event_handler.h"
#include "source/extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

/**
 * All stats for the Thrift to Metadata filter. @see stats_macros.h
 */
#define ALL_THRIFT_TO_METADATA_FILTER_STATS(COUNTER)                                               \
  COUNTER(success)                                                                                 \
  COUNTER(mismatched_content_type)                                                                 \
  COUNTER(no_body)                                                                                 \
  COUNTER(invalid_thrift_body)

/**
 * Wrapper struct for Thrift to Metadata filter stats. @see stats_macros.h
 */
struct ThriftToMetadataStats {
  ALL_THRIFT_TO_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using ProtoRule = envoy::extensions::filters::http::thrift_to_metadata::v3::Rule;
using KeyValuePair = envoy::extensions::filters::http::thrift_to_metadata::v3::KeyValuePair;
using StructMap = absl::flat_hash_map<std::string, ProtobufWkt::Struct>;
using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;

class ThriftDecoderHandler;
using ThriftMetadataToProtobufValue = std::function<absl::optional<ProtobufWkt::Value>(
    MessageMetadataSharedPtr, const ThriftDecoderHandler&)>;

class Rule {
public:
  Rule(const ProtoRule& rule);
  const ProtoRule& rule() const { return rule_; }
  absl::optional<ProtobufWkt::Value> extract_value(MessageMetadataSharedPtr metadata,
                                                   const ThriftDecoderHandler& handler) const {
    return protobuf_value_extracter_(metadata, handler);
  }

private:
  const ProtoRule rule_;
  ThriftMetadataToProtobufValue protobuf_value_extracter_{};
};

using Rules = std::vector<Rule>;

class Filter;

// ThriftDecoderHandler is a wrapper around the Decoder class and delegate thrift event to handler.
class ThriftDecoderHandler : public DecoderCallbacks {
public:
  ThriftDecoderHandler(DecoderEventHandler& handler, bool is_request, TransportPtr transport,
                       ProtocolPtr protocol)
      : handler_(handler), is_request_(is_request), transport_(std::move(transport)),
        protocol_(std::move(protocol)),
        decoder_(std::make_unique<Decoder>(*transport_, *protocol_, *this)) {}

  FilterStatus onData(Buffer::Instance& data, bool& buffer_underflow) {
    return decoder_->onData(data, buffer_underflow);
  }

  const std::string& transportName() const { return transport_->name(); }
  const std::string& protocolName() const { return protocol_->name(); }

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return handler_; }
  // TODO: currently the thrift decoder with passthrough enabling relies on frame size in transport.
  // However, unframed transport does not have frame size.
  // In theory we could know the frame size by looking at the end_stream flag in the future.
  bool passthroughEnabled() const override { return false; }
  bool isRequest() const override { return is_request_; }
  bool headerKeysPreserveCase() const override { return false; }

private:
  DecoderEventHandler& handler_;
  bool is_request_;
  TransportPtr transport_;
  ProtocolPtr protocol_;
  DecoderPtr decoder_;
};

using ThriftDecoderHandlerPtr = std::unique_ptr<ThriftDecoderHandler>;

/**
 * Configuration for the Thrift to Metadata filter.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata&
                   proto_config,
               Stats::Scope& scope);

  ThriftDecoderHandlerPtr createThriftDecoderHandler(Filter& filter, bool is_request) {
    return std::make_unique<ThriftDecoderHandler>(filter, is_request, createTransport(),
                                                  createProtocol());
  }

  ThriftToMetadataStats& rqstats() { return rqstats_; }
  ThriftToMetadataStats& respstats() { return respstats_; }
  bool shouldParseRequestMetadata() const { return !request_rules_.empty(); }
  bool shouldParseResponseMetadata() const { return !response_rules_.empty(); }
  const Rules& requestRules() const { return request_rules_; }
  const Rules& responseRules() const { return response_rules_; }
  bool contentTypeAllowed(absl::string_view) const;

private:
  using ProtobufRepeatedRule = Protobuf::RepeatedPtrField<ProtoRule>;
  Rules generateRules(const ProtobufRepeatedRule& proto_rules) const {
    Rules rules;
    for (const auto& rule : proto_rules) {
      rules.emplace_back(rule);
    }
    return rules;
  }

  absl::flat_hash_set<std::string> generateAllowContentTypes(
      const Protobuf::RepeatedPtrField<std::string>& proto_allow_content_types) const {
    if (proto_allow_content_types.empty()) {
      return {Http::Headers::get().ContentTypeValues.Thrift};
    }

    absl::flat_hash_set<std::string> allow_content_types;
    for (const auto& allowed_content_type : proto_allow_content_types) {
      allow_content_types.insert(allowed_content_type);
    }
    return allow_content_types;
  }

  TransportPtr createTransport() {
    return NamedTransportConfigFactory::getFactory(transport_).createTransport();
  }

  ProtocolPtr createProtocol() {
    return NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
  }

  ThriftToMetadataStats rqstats_;
  ThriftToMetadataStats respstats_;
  const Rules request_rules_;
  const Rules response_rules_;
  const TransportType transport_;
  const ProtocolType protocol_;
  const absl::flat_hash_set<std::string> allow_content_types_;
  const bool allow_empty_content_type_;
};

/**
 * HTTP Thrift to Metadata Filter.
 */
class Filter : public Http::PassThroughFilter,
               public PassThroughDecoderEventHandler,
               Logger::Loggable<Logger::Id::filter> {
public:
  Filter(std::shared_ptr<FilterConfig> config) : config_(config){};
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeComplete() override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeComplete() override;

  // PassThroughDecoderEventHandler
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;

private:
  // Return false if the decoder throws an exception.
  bool processData(Buffer::Instance& data, Buffer::Instance& buffer,
                   ThriftDecoderHandlerPtr& handler, Http::StreamFilterCallbacks& filter_callback);
  void processMetadata(MessageMetadataSharedPtr metadata, const Rules& rules,
                       ThriftDecoderHandlerPtr& handler,
                       Http::StreamFilterCallbacks& filter_callback, ThriftToMetadataStats& stats,
                       bool& processing_finished_flag);
  void handleOnPresent(ProtobufWkt::Value&& value, const Rule& rule, StructMap& struct_map);

  void handleOnMissing(const Rule& rule, StructMap& struct_map);
  void handleAllOnMissing(const Rules& rules, Http::StreamFilterCallbacks& filter_callback,
                          bool& processing_finished_flag);
  void applyKeyValue(ProtobufWkt::Value value, const KeyValuePair& keyval, StructMap& struct_map);
  void finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                               const StructMap& struct_map, bool& processing_finished_flag);
  const std::string& decideNamespace(const std::string& nspace) const;

  std::shared_ptr<FilterConfig> config_;
  bool request_processing_finished_{false};
  bool response_processing_finished_{false};
  ThriftDecoderHandlerPtr rq_handler_;
  ThriftDecoderHandlerPtr resp_handler_;
  Buffer::OwnedImpl rq_buffer_;
  Buffer::OwnedImpl resp_buffer_;
};

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
