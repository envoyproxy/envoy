#include "source/extensions/filters/http/basic_auth/config.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

using envoy::extensions::filters::http::basic_auth::v3::BasicAuth;
using envoy::extensions::filters::http::basic_auth::v3::BasicAuthPerRoute;

namespace {

absl::StatusOr<UserMap> readHtpasswd(const std::string& htpasswd) {
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
      return absl::InvalidArgumentError(
          "basic auth: invalid htpasswd format, username:password is expected");
    }

    std::string name = line.substr(0, colon_pos);
    std::string hash = line.substr(colon_pos + 1);

    if (name.empty() || hash.empty()) {
      return absl::InvalidArgumentError("basic auth: empty user name or password");
    }

    if (users.contains(name)) {
      return absl::InvalidArgumentError("basic auth: duplicate users");
    }

    if (!absl::StartsWith(hash, "{SHA}")) {
      return absl::InvalidArgumentError(
          "basic auth: unsupported htpasswd format: please use {SHA}");
    }

    hash = hash.substr(5);
    // The base64 encoded SHA1 hash is 28 bytes long
    if (hash.length() != 28) {
      return absl::InvalidArgumentError(
          "basic auth: invalid htpasswd format, invalid SHA hash length");
    }

    users.insert({name, {name, hash}});
  }

  return users;
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb> BasicAuthFilterFactory::createFilterFactoryFromProtoTyped(
    const BasicAuth& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto htpasswd_or =
      Config::DataSource::read(proto_config.users(), false, context.serverFactoryContext().api());
  RETURN_IF_NOT_OK_REF(htpasswd_or.status());
  auto users_or = readHtpasswd(htpasswd_or.value());
  RETURN_IF_NOT_OK_REF(users_or.status());
  FilterConfigConstSharedPtr config = std::make_unique<FilterConfig>(
      std::move(users_or.value()), proto_config.forward_username_header(),
      proto_config.authentication_header(), proto_config.allow_missing(),
      proto_config.emit_dynamic_metadata(), stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BasicAuthFilter>(config));
  };
}

absl::StatusOr<Http::FilterFactoryCb> BasicAuthFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const BasicAuth& proto_config, const std::string& stats_prefix,
    Server::Configuration::ServerFactoryContext& context) {
  auto htpasswd_or = Config::DataSource::read(proto_config.users(), false, context.api());
  RETURN_IF_NOT_OK_REF(htpasswd_or.status());
  auto users_or = readHtpasswd(htpasswd_or.value());
  RETURN_IF_NOT_OK_REF(users_or.status());
  FilterConfigConstSharedPtr config = std::make_unique<FilterConfig>(
      std::move(users_or.value()), proto_config.forward_username_header(),
      proto_config.authentication_header(), proto_config.allow_missing(),
      proto_config.emit_dynamic_metadata(), stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BasicAuthFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
BasicAuthFilterFactory::createRouteSpecificFilterConfigTyped(
    const BasicAuthPerRoute& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  auto htpasswd_or = Config::DataSource::read(proto_config.users(), true, context.api());
  RETURN_IF_NOT_OK_REF(htpasswd_or.status());
  auto users_or = readHtpasswd(htpasswd_or.value());
  RETURN_IF_NOT_OK_REF(users_or.status());
  return std::make_unique<FilterConfigPerRoute>(std::move(users_or.value()));
}

REGISTER_FACTORY(BasicAuthFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
