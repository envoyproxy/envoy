#include "source/extensions/filters/http/basic_auth/config.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

using envoy::extensions::filters::http::basic_auth::v3::BasicAuth;

namespace {

UserMapConstPtr readHtpasswd(const std::string& htpasswd) {
  std::unique_ptr<absl::flat_hash_map<std::string, User>> users =
      std::make_unique<absl::flat_hash_map<std::string, User>>();
  std::istringstream htpsswd_ss(htpasswd);
  std::string line;

  while (std::getline(htpsswd_ss, line)) {
    const size_t colon_pos = line.find(':');

    if (colon_pos != std::string::npos) {
      std::string name = line.substr(0, colon_pos);
      std::string hash = line.substr(colon_pos + 1);

      if (name.empty()) {
        throw EnvoyException("basic auth: invalid user name");
      }

      if (absl::StartsWith(hash, "{SHA}")) {
        hash = hash.substr(5);
        // The base64 encoded SHA1 hash is 28 bytes long
        if (hash.length() != 28) {
          throw EnvoyException("basic auth: invalid SHA hash length");
        }

        users->insert({name, {name, hash}});
        continue;
      }
    }

    throw EnvoyException("basic auth: unsupported htpasswd format: please use {SHA}");
  }

  return users;
}

} // namespace

Http::FilterFactoryCb BasicAuthFilterFactory::createFilterFactoryFromProtoTyped(
    const BasicAuth& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  const std::string htpasswd = Config::DataSource::read(proto_config.users(), false, context.api());
  UserMapConstPtr users = readHtpasswd(htpasswd);
  FilterConfigConstSharedPtr config =
      std::make_unique<FilterConfig>(std::move(users), stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BasicAuthFilter>(config));
  };
}

REGISTER_FACTORY(BasicAuthFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
