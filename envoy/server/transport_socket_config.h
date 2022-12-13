#pragma once

#include <string>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/init/manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Context passed to transport socket factory to access server resources.
 */
class TransportSocketFactoryContext {
public:
  virtual ~TransportSocketFactoryContext() = default;

  /**
   * @return OptRef<Server::Admin> the global HTTP admin endpoint for the server.
   */
  virtual OptRef<Server::Admin> admin() PURE;

  /**
   * @return Ssl::ContextManager& the SSL context manager.
   */
  virtual Ssl::ContextManager& sslContextManager() PURE;

  /**
   * @return Stats::Scope& the transport socket's stats scope.
   */
  virtual Stats::Scope& scope() PURE;

  /**
   * Return the instance of secret manager.
   */
  virtual Secret::SecretManager& secretManager() PURE;

  /**
   * @return the instance of ClusterManager.
   */
  virtual Upstream::ClusterManager& clusterManager() PURE;

  /**
   * @return information about the local environment the server is running in.
   */
  virtual const LocalInfo::LocalInfo& localInfo() const PURE;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher.
   */
  virtual Event::Dispatcher& mainThreadDispatcher() PURE;

  /**
   * @return Server::Options& the command-line options that Envoy was started with.
   */
  virtual const Options& options() PURE;

  /**
   * @return the server-wide stats store.
   */
  virtual Stats::Store& stats() PURE;

  /**
   * @return a reference to the instance of an init manager.
   */
  virtual Init::Manager& initManager() PURE;

  /**
   * @return the server's singleton manager.
   */
  virtual Singleton::Manager& singletonManager() PURE;

  /**
   * @return the server's TLS slot allocator.
   */
  virtual ThreadLocal::SlotAllocator& threadLocal() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for filter configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;

  /**
   * @return reference to the Api object
   */
  virtual Api::Api& api() PURE;

  /**
   * @return reference to the access log manager object
   */
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;
};

using TransportSocketFactoryContextPtr = std::unique_ptr<TransportSocketFactoryContext>;

class TransportSocketConfigFactory : public Config::TypedFactory {
public:
  ~TransportSocketConfigFactory() override = default;
};

/**
 * Implemented by each transport socket used for upstream connections. Registered via class
 * RegisterFactory.
 */
class UpstreamTransportSocketConfigFactory : public virtual TransportSocketConfigFactory {
public:
  /**
   * Create a particular transport socket factory implementation.
   * @param config const Protobuf::Message& supplies the config message for the transport socket
   *        implementation.
   * @param context TransportSocketFactoryContext& supplies the transport socket's context.
   * @return Network::UpstreamTransportSocketFactoryPtr the transport socket factory instance. The
   * returned TransportSocketFactoryPtr should not be nullptr.
   *
   * @throw EnvoyException if the implementation is unable to produce a factory with the provided
   *        parameters.
   */
  virtual Network::UpstreamTransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               TransportSocketFactoryContext& context) PURE;

  std::string category() const override { return "envoy.transport_sockets.upstream"; }
};

/**
 * Implemented by each transport socket used for downstream connections. Registered via class
 * RegisterFactory.
 */
class DownstreamTransportSocketConfigFactory : public virtual TransportSocketConfigFactory {
public:
  /**
   * Create a particular downstream transport socket factory implementation.
   * @param server_names const std::vector<std::string>& the names of the server. This parameter is
   *        currently used by SNI implementation to know the expected server names.
   * @param config const Protobuf::Message& supplies the config message for the transport socket
   *        implementation.
   * @param context TransportSocketFactoryContext& supplies the transport socket's context.
   * @return Network::DownstreamTransportSocketFactoryPtr the transport socket factory instance. The
   * returned TransportSocketFactoryPtr should not be nullptr.
   *
   * @throw EnvoyException if the implementation is unable to produce a factory with the provided
   *        parameters.
   */
  virtual Network::DownstreamTransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) PURE;

  std::string category() const override { return "envoy.transport_sockets.downstream"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
