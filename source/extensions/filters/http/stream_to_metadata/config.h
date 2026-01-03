#pragma once

#include <string>

#include "envoy/extensions/filters/http/stream_to_metadata/v3/stream_to_metadata.pb.h"
#include "envoy/extensions/filters/http/stream_to_metadata/v3/stream_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {

/**
 * Config registration for the Stream to Metadata filter.
 */
class StreamToMetadataConfig
    : public Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata> {
public:
  StreamToMetadataConfig() : ExceptionFreeFactoryBase("envoy.filters.http.stream_to_metadata") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
