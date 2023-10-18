#include "source/extensions/filters/http/basic_auth/config.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

namespace {

UserMap readHtpasswd(std::string htpasswd) {
  UserMap users;
  std::istringstream htpsswd_ss(htpasswd);
  std::string line;

  while (std::getline(htpsswd_ss, line)) {
    size_t colonPos = line.find(':');

    if (colonPos != std::string::npos) {
      std::string name, hash;

      name = line.substr(0, colonPos);
      hash = line.substr(colonPos + 1);

      if (name.length() == 0) {
        throw EnvoyException("invalid user name");
      }

      if (hash.find("{SHA}") == 0) {
        hash = hash.substr(5);
        if (hash.length() != 28) {
          throw EnvoyException("invalid SHA hash length");
        }

        users.insert({name, {name, hash}});
        continue;
      }
    }

    throw EnvoyException("unsupported htpasswd format: please use {SHA}");
  }

  return users;
}

} // namespace

Http::FilterFactoryCb BasicAuthFilterFactory::createFilterFactoryFromProtoTyped(
    const BasicAuth& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto htpasswd = Config::DataSource::read(proto_config.users(), false, context.api());
  auto users = readHtpasswd(htpasswd);
  FilterConfigSharedPtr config =
      std::make_shared<FilterConfig>(users, stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BasicAuthFilter>(config));
  };
}

REGISTER_FACTORY(BasicAuthFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
