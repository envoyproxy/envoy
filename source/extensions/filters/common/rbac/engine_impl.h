#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/common/rbac/engine.h"
#include "source/extensions/filters/common/rbac/matchers.h"
#include "source/extensions/filters/common/rbac/matching/data.h"

#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class DynamicMetadataKeys {
public:
  const std::string ShadowEffectivePolicyIdField{"shadow_effective_policy_id"};
  const std::string ShadowEngineResultField{"shadow_engine_result"};
  const std::string EngineResultAllowed{"allowed"};
  const std::string EngineResultDenied{"denied"};
  const std::string AccessLogKey{"access_log_hint"};
  const std::string CommonNamespace{"envoy.common"};
};

using DynamicMetadataKeysSingleton = ConstSingleton<DynamicMetadataKeys>;

enum class EnforcementMode { Enforced, Shadow };

struct ActionContext {};

class Action : public Envoy::Matcher::ActionBase<envoy::config::rbac::v3::Action> {
public:
  Action(const std::string& name, bool allow) : name_(name), allow_(allow) {}

  const std::string& name() const { return name_; }
  bool allow() const { return allow_; }

private:
  const std::string name_;
  const bool allow_;
};

class ActionFactory : public Envoy::Matcher::ActionFactory<ActionContext> {
public:
  Envoy::Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, ActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;
  std::string name() const override { return "action"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::rbac::v3::Action>();
  }
};

using ActionValidationVisitor = Envoy::Matcher::MatchTreeValidationVisitor<Matching::MatchingData>;

class NetworkActionValidationVisitor : public ActionValidationVisitor {
public:
  absl::Status performDataInputValidation(
      const Envoy::Matcher::DataInputFactory<Matching::MatchingData>& data_input,
      absl::string_view type_url) override;
};

class HttpActionValidationVisitor : public ActionValidationVisitor {
public:
  absl::Status performDataInputValidation(
      const Envoy::Matcher::DataInputFactory<Matching::MatchingData>& data_input,
      absl::string_view type_url) override;
};

class RoleBasedAccessControlEngineImpl : public RoleBasedAccessControlEngine, NonCopyable {
public:
  RoleBasedAccessControlEngineImpl(const envoy::config::rbac::v3::RBAC& rules,
                                   ProtobufMessage::ValidationVisitor& validation_visitor,
                                   const EnforcementMode mode = EnforcementMode::Enforced);

  bool handleAction(const Network::Connection& connection,
                    const Envoy::Http::RequestHeaderMap& headers, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) const override;

  bool handleAction(const Network::Connection& connection, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) const override;

private:
  // Checks whether the request matches any policies
  bool checkPolicyMatch(const Network::Connection& connection, const StreamInfo::StreamInfo& info,
                        const Envoy::Http::RequestHeaderMap& headers,
                        std::string* effective_policy_id) const;

  const envoy::config::rbac::v3::RBAC::Action action_;
  const EnforcementMode mode_;

  std::map<std::string, std::unique_ptr<PolicyMatcher>> policies_;

  Protobuf::Arena constant_arena_;
  Expr::BuilderPtr builder_;
};

class RoleBasedAccessControlMatcherEngineImpl : public RoleBasedAccessControlEngine, NonCopyable {
public:
  RoleBasedAccessControlMatcherEngineImpl(
      const xds::type::matcher::v3::Matcher& matcher,
      Server::Configuration::ServerFactoryContext& factory_context,
      ActionValidationVisitor& validation_visitor,
      const EnforcementMode mode = EnforcementMode::Enforced);

  bool handleAction(const Network::Connection& connection,
                    const Envoy::Http::RequestHeaderMap& headers, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) const override;

  bool handleAction(const Network::Connection& connection, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) const override;

private:
  Envoy::Matcher::MatchTreeSharedPtr<Matching::MatchingData> matcher_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
