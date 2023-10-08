#pragma once

#include "contrib/envoy/extensions/filters/network/generic_proxy/local_ratelimit/v3/local_ratelimit.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/local_ratelimit/v3/local_ratelimit.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace LocalRateLimit {

class LocalRateLimitFactory : public NamedFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<RateLimitConfig>();
  }
  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override { return nullptr; }
  RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) override {
    return nullptr;
  }
  bool isTerminalFilter() override { return false; }
  std::string name() const override { return "envoy.filters.generic.local_ratelimit"; }
};

} // namespace LocalRateLimit
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
