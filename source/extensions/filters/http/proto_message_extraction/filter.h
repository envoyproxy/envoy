#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/proto_message_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_extraction/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor_interface.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor.h"
#include "source/extensions/filters/http/proto_message_extraction/filter_config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

inline constexpr const char kFilterName[] = "envoy.filters.http.proto_message_extraction";

class Filter : public Envoy::Http::PassThroughFilter,
               Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  explicit Filter(FilterConfig& config) : filter_config_(config) {}

  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) override;

  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance& data, bool end_stream) override;

  Envoy::Http::FilterHeadersStatus encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                                 bool end_stream) override;

  Envoy::Http::FilterDataStatus encodeData(Envoy::Buffer::Instance& data, bool end_stream) override;

private:
  struct HandleDataStatus {
    explicit HandleDataStatus(Envoy::Http::FilterDataStatus status)
        : got_messages(false), filter_status(status) {}

    // If true, the function has processed at least one message.
    bool got_messages;

    // If "got_message" is false, return this filter_status.
    Envoy::Http::FilterDataStatus filter_status;
  };

  HandleDataStatus handleDecodeData(Envoy::Buffer::Instance& data, bool end_stream);

  HandleDataStatus handleEncodeData(Envoy::Buffer::Instance& data, bool end_stream);

  void handleRequestExtractionResult(const std::vector<ExtractedMessageMetadata>& result);

  void handleResponseExtractionResult(const std::vector<ExtractedMessageMetadata>& result);

  void rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                     absl::string_view rc_detail);

  void rejectResponse(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                      absl::string_view rc_detail);

  const FilterConfig& filter_config_;

  Extractor* extractor_ = nullptr;

  Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverterPtr request_msg_converter_ =
      nullptr;

  Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverterPtr response_msg_converter_ =
      nullptr;

  bool request_extraction_done_ = false;

  bool response_extraction_done_ = false;
};

class FilterFactory : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
                          envoy::extensions::filters::http::proto_message_extraction::v3::
                              ProtoMessageExtractionConfig> {
private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_message_extraction::v3::
          ProtoMessageExtractionConfig& proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
