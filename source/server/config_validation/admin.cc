#include "source/server/config_validation/admin.h"

#include "source/common/config/metadata.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"

namespace Envoy {
namespace Server {

class ValidationFactoryContext final : public Configuration::FactoryContext {
public:
  ValidationFactoryContext(Envoy::Server::Instance& server) : server_(server) {}

  AccessLog::AccessLogManager& accessLogManager() override { return server_.accessLogManager(); }
  Upstream::ClusterManager& clusterManager() override { return server_.clusterManager(); }
  Event::Dispatcher& mainThreadDispatcher() override { return server_.dispatcher(); }
  const Server::Options& options() override { return server_.options(); }
  Grpc::Context& grpcContext() override { return server_.grpcContext(); }
  bool healthCheckFailed() override { return server_.healthCheckFailed(); }
  Http::Context& httpContext() override { return server_.httpContext(); }
  Router::Context& routerContext() override { return server_.routerContext(); }
  const LocalInfo::LocalInfo& localInfo() const override { return server_.localInfo(); }
  Envoy::Runtime::Loader& runtime() override { return server_.runtime(); }
  Stats::Scope& serverScope() override { return *server_.stats().rootScope(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return server_.messageValidationContext();
  }
  Singleton::Manager& singletonManager() override { return server_.singletonManager(); }
  OverloadManager& overloadManager() override { return server_.overloadManager(); }
  ThreadLocal::Instance& threadLocal() override { return server_.threadLocal(); }
  OptRef<Admin> admin() override { return OptRef<Admin>(server_.admin()); }
  TimeSource& timeSource() override { return server_.timeSource(); }
  Api::Api& api() override { return server_.api(); }
  ServerLifecycleNotifier& lifecycleNotifier() override { return server_.lifecycleNotifier(); }
  ProcessContextOptRef processContext() override { return server_.processContext(); }

  Configuration::ServerFactoryContext& getServerFactoryContext() const override {
    return server_.serverFactoryContext();
  }
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override {
    return server_.transportSocketFactoryContext();
  }
  Stats::Scope& scope() override { return *server_.stats().rootScope(); }
  Stats::Scope& listenerScope() override { return *server_.stats().rootScope(); }
  bool isQuicListener() const override { return false; }
  const envoy::config::core::v3::Metadata& listenerMetadata() const override {
    return metadata_.proto_metadata_;
  }
  const Envoy::Config::TypedMetadata& listenerTypedMetadata() const override {
    return metadata_.typed_metadata_;
  }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return envoy::config::core::v3::UNSPECIFIED;
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return server_.messageValidationContext().staticValidationVisitor();
  }
  Init::Manager& initManager() override { return server_.initManager(); }
  Network::DrainDecision& drainDecision() override { return server_.drainManager(); }

private:
  Envoy::Server::Instance& server_;
  // Empty metadata for the admin handler.
  Envoy::Config::MetadataPack<Envoy::Network::ListenerTypedMetadataFactory> metadata_;
};

// Pretend that handler was added successfully.
bool ValidationAdmin::addStreamingHandler(const std::string&, const std::string&, GenRequestFn,
                                          bool, bool, const ParamDescriptorVec&) {
  return true;
}

bool ValidationAdmin::addHandler(const std::string&, const std::string&, HandlerCb, bool, bool,
                                 const ParamDescriptorVec&) {
  return true;
}

bool ValidationAdmin::removeHandler(const std::string&) { return true; }

const Network::Socket& ValidationAdmin::socket() { return *socket_; }

ConfigTracker& ValidationAdmin::getConfigTracker() { return config_tracker_; }

void ValidationAdmin::startHttpListener(std::list<AccessLog::InstanceSharedPtr>,
                                        Network::Address::InstanceConstSharedPtr,
                                        Network::Socket::OptionsSharedPtr) {}

Http::Code ValidationAdmin::request(absl::string_view, absl::string_view, Http::ResponseHeaderMap&,
                                    std::string&) {
  PANIC("not implemented");
}

void ValidationAdmin::addListenerToHandler(Network::ConnectionHandler*) {}

} // namespace Server
} // namespace Envoy
