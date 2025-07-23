#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class CustomResponseFilter;

// Base class for custom response policies.
class Policy : public std::enable_shared_from_this<Policy> {
public:
  virtual ~Policy() = default;
  virtual Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool,
                                                  CustomResponseFilter&) const PURE;

protected:
  Policy() = default;
};

using PolicySharedPtr = std::shared_ptr<const Policy>;

struct CustomResponseFilterState : public std::enable_shared_from_this<CustomResponseFilterState>,
                                   public StreamInfo::FilterState::Object {

  CustomResponseFilterState(PolicySharedPtr a_policy, absl::optional<::Envoy::Http::Code> code)
      : policy(a_policy), original_response_code(code) {}

  PolicySharedPtr policy;
  absl::optional<::Envoy::Http::Code> original_response_code;
  static constexpr absl::string_view kFilterStateName = "envoy.filters.http.custom_response";
};

struct CustomResponseMatchAction : public Matcher::ActionBase<ProtobufWkt::Any> {
  explicit CustomResponseMatchAction(PolicySharedPtr policy) : policy_(policy) {}
  const PolicySharedPtr policy_;
};

struct CustomResponseActionFactoryContext {
  Server::Configuration::ServerFactoryContext& server_;
  Stats::StatName stats_prefix_;
};

// Base class for action factories for custom response policies.
template <typename PolicyConfig>
class PolicyMatchActionFactory : public Matcher::ActionFactory<CustomResponseActionFactoryContext>,
                                 Logger::Loggable<Logger::Id::config> {
public:
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config,
                                                 CustomResponseActionFactoryContext& context,
                                                 ProtobufMessage::ValidationVisitor&) override {
    return [policy = createPolicy(config, context.server_, context.stats_prefix_)] {
      return std::make_unique<CustomResponseMatchAction>(policy);
    };
  }

  std::string category() const override { return "envoy.http.custom_response"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PolicyConfig>();
  }

protected:
  virtual PolicySharedPtr createPolicy(const Protobuf::Message& config,
                                       Envoy::Server::Configuration::ServerFactoryContext& context,
                                       Stats::StatName stats_prefix) PURE;
};

// Macro used to register factories for custom response policies
#define REGISTER_CUSTOM_RESPONSE_POLICY_FACTORY(factory)                                           \
  REGISTER_FACTORY(                                                                                \
      factory,                                                                                     \
      Matcher::ActionFactory<                                                                      \
          ::Envoy::Extensions::HttpFilters::CustomResponse::CustomResponseActionFactoryContext>)

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
