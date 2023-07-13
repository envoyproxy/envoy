#pragma once

#include <memory>
#include <string>

#include "source/common/protobuf/message_converter.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "envoy/http/filter.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

class Filter : public Envoy::Http::PassThroughDecoderFilter,
               Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  Filter(FilterConfig& config) : filter_config_(config) {}

  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance& data, bool end_stream) override;

  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) override;

private:
  struct HandleDecodeDataStatus {
    HandleDecodeDataStatus() : got_messages(true) {}
    explicit HandleDecodeDataStatus(Envoy::Http::FilterDataStatus status)
        : got_messages(false), filter_status(status) {}

    // If true, the function has processed at least one message.
    bool got_messages;

    // If "got_message" is false, return this filter_status.
    Envoy::Http::FilterDataStatus filter_status;
  };

  bool requireExtraction() {
    return extractor_ && request_msg_converter_;
  }

  HandleDecodeDataStatus handleDecodeData(Envoy::Buffer::Instance& data, bool end_stream);

  void handleExtractionResult();

  void rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                     absl::string_view rc_detail);

  FilterConfig& filter_config_;

  ProtobufMessage::MessageConverterPtr request_msg_converter_;

  ExtractorPtr extractor_ = nullptr;

  bool extraction_done_ = false;
};

class FilterFactory
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig> {
public:
  FilterFactory() : FactoryBase("envoy.filters.http.grpc_field_extraction") {}

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
