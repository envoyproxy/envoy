#include "source/extensions/filters/network/http_connection_manager/forward_client_cert_details.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

Http::ForwardClientCertType
convertForwardClientCertDetailsType(envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::ForwardClientCertDetails type) {
  switch (type) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE:
    return Http::ForwardClientCertType::Sanitize;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      FORWARD_ONLY:
    return Http::ForwardClientCertType::ForwardOnly;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD:
    return Http::ForwardClientCertType::AppendForward;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET:
    return Http::ForwardClientCertType::SanitizeSet;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ALWAYS_FORWARD_ONLY:
    return Http::ForwardClientCertType::AlwaysForwardOnly;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

std::vector<Http::ClientCertDetailsType> convertSetCurrentClientCertDetails(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        SetCurrentClientCertDetails& details) {
  std::vector<Http::ClientCertDetailsType> result;
  if (details.cert()) {
    result.push_back(Http::ClientCertDetailsType::Cert);
  }
  if (details.chain()) {
    result.push_back(Http::ClientCertDetailsType::Chain);
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(details, subject, false)) {
    result.push_back(Http::ClientCertDetailsType::Subject);
  }
  if (details.uri()) {
    result.push_back(Http::ClientCertDetailsType::URI);
  }
  if (details.dns()) {
    result.push_back(Http::ClientCertDetailsType::DNS);
  }
  return result;
}

Matcher::ActionConstSharedPtr
ForwardClientCertActionFactory::createAction(const Protobuf::Message& config,
                                             ForwardClientCertActionFactoryContext&,
                                             ProtobufMessage::ValidationVisitor&) {
  const auto& typed_config =
      dynamic_cast<const envoy::extensions::filters::network::http_connection_manager::v3::
                       HttpConnectionManager::ForwardClientCertConfig&>(config);

  return std::make_shared<ForwardClientCertAction>(
      convertForwardClientCertDetailsType(typed_config.forward_client_cert_details()),
      convertSetCurrentClientCertDetails(typed_config.set_current_client_cert_details()));
}

REGISTER_FACTORY(ForwardClientCertActionFactory,
                 Matcher::ActionFactory<ForwardClientCertActionFactoryContext>);

Matcher::MatchTreePtr<Http::HttpMatchingData>
createForwardClientCertMatcher(const xds::type::matcher::v3::Matcher& matcher_config,
                               Server::Configuration::ServerFactoryContext& factory_context) {
  ForwardClientCertMatcherValidationVisitor validation_visitor;
  ForwardClientCertActionFactoryContext action_factory_context{factory_context};
  Matcher::MatchTreeFactory<Http::HttpMatchingData, ForwardClientCertActionFactoryContext> factory(
      action_factory_context, factory_context, validation_visitor);
  return factory.create(matcher_config)();
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
