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

HttpApiListenerImpl::HttpApiListenerImpl(const envoy::api::v2::Listener& config,
                                         ListenerManagerImpl& parent, const std::string& name,
                                         ProtobufMessage::ValidationVisitor& validation_visitor)
    : config_(config), parent_(parent), name_(name),
      address_(Network::Address::resolveProtoAddress(config.address())),
      validation_visitor_(validation_visitor),
      global_scope_(parent_.server_.stats().createScope("")),
      listener_scope_(parent_.server_.stats().createScope(fmt::format("listener.api.{}.", name_))),
      read_callbacks_(SyntheticReadCallbacks(*this)),
      http_connection_manager_factory_(
          Envoy::Extensions::NetworkFilters::HttpConnectionManager::HttpConnectionManagerFactory::
              createHttpConnectionManagerFactoryFromProto(config.api_listener().api_listener(),
                                                          *this, read_callbacks_)) {}

ApiListenerHandle* HttpApiListenerImpl::handle() {
  if (!http_connection_manager_) {
    http_connection_manager_ = http_connection_manager_factory_();
  }
  return http_connection_manager_.get();
}

AccessLog::AccessLogManager& HttpApiListenerImpl::accessLogManager() {
  return parent_.server_.accessLogManager();
}
Upstream::ClusterManager& HttpApiListenerImpl::clusterManager() {
  return parent_.server_.clusterManager();
}
Event::Dispatcher& HttpApiListenerImpl::dispatcher() { return parent_.server_.dispatcher(); }
Network::DrainDecision& HttpApiListenerImpl::drainDecision() { return *this; }
Grpc::Context& HttpApiListenerImpl::grpcContext() { return parent_.server_.grpcContext(); }
bool HttpApiListenerImpl::healthCheckFailed() { return parent_.server_.healthCheckFailed(); }
Tracing::HttpTracer& HttpApiListenerImpl::httpTracer() { return httpContext().tracer(); }
Http::Context& HttpApiListenerImpl::httpContext() { return parent_.server_.httpContext(); }
Init::Manager& HttpApiListenerImpl::initManager() { return parent_.server_.initManager(); }
const LocalInfo::LocalInfo& HttpApiListenerImpl::localInfo() const {
  return parent_.server_.localInfo();
}
Envoy::Runtime::RandomGenerator& HttpApiListenerImpl::random() { return parent_.server_.random(); }
Envoy::Runtime::Loader& HttpApiListenerImpl::runtime() { return parent_.server_.runtime(); }
Stats::Scope& HttpApiListenerImpl::scope() { return *global_scope_; }
Singleton::Manager& HttpApiListenerImpl::singletonManager() {
  return parent_.server_.singletonManager();
}
OverloadManager& HttpApiListenerImpl::overloadManager() {
  return parent_.server_.overloadManager();
}
ThreadLocal::Instance& HttpApiListenerImpl::threadLocal() { return parent_.server_.threadLocal(); }
Admin& HttpApiListenerImpl::admin() { return parent_.server_.admin(); }
const envoy::api::v2::core::Metadata& HttpApiListenerImpl::listenerMetadata() const {
  return config_.metadata();
};
envoy::api::v2::core::TrafficDirection HttpApiListenerImpl::direction() const {
  return config_.traffic_direction();
};
TimeSource& HttpApiListenerImpl::timeSource() { return api().timeSource(); }
ProtobufMessage::ValidationVisitor& HttpApiListenerImpl::messageValidationVisitor() {
  return validation_visitor_;
}
Api::Api& HttpApiListenerImpl::api() { return parent_.server_.api(); }
ServerLifecycleNotifier& HttpApiListenerImpl::lifecycleNotifier() {
  return parent_.server_.lifecycleNotifier();
}
OptProcessContextRef HttpApiListenerImpl::processContext() {
  return parent_.server_.processContext();
}
Configuration::ServerFactoryContext& HttpApiListenerImpl::getServerFactoryContext() const {
  return parent_.server_.serverFactoryContext();
}
Stats::Scope& HttpApiListenerImpl::listenerScope() { return *listener_scope_; }

} // namespace Server
} // namespace Envoy
