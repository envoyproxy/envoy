#pragma once

#include "envoy/extensions/filters/http/rbac/v3alpha/rbac.pb.h"

#include "extensions/filters/common/rbac/utility.h"
#include "extensions/filters/http/rbac/rbac_filter.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

class MockRoleBasedAccessControlRouteSpecificFilterConfig
    : public RoleBasedAccessControlRouteSpecificFilterConfig {
public:
  MockRoleBasedAccessControlRouteSpecificFilterConfig(
      const envoy::extensions::filters::http::rbac::v3alpha::RBACPerRoute& r)
      : RoleBasedAccessControlRouteSpecificFilterConfig(r){};

  MOCK_CONST_METHOD0(engine, Filters::Common::RBAC::RoleBasedAccessControlEngineImpl&());
};

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
