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

ApiListenerImpl::ApiListenerImpl(const envoy::config::listener::v3alpha::Listener& config,
                                 ListenerManagerImpl& parent, const std::string& name,
                                 ProtobufMessage::ValidationVisitor& validation_visitor)
    : config_(config), parent_(parent), name_(name),
      address_(Network::Address::resolveProtoAddress(config.address())),
      validation_visitor_(validation_visitor),
      global_scope_(parent_.server_.stats().createScope("")),
      listener_scope_(parent_.server_.stats().createScope(fmt::format("listener.api.{}.", name_))),
      read_callbacks_(SyntheticReadCallbacks(*this)) {}

AccessLog::AccessLogManager& ApiListenerImpl::accessLogManager() {
  return parent_.server_.accessLogManager();
}
Upstream::ClusterManager& ApiListenerImpl::clusterManager() {
  return parent_.server_.clusterManager();
}
Event::Dispatcher& ApiListenerImpl::dispatcher() { return parent_.server_.dispatcher(); }
Network::DrainDecision& ApiListenerImpl::drainDecision() { return *this; }
Grpc::Context& ApiListenerImpl::grpcContext() { return parent_.server_.grpcContext(); }
bool ApiListenerImpl::healthCheckFailed() { return parent_.server_.healthCheckFailed(); }
Tracing::HttpTracer& ApiListenerImpl::httpTracer() { return httpContext().tracer(); }
Http::Context& ApiListenerImpl::httpContext() { return parent_.server_.httpContext(); }
Init::Manager& ApiListenerImpl::initManager() { return parent_.server_.initManager(); }
const LocalInfo::LocalInfo& ApiListenerImpl::localInfo() const {
  return parent_.server_.localInfo();
}
Envoy::Runtime::RandomGenerator& ApiListenerImpl::random() { return parent_.server_.random(); }
Envoy::Runtime::Loader& ApiListenerImpl::runtime() { return parent_.server_.runtime(); }
Stats::Scope& ApiListenerImpl::scope() { return *global_scope_; }
Singleton::Manager& ApiListenerImpl::singletonManager() {
  return parent_.server_.singletonManager();
}
OverloadManager& ApiListenerImpl::overloadManager() { return parent_.server_.overloadManager(); }
ThreadLocal::Instance& ApiListenerImpl::threadLocal() { return parent_.server_.threadLocal(); }
Admin& ApiListenerImpl::admin() { return parent_.server_.admin(); }
const envoy::config::core::v3alpha::Metadata& ApiListenerImpl::listenerMetadata() const {
  return config_.metadata();
};
envoy::config::core::v3alpha::TrafficDirection ApiListenerImpl::direction() const {
  return config_.traffic_direction();
};
TimeSource& ApiListenerImpl::timeSource() { return api().timeSource(); }
ProtobufMessage::ValidationVisitor& ApiListenerImpl::messageValidationVisitor() {
  return validation_visitor_;
}
Api::Api& ApiListenerImpl::api() { return parent_.server_.api(); }
ServerLifecycleNotifier& ApiListenerImpl::lifecycleNotifier() {
  return parent_.server_.lifecycleNotifier();
}
OptProcessContextRef ApiListenerImpl::processContext() { return parent_.server_.processContext(); }
Configuration::ServerFactoryContext& ApiListenerImpl::getServerFactoryContext() const {
  return parent_.server_.serverFactoryContext();
}
Stats::Scope& ApiListenerImpl::listenerScope() { return *listener_scope_; }

HttpApiListener::HttpApiListener(const envoy::config::listener::v3alpha::Listener& config,
                                 ListenerManagerImpl& parent, const std::string& name,
                                 ProtobufMessage::ValidationVisitor& validation_visitor)
    : ApiListenerImpl(config, parent, name, validation_visitor) {
  auto typed_config = MessageUtil::anyConvert<
      envoy::extensions::filters::network::http_connection_manager::v3alpha::HttpConnectionManager>(
      config.api_listener().api_listener());

  http_connection_manager_factory_ =
      Envoy::Extensions::NetworkFilters::HttpConnectionManager::HttpConnectionManagerFactory::
          createHttpConnectionManagerFactoryFromProto(typed_config, *this, read_callbacks_);
}

Http::ApiListener* HttpApiListener::http() {
  if (!http_connection_manager_) {
    http_connection_manager_ = http_connection_manager_factory_();
  }
  return http_connection_manager_.get();
}

} // namespace Server
} // namespace Envoy
