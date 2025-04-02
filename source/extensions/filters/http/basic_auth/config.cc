#include "source/extensions/filters/http/basic_auth/config.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"
#include "source/extensions/hash/factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

using envoy::extensions::filters::http::basic_auth::v3::BasicAuth;
using envoy::extensions::filters::http::basic_auth::v3::BasicAuthPerRoute;

namespace {

constexpr std::string kPrefixSHA = "{SHA}";

UserMap readHtpasswd(const std::string& htpasswd) {
  UserMap users;
  absl::flat_hash_map<std::string, Hash::AlgorithmProviderSharedPtr> algorithm_providers;

  std::istringstream htpsswd_ss(htpasswd);
  std::string line;

  while (std::getline(htpsswd_ss, line)) {
    // TODO(wbpcode): should we trim the spaces or empty chars?

    // Skip empty lines and comments.
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
      throw EnvoyException("basic auth: invalid htpasswd format, username:password is expected");
    }

    std::string name = line.substr(0, colon_pos);
    std::string hash = line.substr(colon_pos + 1);

    if (name.empty() || hash.empty()) {
      throw EnvoyException("basic auth: empty user name or password");
    }

    if (users.contains(name)) {
      throw EnvoyException("basic auth: duplicate users");
    }

    std::string factory_name;
    int hash_prefix_length;

    if (absl::StartsWith(hash, kPrefixSHA)) {
      factory_name = "envoy.hash.sha1";
      hash_prefix_length = kPrefixSHA.length();
    } else {
      throw EnvoyException("basic auth: unsupported htpasswd format: please use {SHA}");
    }

    auto algorithm_provider = algorithm_providers[factory_name];
    if (algorithm_provider == nullptr) {
      auto* factory = Envoy::Config::Utility::getFactoryByName<
          Envoy::Extensions::Hash::NamedAlgorithmProviderConfigFactory>(factory_name);
      if (factory == nullptr) {
        throw EnvoyException(
            absl::StrCat("basic auth: did not find factory named '", factory_name, "'"));
      }
      algorithm_provider = factory->createAlgorithmProvider();
      algorithm_providers[factory_name] = algorithm_provider;
    }

    hash = hash.substr(hash_prefix_length);
    if (hash.length() != algorithm_provider->base64EncodedHashLength()) {
      throw EnvoyException("basic auth: invalid htpasswd format, invalid hash length");
    }

    users.insert({name, {name, hash, algorithm_providers[factory_name]}});
  }

  return users;
}

} // namespace

Http::FilterFactoryCb BasicAuthFilterFactory::createFilterFactoryFromProtoTyped(
    const BasicAuth& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  UserMap users = readHtpasswd(THROW_OR_RETURN_VALUE(
      Config::DataSource::read(proto_config.users(), false, context.serverFactoryContext().api()),
      std::string));
  FilterConfigConstSharedPtr config = std::make_unique<FilterConfig>(
      std::move(users), proto_config.forward_username_header(),
      proto_config.authentication_header(), stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BasicAuthFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
BasicAuthFilterFactory::createRouteSpecificFilterConfigTyped(
    const BasicAuthPerRoute& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  UserMap users = readHtpasswd(THROW_OR_RETURN_VALUE(
      Config::DataSource::read(proto_config.users(), true, context.api()), std::string));
  return std::make_unique<FilterConfigPerRoute>(std::move(users));
}

REGISTER_FACTORY(BasicAuthFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
