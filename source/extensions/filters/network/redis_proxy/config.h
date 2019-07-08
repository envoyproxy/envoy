#pragma once

#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"
#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.validate.h"

#include "common/config/datasource.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class ProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::config::filter::network::redis_proxy::v2::RedisProtocolOptions& proto_config)
      : auth_password_(proto_config.auth_password()) {}

  std::string auth_password(Api::Api& api) const {
    return Config::DataSource::read(auth_password_, true, api);
  }

  const envoy::api::v2::core::DataSource& auth_password_datasource() const {
    return auth_password_;
  }

private:
  envoy::api::v2::core::DataSource auth_password_;
};

/**
 * Config registration for the redis proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::redis_proxy::v2::RedisProxy,
          envoy::config::filter::network::redis_proxy::v2::RedisProtocolOptions> {
public:
  RedisProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().RedisProxy) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;

  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsTyped(
      const envoy::config::filter::network::redis_proxy::v2::RedisProtocolOptions& proto_config)
      override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
