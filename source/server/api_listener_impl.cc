#include "source/server/api_listener_impl.h"

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/http/api_listener.h"
#include "envoy/stats/scope.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"

namespace Envoy {
namespace Server {

bool isQuic(const envoy::config::listener::v3::Listener& config) {
  return config.has_udp_listener_config() && config.udp_listener_config().has_quic_options();
}

ApiListenerImplBase::ApiListenerImplBase(const envoy::config::listener::v3::Listener& config,
                                         Server::Instance& server, const std::string& name)
    : config_(config), name_(name),
      address_(Network::Address::resolveProtoAddress(config.address())),
      global_scope_(server.stats().createScope("")),
      listener_scope_(server.stats().createScope(fmt::format("listener.api.{}.", name_))),
      factory_context_(server, config_, *this, *global_scope_, *listener_scope_, isQuic(config)) {}

void ApiListenerImplBase::SyntheticReadCallbacks::SyntheticConnection::raiseConnectionEvent(
    Network::ConnectionEvent event) {
  for (Network::ConnectionCallbacks* callback : callbacks_) {
    callback->onEvent(event);
  }
}

HttpApiListener::ApiListenerWrapper::~ApiListenerWrapper() {
  // The Http::ConnectionManagerImpl is a callback target for the read_callback_.connection_. By
  // raising connection closure, Http::ConnectionManagerImpl::onEvent is fired. In that case the
  // Http::ConnectionManagerImpl will reset any ActiveStreams it has.
  read_callbacks_.connection_.raiseConnectionEvent(Network::ConnectionEvent::RemoteClose);
}

Http::RequestDecoderHandlePtr
HttpApiListener::ApiListenerWrapper::newStreamHandle(Http::ResponseEncoder& response_encoder,
                                                     bool is_internally_created) {
  return http_connection_manager_->newStreamHandle(response_encoder, is_internally_created);
}

HttpApiListener::HttpApiListener(const envoy::config::listener::v3::Listener& config,
                                 Server::Instance& server, const std::string& name)
    : ApiListenerImplBase(config, server, name) {
  if (config.api_listener().api_listener().type_url() ==
      absl::StrCat(
          "type.googleapis.com/",
          createReflectableMessage(envoy::extensions::filters::network::http_connection_manager::
                                       v3::EnvoyMobileHttpConnectionManager::default_instance())
              ->GetDescriptor()
              ->full_name())) {
    auto typed_config = MessageUtil::anyConvertAndValidate<
        envoy::extensions::filters::network::http_connection_manager::v3::
            EnvoyMobileHttpConnectionManager>(config.api_listener().api_listener(),
                                              factory_context_.messageValidationVisitor());

    http_connection_manager_factory_ = Envoy::Extensions::NetworkFilters::HttpConnectionManager::
        HttpConnectionManagerFactory::createHttpConnectionManagerFactoryFromProto(
            typed_config.config(), factory_context_, false);
  } else {
    auto typed_config = MessageUtil::anyConvertAndValidate<
        envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
        config.api_listener().api_listener(), factory_context_.messageValidationVisitor());

    http_connection_manager_factory_ =
        Envoy::Extensions::NetworkFilters::HttpConnectionManager::HttpConnectionManagerFactory::
            createHttpConnectionManagerFactoryFromProto(typed_config, factory_context_, true);
  }
}

Http::ApiListenerPtr HttpApiListener::createHttpApiListener(Event::Dispatcher& dispatcher) {
  return std::make_unique<ApiListenerWrapper>(*this, dispatcher);
}

} // namespace Server
} // namespace Envoy
