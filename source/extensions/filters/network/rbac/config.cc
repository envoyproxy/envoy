#include "source/extensions/filters/network/rbac/config.h"

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/rbac/rbac_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

static absl::Status validateFail(const std::string& header) {
  return absl::InvalidArgumentError(fmt::format("Found header({}) rule,"
                                                "not supported by RBAC network filter",
                                                header));
}

static absl::Status validatePermission(const envoy::config::rbac::v3::Permission& permission) {
  if (permission.has_header()) {
    return validateFail(permission.header().DebugString());
  }
  if (permission.has_and_rules()) {
    for (const auto& r : permission.and_rules().rules()) {
      RETURN_IF_NOT_OK(validatePermission(r));
    }
  }
  if (permission.has_or_rules()) {
    for (const auto& r : permission.or_rules().rules()) {
      RETURN_IF_NOT_OK(validatePermission(r));
    }
  }
  if (permission.has_not_rule()) {
    RETURN_IF_NOT_OK(validatePermission(permission.not_rule()));
  }
  return absl::OkStatus();
}

static absl::Status validatePrincipal(const envoy::config::rbac::v3::Principal& principal) {
  if (principal.has_header()) {
    return validateFail(principal.header().DebugString());
  }
  if (principal.has_and_ids()) {
    for (const auto& r : principal.and_ids().ids()) {
      RETURN_IF_NOT_OK(validatePrincipal(r));
    }
  }
  if (principal.has_or_ids()) {
    for (const auto& r : principal.or_ids().ids()) {
      RETURN_IF_NOT_OK(validatePrincipal(r));
    }
  }
  if (principal.has_not_id()) {
    RETURN_IF_NOT_OK(validatePrincipal(principal.not_id()));
  }
  return absl::OkStatus();
}

/**
 * Validate the RBAC rules doesn't include any header or metadata rule.
 */
static absl::Status validateRbacRules(const envoy::config::rbac::v3::RBAC& rules) {
  for (const auto& policy : rules.policies()) {
    for (const auto& permission : policy.second.permissions()) {
      RETURN_IF_NOT_OK(validatePermission(permission));
    }
    for (const auto& principal : policy.second.principals()) {
      RETURN_IF_NOT_OK(validatePrincipal(principal));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<Network::FilterFactoryCb>
RoleBasedAccessControlNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config,
    Server::Configuration::FactoryContext& context) {
  if (proto_config.has_rules()) {
    RETURN_IF_NOT_OK(validateRbacRules(proto_config.rules()));
  }
  if (proto_config.has_shadow_rules()) {
    RETURN_IF_NOT_OK(validateRbacRules(proto_config.shadow_rules()));
  }
  RoleBasedAccessControlFilterConfigSharedPtr config(
      std::make_shared<RoleBasedAccessControlFilterConfig>(proto_config, context.scope(),
                                                           context.serverFactoryContext(),
                                                           context.messageValidationVisitor()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<RoleBasedAccessControlFilter>(config));
  };
}

/**
 * Static registration for the RBAC network filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RoleBasedAccessControlNetworkFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
