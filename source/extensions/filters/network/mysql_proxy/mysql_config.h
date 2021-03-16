#pragma once

#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.validate.h"

#include "common/config/datasource.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProtocolOptions&
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
        NetworkFilterNames::get().MySQLProxy);
    if (options) {
      return options->authUsername(api);
    }
    return EMPTY_STRING;
  }

  static const std::string authPassword(const Upstream::ClusterInfoConstSharedPtr info,
                                        Api::Api& api) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().MySQLProxy);
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
 * Config registration for the MySQL proxy filter.
 */
class MySQLConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy,
          envoy::extensions::filters::network::mysql_proxy::v3::MySQLProtocolOptions> {
public:
  MySQLConfigFactory() : FactoryBase(NetworkFilterNames::get().MySQLProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsTyped(
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProtocolOptions&
          proto_config,
      Server::Configuration::ProtocolOptionsFactoryContext&) override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
