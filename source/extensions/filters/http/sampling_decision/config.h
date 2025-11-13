#pragma once

#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.h"
#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SamplingDecision {

/**
 * Config registration for the sampling decision filter.
 */
class SamplingDecisionFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision> {
public:
  SamplingDecisionFilterFactory() : FactoryBase("envoy.filters.http.sampling_decision") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace SamplingDecision
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
