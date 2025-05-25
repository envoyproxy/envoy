#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
static constexpr uint16_t AwsIamDefaultExpiration = 60;
constexpr char DEFAULT_SERVICE_NAME[] = "elasticache";
} // namespace

class ProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions&
          proto_config)
      : auth_username_(proto_config.auth_username()), auth_password_(proto_config.auth_password()) {
    proto_config_.MergeFrom(proto_config);
  }

  bool hasAwsIam() const { return proto_config_.has_aws_iam(); }

  static bool hasAwsIam(const Upstream::ClusterInfoConstSharedPtr info) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().RedisProxy);
    if (options) {
      return options->hasAwsIam();
    }
    return false;
  }

  static absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> awsIam(const Upstream::ClusterInfoConstSharedPtr info) {
    auto options = info->extensionProtocolOptionsTyped<ProtocolOptionsConfigImpl>(
        NetworkFilterNames::get().RedisProxy);
    if (options && options->hasAwsIam()) {
      return options->awsIam();
    }
    return absl::nullopt;
  }

  envoy::extensions::filters::network::redis_proxy::v3::AwsIam awsIam() const {
    return proto_config_.aws_iam();
  }

  std::string authUsername(Api::Api& api) const {
    return THROW_OR_RETURN_VALUE(Config::DataSource::read(auth_username_, true, api), std::string);
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

  std::string authPassword(Api::Api& api) const {
    if (proto_config_.has_aws_iam()) {
      return {};
    }
    return THROW_OR_RETURN_VALUE(Config::DataSource::read(auth_password_, true, api), std::string);
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
  envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions proto_config_;
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
