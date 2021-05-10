//#pragma once
//
//#include "envoy/extensions/filters/network/sip_proxy/filters/ratelimit/v3/rate_limit.pb.h"
//#include "envoy/extensions/filters/network/sip_proxy/filters/ratelimit/v3/rate_limit.pb.validate.h"
//
//#include "extensions/filters/common/ratelimit/ratelimit.h"
//#include "extensions/filters/network/sip_proxy/filters/factory_base.h"
//#include "extensions/filters/network/sip_proxy/filters/well_known_names.h"
//
//namespace Envoy {
//namespace Extensions {
//namespace SipFilters {
//namespace RateLimitFilter {
//
//using namespace Envoy::Extensions::NetworkFilters;
//
///**
// * Config registration for the rate limit filter. @see NamedSipFilterConfigFactory.
// */
//class RateLimitFilterConfig
//    : public SipProxy::SipFilters::FactoryBase<
//          envoy::extensions::filters::network::sip_proxy::filters::ratelimit::v3::RateLimit> {
//public:
//  RateLimitFilterConfig()
//      : FactoryBase(SipProxy::SipFilters::SipFilterNames::get().RATE_LIMIT) {}
//
//private:
//  SipProxy::SipFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
//      const envoy::extensions::filters::network::sip_proxy::filters::ratelimit::v3::RateLimit&
//          proto_config,
//      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
//};
//
//} // namespace RateLimitFilter
//} // namespace SipFilters
//} // namespace Extensions
//} // namespace Envoy
