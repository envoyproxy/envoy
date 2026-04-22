#pragma once

#include <string>
#include <utility>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace {
absl::flat_hash_map<
    envoy::config::core::v3::Address,
    std::pair<envoy::config::core::v3::DataSource, envoy::config::core::v3::DataSource>,
    MessageUtil, MessageUtil>
generateCredentials(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions&
        proto_config) {
  absl::flat_hash_map<
      envoy::config::core::v3::Address,
      std::pair<envoy::config::core::v3::DataSource, envoy::config::core::v3::DataSource>,
      MessageUtil, MessageUtil>
      credentials;
  for (const auto& credential : proto_config.credentials()) {
    credentials.insert(
        std::make_pair(credential.address(),
                       std::make_pair(credential.auth_username(), credential.auth_password())));
  }
  return credentials;
}
} // namespace

class ProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  struct Credentials {
    std::string username;
    std::string password;
  };

  ProtocolOptionsConfigImpl(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions&
          proto_config)
      : auth_username_(proto_config.auth_username()), auth_password_(proto_config.auth_password()),
        credentials_(generateCredentials(proto_config)) {
    proto_config_.MergeFrom(proto_config);
  }

  // Returns a <username, password> credential pair for the given host. If host is null, then
  // the default credentials are returned.
  Credentials authCredentials(Api::Api& api, Upstream::HostConstSharedPtr host) const {
    const auto credential = getCredential(host);
    const envoy::config::core::v3::DataSource& auth_username =
        credential.ok() ? credential->first : auth_username_;
    const envoy::config::core::v3::DataSource& auth_password =
        credential.ok() ? credential->second : auth_password_;
    return Credentials{
        THROW_OR_RETURN_VALUE(Config::DataSource::read(auth_username, true, api), std::string),
        THROW_OR_RETURN_VALUE(Config::DataSource::read(auth_password, true, api), std::string)};
  }

  // Returns a <username, password> credential pair for the default credentials for the cluster.
  static const Credentials authCredentials(const Upstream::ClusterInfoConstSharedPtr info,
                                           Api::Api& api) {
    return authCredentials(info, api, nullptr);
  }

  // Returns a <username, password> credential pair for the given host. If host is null, then
  // the default credentials for the cluster are returned.
  static const Credentials authCredentials(const Upstream::ClusterInfoConstSharedPtr info,
                                           Api::Api& api, Upstream::HostConstSharedPtr host) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().RedisProxy);
    if (options) {
      return options->authCredentials(api, host);
    }
    return Credentials{EMPTY_STRING, EMPTY_STRING};
  }

  // Returns the default username for the cluster.
  static const std::string authUsername(const Upstream::ClusterInfoConstSharedPtr info,
                                        Api::Api& api) {
    return authCredentials(info, api).username;
  }

  // Returns the default password for the cluster.
  static const std::string authPassword(const Upstream::ClusterInfoConstSharedPtr info,
                                        Api::Api& api) {
    return authCredentials(info, api).password;
  }

  static absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam>
  awsIamConfig(const Upstream::ClusterInfoConstSharedPtr info) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().RedisProxy);
    if (options && options->proto_config_.has_aws_iam()) {
      return options->proto_config_.aws_iam();
    }
    return absl::nullopt;
  }

private:
  absl::StatusOr<
      std::pair<envoy::config::core::v3::DataSource, envoy::config::core::v3::DataSource>>
  getCredential(Upstream::HostConstSharedPtr host) const {
    // The addresses in `credentials_` are unresolved. In order to compare them
    // to `host`, we need to look at `host->hostname()` which is the unresolved
    // value, and then separately look at the port.
    if (host != nullptr && host->address() != nullptr && host->address()->ip() != nullptr) {
      for (const auto& [address, credential] : credentials_) {
        // If host->hostname() is empty, then the host is not configured via DNS,
        // so fall back to the IP address.
        if (host->hostname().empty()) {
          if (host->address()->ip()->addressAsString() == address.socket_address().address() &&
              host->address()->ip()->port() == address.socket_address().port_value()) {
            return credential;
          }
        } else if (host->hostname() == address.socket_address().address() &&
                   host->address()->ip()->port() == address.socket_address().port_value()) {
          return credential;
        }
      }
    }
    return absl::NotFoundError("Credential not found");
  }

  // The default username and password.
  const envoy::config::core::v3::DataSource auth_username_;
  const envoy::config::core::v3::DataSource auth_password_;

  envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions proto_config_;

  // Credential map from `address` to a username/password pair.
  const absl::flat_hash_map<
      envoy::config::core::v3::Address,
      std::pair<envoy::config::core::v3::DataSource, envoy::config::core::v3::DataSource>,
      MessageUtil, MessageUtil>
      credentials_;
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

  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsTyped(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions&
          proto_config,
      Server::Configuration::ProtocolOptionsFactoryContext&) override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
