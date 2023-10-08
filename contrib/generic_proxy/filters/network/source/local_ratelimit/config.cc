#include "contrib/generic_proxy/filters/network/source/local_ratelimit/config.h"

#include "envoy/registry/registry.h"

#include "contrib/generic_proxy/filters/network/source/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace LocalRateLimit {

FilterFactoryCb LocalRateLimitFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  const auto& filter_config = MessageUtil::downcastAndValidate<const RateLimitConfig&>(
      config, context.messageValidationVisitor());
  DecoderFilterSharedPtr rate_limit_filter =
      std::make_shared<LocalRateLimitFilter>(filter_config, context);

  return [rate_limit_filter](FilterChainFactoryCallbacks& callbacks) {
    callbacks.addDecoderFilter(rate_limit_filter);
  };
}

REGISTER_FACTORY(LocalRateLimitFactory, NamedFilterConfigFactory);

} // namespace LocalRateLimit
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
