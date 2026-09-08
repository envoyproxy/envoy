#pragma once

#include <string>

#include "envoy/extensions/filters/http/aws_eventstream_parser/v3/aws_eventstream_parser.pb.h"
#include "envoy/extensions/filters/http/aws_eventstream_parser/v3/aws_eventstream_parser.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamParser {

/**
 * Config registration for the AWS EventStream Parser filter.
 */
class AwsEventstreamParserConfig
    : public Extensions::HttpFilters::Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser> {
public:
  AwsEventstreamParserConfig() : UnifiedFactoryBase("envoy.filters.http.aws_eventstream_parser") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
