#include "source/extensions/http/custom_response/local_response_policy/local_response_factory.h"

#include "source/extensions/http/custom_response/local_response_policy/local_response_policy.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace CustomResponse {

using LocalResponsePolicyProto =
    envoy::extensions::http::custom_response::local_response_policy::v3::LocalResponsePolicy;

Extensions::HttpFilters::CustomResponse::PolicySharedPtr
LocalResponseFactory::createPolicy(const Protobuf::Message& config,
                                   Envoy::Server::Configuration::ServerFactoryContext& context,
                                   Stats::StatName) {
  const auto& local_response_config =
      MessageUtil::downcastAndValidate<const LocalResponsePolicyProto&>(
          config, context.messageValidationVisitor());
  return std::make_shared<LocalResponsePolicy>(local_response_config, context);
}

std::string LocalResponseFactory::name() const {
  return "envoy.extensions.http.custom_response.local_response_policy";
}

REGISTER_CUSTOM_RESPONSE_POLICY_FACTORY(LocalResponseFactory);

} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
