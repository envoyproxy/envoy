#include "source/extensions/filters/http/custom_response/redirect_factory.h"

#include <memory>

#include "source/extensions/filters/http/custom_response/redirect_policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
using RedirectPolicyProto =
    envoy::extensions::filters::http::custom_response::v3::CustomResponse::RedirectPolicy;

ProtobufTypes::MessagePtr RedirectFactory::createEmptyConfigProto() {
  return std::make_unique<RedirectPolicyProto>();
}

RedirectFactory::~RedirectFactory() = default;

PolicySharedPtr
RedirectFactory::createPolicy(const Protobuf::Message& config,
                              Envoy::Server::Configuration::ServerFactoryContext& context,
                              Stats::StatName stats_prefix) {
  const auto& redirect_config = MessageUtil::downcastAndValidate<const RedirectPolicyProto&>(
      config, context.messageValidationVisitor());
  return std::make_shared<RedirectPolicy>(redirect_config, stats_prefix, context);
}

std::string RedirectFactory::name() const {
  return "envoy.extensions.filters.http.custom_response.redirect_policy";
}

REGISTER_FACTORY(RedirectFactory, PolicyFactory);

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
