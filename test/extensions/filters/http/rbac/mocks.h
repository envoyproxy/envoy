#pragma once

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"

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
      const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& r)
      : RoleBasedAccessControlRouteSpecificFilterConfig(r){};

  MOCK_METHOD(Filters::Common::RBAC::RoleBasedAccessControlEngineImpl&, engine, (), (const));
};

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
