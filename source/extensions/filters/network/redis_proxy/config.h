#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/upstream/upstream.h"

#include "common/common/empty_string.h"
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
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions&
          proto_config)
      : auth_username_(proto_config.auth_username()), auth_password_(proto_config.auth_password()) {
  }

  std::string authUsername(Api::Api& api) const {
    return Config::DataSource::read(auth_username_, true, api);
  }

  std::string authPassword(Api::Api& api) const {
    return Config::DataSource::read(auth_password_, true, api);
  }

  static const std::string authUsername(const Upstream::ClusterInfoConstSharedPtr info,
                                        Api::Api& api) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().RedisProxy);
    if (options) {
      return options->authUsername(api);
    }
    return EMPTY_STRING;
  }

  static const std::string authPassword(const Upstream::ClusterInfoConstSharedPtr info,
                                        Api::Api& api) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().RedisProxy);
    if (options) {
      return options->authPassword(api);
    }
    return EMPTY_STRING;
  }

private:
  envoy::config::core::v3::DataSource auth_username_;
  envoy::config::core::v3::DataSource auth_password_;
};

/**
 * Config registration for the redis proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::redis_proxy::v3::RedisProxy,
          envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions> {
public:
  RedisProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().RedisProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;

  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsTyped(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions&
          proto_config) override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
