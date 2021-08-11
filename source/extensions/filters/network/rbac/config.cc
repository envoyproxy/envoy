#include "source/extensions/filters/network/rbac/config.h"

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/rbac/rbac_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

static void validateFail(const std::string& header) {
  throw EnvoyException(fmt::format("Found header({}) rule,"
                                   "not supported by RBAC network filter",
                                   header));
}

static void validatePermission(const envoy::config::rbac::v3::Permission& permission) {
  if (permission.has_header()) {
    validateFail(permission.header().DebugString());
  }
  if (permission.has_and_rules()) {
    for (const auto& r : permission.and_rules().rules()) {
      validatePermission(r);
    }
  }
  if (permission.has_or_rules()) {
    for (const auto& r : permission.or_rules().rules()) {
      validatePermission(r);
    }
  }
  if (permission.has_not_rule()) {
    validatePermission(permission.not_rule());
  }
}

static void validatePrincipal(const envoy::config::rbac::v3::Principal& principal) {
  if (principal.has_header()) {
    validateFail(principal.header().DebugString());
  }
  if (principal.has_and_ids()) {
    for (const auto& r : principal.and_ids().ids()) {
      validatePrincipal(r);
    }
  }
  if (principal.has_or_ids()) {
    for (const auto& r : principal.or_ids().ids()) {
      validatePrincipal(r);
    }
  }
  if (principal.has_not_id()) {
    validatePrincipal(principal.not_id());
  }
}

/**
 * Validate the RBAC rules doesn't include any header or metadata rule.
 */
static void validateRbacRules(const envoy::config::rbac::v3::RBAC& rules) {
  for (const auto& policy : rules.policies()) {
    for (const auto& permission : policy.second.permissions()) {
      validatePermission(permission);
    }
    for (const auto& principal : policy.second.principals()) {
      validatePrincipal(principal);
    }
  }
}

Network::FilterFactoryCb
RoleBasedAccessControlNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config,
    Server::Configuration::FactoryContext& context) {
  validateRbacRules(proto_config.rules());
  validateRbacRules(proto_config.shadow_rules());
  RoleBasedAccessControlFilterConfigSharedPtr config(
      std::make_shared<RoleBasedAccessControlFilterConfig>(proto_config, context.scope()));
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
