#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerConfigProviderImpl::SamplerConfigProviderImpl(
    Server::Configuration::TracerFactoryContext& /*context*/,
    const envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig& config)
    : sampler_config_(config.root_spans_per_minute()) {}

const SamplerConfig& SamplerConfigProviderImpl::getSamplerConfig() const { return sampler_config_; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
