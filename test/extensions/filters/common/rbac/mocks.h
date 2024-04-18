#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"

#include "gmock/gmock.h"
#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class MockEngine : public RoleBasedAccessControlEngineImpl {
public:
  MockEngine(const envoy::config::rbac::v3::RBAC& rules,
             Server::Configuration::CommonFactoryContext& context,
             const EnforcementMode mode = EnforcementMode::Enforced)
      : RoleBasedAccessControlEngineImpl(rules, ProtobufMessage::getStrictValidationVisitor(),
                                         context, mode){};

  MOCK_METHOD(bool, handleAction,
              (const Envoy::Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               StreamInfo::StreamInfo&, std::string* effective_policy_id),
              (const));

  MOCK_METHOD(bool, handleAction,
              (const Envoy::Network::Connection&, StreamInfo::StreamInfo&,
               std::string* effective_policy_id),
              (const));
};

class MockMatcherEngine : public RoleBasedAccessControlMatcherEngineImpl {
public:
  MockMatcherEngine(const xds::type::matcher::v3::Matcher& matcher,
                    Server::Configuration::ServerFactoryContext& factory_context,
                    ActionValidationVisitor& validation_visitor,
                    const EnforcementMode mode = EnforcementMode::Enforced)
      : RoleBasedAccessControlMatcherEngineImpl(matcher, factory_context, validation_visitor,
                                                mode){};

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
