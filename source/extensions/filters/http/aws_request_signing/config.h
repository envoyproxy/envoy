#pragma once

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

/**
 * Config registration for the AWS request signing filter.
 */
class AwsRequestSigningFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigning> {
public:
  AwsRequestSigningFilterFactory() : FactoryBase(HttpFilterNames::get().AwsRequestSigning) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigning&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
