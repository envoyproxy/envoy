#pragma once

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

inline constexpr const char kFilterName[] = "envoy.filters.http.proto_api_scrubber";

/**
 * A filter that supports scrubbing of request and response protobuf payloads based on configured
 * restrictions.
 */
class ProtoApiScrubberFilter : public Http::PassThroughFilter,
                               Logger::Loggable<Logger::Id::filter> {
public:
  explicit ProtoApiScrubberFilter(const ProtoApiScrubberFilterConfig&) {};

  Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

private:
  // Rejects requests and sends local reply back to the client.
  void rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                     absl::string_view rc_detail);

  bool is_valid_grpc_request_ = false;

  // Request message converter which converts Envoy Buffer data to StreamMessage (for scrubbing) and
  // vice-versa.
  GrpcFieldExtraction::MessageConverterPtr request_msg_converter_{nullptr};
};

class FilterFactory : public Common::FactoryBase<ProtoApiScrubberConfig> {
private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProtoApiScrubberConfig& proto_config, const std::string&,
                                    Server::Configuration::FactoryContext&) override;
};
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
