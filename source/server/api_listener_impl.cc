#include "server/api_listener_impl.h"

#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/stats/scope.h"

#include "common/http/conn_manager_impl.h"
#include "common/network/resolver_impl.h"
#include "common/protobuf/utility.h"

#include "server/drain_manager_impl.h"
#include "server/listener_manager_impl.h"

#include "extensions/filters/network/http_connection_manager/config.h"

namespace Envoy {
namespace Server {

ApiListenerImplBase::ApiListenerImplBase(const envoy::config::listener::v3::Listener& config,
                                         ListenerManagerImpl& parent, const std::string& name)
    : config_(config), parent_(parent), name_(name),
      address_(Network::Address::resolveProtoAddress(config.address())),
      global_scope_(parent_.server_.stats().createScope("")),
      listener_scope_(parent_.server_.stats().createScope(fmt::format("listener.api.{}.", name_))),
      factory_context_(parent_.server_, config_, *this, *global_scope_, *listener_scope_),
      read_callbacks_(SyntheticReadCallbacks(*this)) {}

HttpApiListener::HttpApiListener(const envoy::config::listener::v3::Listener& config,
                                 ListenerManagerImpl& parent, const std::string& name)
    : ApiListenerImplBase(config, parent, name) {
  auto typed_config = MessageUtil::anyConvertAndValidate<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
      config.api_listener().api_listener(), factory_context_.messageValidationVisitor());

  http_connection_manager_factory_ = Envoy::Extensions::NetworkFilters::HttpConnectionManager::
      HttpConnectionManagerFactory::createHttpConnectionManagerFactoryFromProto(
          typed_config, factory_context_, read_callbacks_);
}

Http::ApiListenerOptRef HttpApiListener::http() {
  if (!http_connection_manager_) {
    http_connection_manager_ = http_connection_manager_factory_();
  }
  return Http::ApiListenerOptRef(std::ref(*http_connection_manager_));
}

} // namespace Server
} // namespace Envoy
