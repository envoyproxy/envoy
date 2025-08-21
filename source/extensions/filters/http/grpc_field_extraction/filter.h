#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

inline constexpr const char kFilterName[] = "envoy.filters.http.grpc_field_extraction";

class Filter : public Envoy::Http::PassThroughDecoderFilter,
               Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  Filter(const FilterConfigSharedPtr& config) : filter_config_(config) {}

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

  HandleDecodeDataStatus handleDecodeData(Envoy::Buffer::Instance& data, bool end_stream);

  void handleExtractionResult(const ExtractionResult& result);

  void rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                     absl::string_view rc_detail);

  const FilterConfigSharedPtr filter_config_;

  MessageConverterPtr request_msg_converter_ = nullptr;

  const Extractor* extractor_ = nullptr;

  bool extraction_done_ = false;
};

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
