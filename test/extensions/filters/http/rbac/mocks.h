#pragma once

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

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
      const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& r,
      Server::Configuration::ServerFactoryContext& context)
      : RoleBasedAccessControlRouteSpecificFilterConfig(
            r, context, ProtobufMessage::getStrictValidationVisitor()){};

  MOCK_METHOD(Filters::Common::RBAC::RoleBasedAccessControlEngine&, engine, (), (const));
};

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
