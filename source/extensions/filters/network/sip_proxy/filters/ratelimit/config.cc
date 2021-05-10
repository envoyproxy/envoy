//#include "extensions/filters/network/sip_proxy/filters/ratelimit/config.h"
//
//#include <chrono>
//#include <string>
//
//#include "envoy/extensions/filters/network/sip_proxy/filters/ratelimit/v3/rate_limit.pb.h"
//#include "envoy/extensions/filters/network/sip_proxy/filters/ratelimit/v3/rate_limit.pb.validate.h"
//#include "envoy/registry/registry.h"
//
//#include "common/protobuf/utility.h"
//
//#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
//#include "extensions/filters/network/sip_proxy/filters/ratelimit/ratelimit.h"
//
//namespace Envoy {
//namespace Extensions {
//namespace SipFilters {
//namespace RateLimitFilter {
//
//using namespace Envoy::Extensions::NetworkFilters;
//
//SipProxy::SipFilters::FilterFactoryCb
//RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
//    const envoy::extensions::filters::network::sip_proxy::filters::ratelimit::v3::RateLimit&
//        proto_config,
//    const std::string&, Server::Configuration::FactoryContext& context) {
//  ASSERT(!proto_config.domain().empty());
//  ConfigSharedPtr config(new Config(proto_config, context.localInfo(), context.scope(),
//                                    context.runtime(), context.clusterManager()));
//  const std::chrono::milliseconds timeout =
//      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));
//
//  return [proto_config, &context, timeout,
//          config](SipProxy::SipFilters::FilterChainFactoryCallbacks& callbacks) -> void {
//    callbacks.addDecoderFilter(std::make_shared<Filter>(
//        config, Filters::Common::RateLimit::rateLimitClient(
//                    context, proto_config.rate_limit_service().grpc_service(), timeout,
//                    proto_config.rate_limit_service().transport_api_version())));
//  };
//}
//
///**
// * Static registration for the rate limit filter. @see RegisterFactory.
// */
//REGISTER_FACTORY(RateLimitFilterConfig, SipProxy::SipFilters::NamedSipFilterConfigFactory);
//
//} // namespace RateLimitFilter
//} // namespace SipFilters
//} // namespace Extensions
//} // namespace Envoy
