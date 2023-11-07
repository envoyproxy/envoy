#include "source/extensions/filters/http/basic_auth/config.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

using envoy::extensions::filters::http::basic_auth::v3::BasicAuth;

namespace {

UserMap readHtpasswd(const std::string& htpasswd) {
  UserMap users;

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

    // TODO(wbpcode): user name in exception message would simplify the debugging. But if we think
    // it also sensitive information, we can remove it.
    if (users.contains(name)) {
      throw EnvoyException(fmt::format("basic auth: duplicate user '{}'", name));
    }

    if (!absl::StartsWith(hash, "{SHA}")) {
      throw EnvoyException(fmt::format(
          "basic auth: unsupported htpasswd format: please use {{SHA}} for '{}'", name));
    }

    hash = hash.substr(5);
    // The base64 encoded SHA1 hash is 28 bytes long
    if (hash.length() != 28) {
      throw EnvoyException(fmt::format("basic auth: invalid SHA hash length for '{}'", name));
    }

    users.insert({name, {name, hash}});
  }

  return users;
}

} // namespace

Http::FilterFactoryCb BasicAuthFilterFactory::createFilterFactoryFromProtoTyped(
    const BasicAuth& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  UserMap users =
      readHtpasswd(Config::DataSource::read(proto_config.users(), false, context.api()));
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
