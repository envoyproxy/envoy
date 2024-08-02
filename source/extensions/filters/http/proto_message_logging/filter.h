#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_logging/v3/config.pb.validate.h"
#include "envoy/http/filter.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/proto_message_logging/extractor.h"
#include "source/extensions/filters/http/proto_message_logging/filter_config.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

inline constexpr const char kFilterName[] =
    "envoy.filters.http.proto_message_logging";

class Filter : public Envoy::Http::PassThroughFilter,
               Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
 public:
  explicit Filter(FilterConfig& config) : filter_config_(config) {}

  Envoy::Http::FilterHeadersStatus decodeHeaders(
      Envoy::Http::RequestHeaderMap& headers, bool end_stream) override;

  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance& data,
                                           bool end_stream) override;

  Envoy::Http::FilterHeadersStatus encodeHeaders(
      Envoy::Http::ResponseHeaderMap& headers, bool end_stream) override;

  Envoy::Http::FilterDataStatus encodeData(Envoy::Buffer::Instance& data,
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

  HandleDecodeDataStatus handleDecodeData(Envoy::Buffer::Instance& data,
                                          bool end_stream);

  HandleDecodeDataStatus handleEncodeData(Envoy::Buffer::Instance& data,
                                          bool end_stream);

  enum class MetadataType {
    RQUEST = 0,
    RESPONSE = 1,
  };

  void handleLoggingResult(const std::vector<AuditMetadata>& result,
                           MetadataType metadata_type);

  void rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status,
                     absl::string_view error_msg, absl::string_view rc_detail);

  void rejectResponse(Envoy::Grpc::Status::GrpcStatus grpc_status,
                      absl::string_view error_msg, absl::string_view rc_detail);

  const FilterConfig& filter_config_;

  Extractor* extractor_ = nullptr;

  Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverterPtr
      request_msg_converter_ = nullptr;

  Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverterPtr
      response_msg_converter_ = nullptr;

  bool request_logging_done_ = false;

  bool response_logging_done_ = false;
};

class FilterFactory
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::proto_message_logging::v3::
              ProtoMessageLoggingConfig> {
 public:
  FilterFactory() : FactoryBase("envoy.filters.http.proto_message_logging") {}

 private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_message_logging::v3::
          ProtoMessageLoggingConfig& proto_config,
      const std::string&,
      Envoy::Server::Configuration::FactoryContext&) override;
};
}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
