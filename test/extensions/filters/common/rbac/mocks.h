#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "extensions/filters/common/rbac/engine_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class MockEngine : public RoleBasedAccessControlEngineImpl {
public:
  MockEngine(const envoy::config::rbac::v3::RBAC& rules)
      : RoleBasedAccessControlEngineImpl(rules){};

  MOCK_METHOD(bool, allowed,
              (const Envoy::Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo&, std::string* effective_policy_id),
              (const));

  MOCK_METHOD(bool, allowed,
              (const Envoy::Network::Connection&, const StreamInfo::StreamInfo&,
               std::string* effective_policy_id),
              (const));
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
