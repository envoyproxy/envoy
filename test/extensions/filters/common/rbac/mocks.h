#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "source/extensions/filters/common/rbac/engine_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class MockEngine : public RoleBasedAccessControlEngineImpl {
public:
  MockEngine(const envoy::config::rbac::v3::RBAC& rules,
             const EnforcementMode mode = EnforcementMode::Enforced)
      : RoleBasedAccessControlEngineImpl(rules, mode){};

  MOCK_METHOD(bool, handleAction,
              (const Envoy::Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               StreamInfo::StreamInfo&, std::string* effective_policy_id),
              (const));

  MOCK_METHOD(bool, handleAction,
              (const Envoy::Network::Connection&, StreamInfo::StreamInfo&,
               std::string* effective_policy_id),
              (const));
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
