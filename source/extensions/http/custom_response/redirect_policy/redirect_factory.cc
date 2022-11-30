#include "source/extensions/http/custom_response/redirect_policy/redirect_factory.h"

#include <memory>

#include "envoy/extensions/http/custom_response/redirect_policy/v3/redirect_policy.pb.h"
#include "envoy/extensions/http/custom_response/redirect_policy/v3/redirect_policy.pb.validate.h"

#include "source/extensions/http/custom_response/redirect_policy/redirect_policy.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace CustomResponse {
using RedirectPolicyProto =
    envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy;

Extensions::HttpFilters::CustomResponse::PolicySharedPtr
RedirectFactory::createPolicy(const Protobuf::Message& config,
                              Envoy::Server::Configuration::ServerFactoryContext& context,
                              Stats::StatName stats_prefix) {
  const auto& redirect_config = MessageUtil::downcastAndValidate<const RedirectPolicyProto&>(
      config, context.messageValidationVisitor());
  return std::make_shared<RedirectPolicy>(redirect_config, stats_prefix, context);
}

std::string RedirectFactory::name() const {
  return "envoy.extensions.http.custom_response.redirect_policy";
}

REGISTER_CUSTOM_RESPONSE_POLICY_FACTORY(RedirectFactory);

} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
