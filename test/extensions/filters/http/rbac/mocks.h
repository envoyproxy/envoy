#pragma once

#include "extensions/filters/common/rbac/config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

class MockRoleBasedAccessControlRouteSpecificFilterConfig
    : public Filters::Common::RBAC::RoleBasedAccessControlRouteSpecificFilterConfig {
public:
  MockRoleBasedAccessControlRouteSpecificFilterConfig(
      const envoy::config::filter::http::rbac::v2::RBACPerRoute& r)
      : Filters::Common::RBAC::RoleBasedAccessControlRouteSpecificFilterConfig(r){};

  MOCK_CONST_METHOD0(engine, Filters::Common::RBAC::RoleBasedAccessControlEngineImpl&());
};

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
