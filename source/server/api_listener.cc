#include "server/api_listener.h"

#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/stats/scope.h"

#include "common/http/conn_manager_impl.h"
#include "common/protobuf/utility.h"
#include "common/network/resolver_impl.h"

#include "server/drain_manager_impl.h"
#include "server/listener_manager_impl.h"

#include "extensions/filters/network/http_connection_manager/config.h"

namespace Envoy {
namespace Server {

HttpApiListener::HttpApiListener(const envoy::api::v2::Listener& config,
                                 ListenerManagerImpl& parent, const std::string& name,
                                 ProtobufMessage::ValidationVisitor& validation_visitor)
    : config_(config), parent_(parent), name_(name),
      address_(Network::Address::resolveProtoAddress(config.address())),
      validation_visitor_(validation_visitor),
      global_scope_(parent_.server_.stats().createScope("")),
      listener_scope_(parent_.server_.stats().createScope(fmt::format("listener.api.{}.", name_))),
      read_callbacks_(SyntheticReadCallbacks(*this)) {
  ENVOY_LOG(error, "In API listener constructor");
  http_connection_manager_factory_ =
      Envoy::Extensions::NetworkFilters::HttpConnectionManager::HttpConnectionManagerFactory::
          createHttpConnectionManagerFactoryFromProto(config.api_listener().api_listener(), *this);
  ENVOY_LOG(error, "Created lambda");
}

Http::ServerConnectionCallbacks* HttpApiListener::apiHandle() {
  if (!http_connection_manager_) {
    http_connection_manager_ = http_connection_manager_factory_(read_callbacks_);
  }
  return http_connection_manager_.get();
}

AccessLog::AccessLogManager& HttpApiListener::accessLogManager() {
  return parent_.server_.accessLogManager();
}
Upstream::ClusterManager& HttpApiListener::clusterManager() {
  return parent_.server_.clusterManager();
}
Event::Dispatcher& HttpApiListener::dispatcher() { return parent_.server_.dispatcher(); }
Network::DrainDecision& HttpApiListener::drainDecision() { return *this; }
Grpc::Context& HttpApiListener::grpcContext() { return parent_.server_.grpcContext(); }
bool HttpApiListener::healthCheckFailed() { return parent_.server_.healthCheckFailed(); }
Tracing::HttpTracer& HttpApiListener::httpTracer() { return httpContext().tracer(); }
Http::Context& HttpApiListener::httpContext() { return parent_.server_.httpContext(); }
Init::Manager& HttpApiListener::initManager() { return parent_.server_.initManager(); }
const LocalInfo::LocalInfo& HttpApiListener::localInfo() const {
  return parent_.server_.localInfo();
}
Envoy::Runtime::RandomGenerator& HttpApiListener::random() { return parent_.server_.random(); }
Envoy::Runtime::Loader& HttpApiListener::runtime() { return parent_.server_.runtime(); }
Stats::Scope& HttpApiListener::scope() { return *global_scope_; }
Singleton::Manager& HttpApiListener::singletonManager() {
  return parent_.server_.singletonManager();
}
OverloadManager& HttpApiListener::overloadManager() { return parent_.server_.overloadManager(); }
ThreadLocal::Instance& HttpApiListener::threadLocal() { return parent_.server_.threadLocal(); }
Admin& HttpApiListener::admin() { return parent_.server_.admin(); }
const envoy::api::v2::core::Metadata& HttpApiListener::listenerMetadata() const {
  return config_.metadata();
};
envoy::api::v2::core::TrafficDirection HttpApiListener::direction() const {
  return config_.traffic_direction();
};
TimeSource& HttpApiListener::timeSource() { return api().timeSource(); }
ProtobufMessage::ValidationVisitor& HttpApiListener::messageValidationVisitor() {
  return validation_visitor_;
}
Api::Api& HttpApiListener::api() { return parent_.server_.api(); }
ServerLifecycleNotifier& HttpApiListener::lifecycleNotifier() {
  return parent_.server_.lifecycleNotifier();
}
OptProcessContextRef HttpApiListener::processContext() { return parent_.server_.processContext(); }
Configuration::ServerFactoryContext& HttpApiListener::getServerFactoryContext() const {
  return parent_.server_.serverFactoryContext();
}
Stats::Scope& HttpApiListener::listenerScope() { return *listener_scope_; }
// FIXME hook the api listener into listener draining in the manager.
bool HttpApiListener::drainClose() const { return false; }

} // namespace Server
} // namespace Envoy
