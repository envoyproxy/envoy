#include "source/extensions/filters/http/custom_response/local_response_factory.h"

#include "source/extensions/filters/http/custom_response/local_response_policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

using LocalResponsePolicyProto =
    envoy::extensions::filters::http::custom_response::v3::CustomResponse::LocalResponsePolicy;

ProtobufTypes::MessagePtr LocalResponseFactory::createEmptyConfigProto() {
  return std::make_unique<LocalResponsePolicyProto>();
}

PolicySharedPtr
LocalResponseFactory::createPolicy(const Protobuf::Message& config,
                                   Envoy::Server::Configuration::ServerFactoryContext& context,
                                   Stats::StatName) {
  const auto& local_response_config =
      MessageUtil::downcastAndValidate<const LocalResponsePolicyProto&>(
          config, context.messageValidationVisitor());
  return std::make_shared<LocalResponsePolicy>(local_response_config, context);
}

std::string LocalResponseFactory::name() const {
  return "envoy.extensions.filters.http.custom_response.local_response_policy";
}
REGISTER_FACTORY(LocalResponseFactory, PolicyFactory);

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
