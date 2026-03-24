#pragma once

#include <string>

#include "envoy/extensions/filters/http/aws_eventstream_to_metadata/v3/aws_eventstream_to_metadata.pb.h"
#include "envoy/extensions/filters/http/aws_eventstream_to_metadata/v3/aws_eventstream_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamToMetadata {

/**
 * Config registration for the AWS EventStream to Metadata filter.
 */
class AwsEventstreamToMetadataConfig
    : public Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::
              AwsEventstreamToMetadata> {
public:
  AwsEventstreamToMetadataConfig()
      : ExceptionFreeFactoryBase("envoy.filters.http.aws_eventstream_to_metadata") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::
          AwsEventstreamToMetadata& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace AwsEventstreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
