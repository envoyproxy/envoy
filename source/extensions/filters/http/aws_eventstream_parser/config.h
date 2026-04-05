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
    : public Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser> {
public:
  AwsEventstreamParserConfig()
      : ExceptionFreeFactoryBase("envoy.filters.http.aws_eventstream_parser") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
