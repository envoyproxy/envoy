#pragma once

#include <memory>

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/common/rbac/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class RoleBasedAccessControlEngineImpl : public RoleBasedAccessControlEngine, NonCopyable {
public:
  RoleBasedAccessControlEngineImpl(const envoy::config::rbac::v3::RBAC& rules);

  bool allowed(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info, std::string* effective_policy_id) const override;

  bool allowed(const Network::Connection& connection, const StreamInfo::StreamInfo& info,
               std::string* effective_policy_id) const override;

private:
  const bool allowed_if_matched_;

  std::map<std::string, PolicyMatcherPtr> policies_;

  Protobuf::Arena constant_arena_;
  Expr::BuilderPtr builder_;
};

using RoleBasedAccessControlEngineImplPtr = std::unique_ptr<RoleBasedAccessControlEngineImpl>;
using RoleBasedAccessControlEngineImplConstPtr =
    std::unique_ptr<const RoleBasedAccessControlEngineImpl>;

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
