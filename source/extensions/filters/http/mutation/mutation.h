#pragma once

#include <memory>

#include "envoy/extensions/filters/http/mutation/v3/mutation.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mutation {

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::mutation::v3::Mutation& config)
      : failure_mode_allow_(config.failure_mode_allow()) {}

  bool failureModeAllow() const { return failure_mode_allow_; }

private:
  const bool failure_mode_allow_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class Filter : public Logger::Loggable<Logger::Id::filter>, public Http::StreamFilter {
public:
  Filter(const FilterConfigSharedPtr& config, Grpc::RawAsyncClientPtr&& client)
      : config_(config), grpc_client_(std::move(client)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  FilterConfigSharedPtr config_;
  Grpc::RawAsyncClientPtr grpc_client_;
};

} // namespace Mutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy