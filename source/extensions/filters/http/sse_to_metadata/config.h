#pragma once

#include <string>

#include "envoy/extensions/filters/http/sse_to_metadata/v3/sse_to_metadata.pb.h"
#include "envoy/extensions/filters/http/sse_to_metadata/v3/sse_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SseToMetadata {

/**
 * Config registration for the SSE to Metadata filter.
 */
class SseToMetadataConfig
    : public Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata> {
public:
  SseToMetadataConfig() : ExceptionFreeFactoryBase("envoy.filters.http.sse_to_metadata") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace SseToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
