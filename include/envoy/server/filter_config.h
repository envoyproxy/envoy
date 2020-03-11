#pragma once

#include <functional>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/grpc/context.h"
#include "envoy/http/codes.h"
#include "envoy/http/context.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/admin.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/process_context.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Common interface for downstream and upstream network filters.
 */
class CommonFactoryContext {
public:
  virtual ~CommonFactoryContext() = default;

  /**
   * @return Upstream::ClusterManager& singleton for use by the entire server.
   */
  virtual Upstream::ClusterManager& clusterManager() PURE;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher. This dispatcher should be used
   *         for all singleton processing.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return information about the local environment the server is running in.
   */
  virtual const LocalInfo::LocalInfo& localInfo() const PURE;

  /**
   * @return ProtobufMessage::ValidationContext& validation visitor for xDS and static configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationContext& messageValidationContext() PURE;

  /**
   * @return RandomGenerator& the random generator for the server.
   */
  virtual Envoy::Runtime::RandomGenerator& random() PURE;

  /**
   * @return Runtime::Loader& the singleton runtime loader for the server.
   */
  virtual Envoy::Runtime::Loader& runtime() PURE;

  /**
   * @return Stats::Scope& the filter's stats scope.
   */
  virtual Stats::Scope& scope() PURE;

  /**
   * @return Singleton::Manager& the server-wide singleton manager.
   */
  virtual Singleton::Manager& singletonManager() PURE;

  /**
   * @return ThreadLocal::SlotAllocator& the thread local storage engine for the server. This is
   *         used to allow runtime lockless updates to configuration, etc. across multiple threads.
   */
  virtual ThreadLocal::SlotAllocator& threadLocal() PURE;

  /**
   * @return Server::Admin& the server's global admin HTTP endpoint.
   */
  virtual Server::Admin& admin() PURE;

  /**
   * @return TimeSource& a reference to the time source.
   */
  virtual TimeSource& timeSource() PURE;

  /**
   * @return Api::Api& a reference to the api object.
   */
  virtual Api::Api& api() PURE;
};

/**
 * ServerFactoryContext is an specialization of common interface for downstream and upstream network
 * filters. The implementation guarantees the lifetime is no shorter than server. It could be used
 * across listeners.
 */
class ServerFactoryContext : public virtual CommonFactoryContext {
public:
  ~ServerFactoryContext() override = default;

  /**
   * @return the server-wide grpc context.
   */
  virtual Grpc::Context& grpcContext() PURE;
};

/**
 * Context passed to network and HTTP filters to access server resources.
 * TODO(mattklein123): When we lock down visibility of the rest of the code, filters should only
 * access the rest of the server via interfaces exposed here.
 */
class FactoryContext : public virtual CommonFactoryContext {
public:
  ~FactoryContext() override = default;

  /**
   * @return ServerFactoryContext which lifetime is no shorter than the server.
   */
  virtual ServerFactoryContext& getServerFactoryContext() const PURE;

  /**
   * @return TransportSocketFactoryContext which lifetime is no shorter than the server.
   */
  virtual TransportSocketFactoryContext& getTransportSocketFactoryContext() const PURE;

  /**
   * @return AccessLogManager for use by the entire server.
   */
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;

  /**
   * @return envoy::config::core::v3::TrafficDirection the direction of the traffic relative to
   * the local proxy.
   */
  virtual envoy::config::core::v3::TrafficDirection direction() const PURE;

  /**
   * @return const Network::DrainDecision& a drain decision that filters can use to determine if
   *         they should be doing graceful closes on connections when possible.
   */
  virtual const Network::DrainDecision& drainDecision() PURE;

  /**
   * @return whether external healthchecks are currently failed or not.
   */
  virtual bool healthCheckFailed() PURE;

  /**
   * @return the server's init manager. This can be used for extensions that need to initialize
   *         after cluster manager init but before the server starts listening. All extensions
   *         should register themselves during configuration load. initialize() will be called on
   *         each registered target after cluster manager init but before the server starts
   *         listening. Once all targets have initialized and invoked their callbacks, the server
   *         will start listening.
   */
  virtual Init::Manager& initManager() PURE;

  /**
   * @return ServerLifecycleNotifier& the lifecycle notifier for the server.
   */
  virtual ServerLifecycleNotifier& lifecycleNotifier() PURE;

  /**
   * @return Stats::Scope& the listener's stats scope.
   */
  virtual Stats::Scope& listenerScope() PURE;

  /**
   * @return const envoy::config::core::v3::Metadata& the config metadata associated with this
   * listener.
   */
  virtual const envoy::config::core::v3::Metadata& listenerMetadata() const PURE;

  /**
   * @return OverloadManager& the overload manager for the server.
   */
  virtual OverloadManager& overloadManager() PURE;

  /**
   * @return Http::Context& a reference to the http context.
   */
  virtual Http::Context& httpContext() PURE;

  /**
   * @return Grpc::Context& a reference to the grpc context.
   */
  virtual Grpc::Context& grpcContext() PURE;

  /**
   * @return ProcessContextOptRef an optional reference to the
   * process context. Will be unset when running in validation mode.
   */
  virtual ProcessContextOptRef processContext() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for filter configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;
};

/**
 * An implementation of FactoryContext. The life time is no shorter than the created filter chains.
 * The life time is no longer than the owning listener. It should be used to create
 * NetworkFilterChain.
 */
class FilterChainFactoryContext : public virtual FactoryContext {};

/**
 * An implementation of FactoryContext. The life time should cover the lifetime of the filter chains
 * and connections. It can be used to create ListenerFilterChain.
 */
class ListenerFactoryContext : public virtual FactoryContext {
public:
  /**
   * Give access to the listener configuration
   */
  virtual const Network::ListenerConfig& listenerConfig() const PURE;
};

/**
 * Common interface for listener filters and UDP listener filters
 */
class ListenerFilterConfigFactoryBase : public Config::TypedFactory {
public:
  virtual ~ListenerFilterConfigFactoryBase() = default;
};

/**
 * Implemented by each listener filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedListenerFilterConfigFactory : public ListenerFilterConfigFactoryBase {
public:
  ~NamedListenerFilterConfigFactory() override = default;

  /**
   * Create a particular listener filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error or a Json::Exception if the json configuration is erroneous. The returned
   * callback should always be initialized.
   * @param config supplies the general protobuf configuration for the filter
   * @param context supplies the filter's context.
   * @return Network::ListenerFilterFactoryCb the factory creation function.
   */
  virtual Network::ListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.listener"; }
};

/**
 * Implemented by each UDP listener filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedUdpListenerFilterConfigFactory : public ListenerFilterConfigFactoryBase {
public:
  ~NamedUdpListenerFilterConfigFactory() override = default;

  /**
   * Create a particular UDP listener filter factory implementation. If the implementation is unable
   * to produce a factory with the provided parameters, it should throw an EnvoyException.
   * The returned callback should always be initialized.
   * @param config supplies the general protobuf configuration for the filter
   * @param context supplies the filter's context.
   * @return Network::UdpListenerFilterFactoryCb the factory creation function.
   */
  virtual Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.udp_listener"; }
};

/**
 * Implemented by filter factories that require more options to process the protocol used by the
 * upstream cluster.
 */
class ProtocolOptionsFactory : public Config::TypedFactory {
public:
  virtual ~ProtocolOptionsFactory() = default;

  /**
   * Create a particular filter's protocol specific options implementation. If the factory
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException.
   * @param config supplies the protobuf configuration for the filter
   * @param validation_visitor message validation visitor instance.
   * @return Upstream::ProtocolOptionsConfigConstSharedPtr the protocol options
   */
  virtual Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& config,
                              ProtobufMessage::ValidationVisitor& validation_visitor) {
    UNREFERENCED_PARAMETER(config);
    UNREFERENCED_PARAMETER(validation_visitor);
    return nullptr;
  }

  /**
   * @return ProtobufTypes::MessagePtr a newly created empty protocol specific options message or
   *         nullptr if protocol specific options are not available.
   */
  virtual ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() { return nullptr; }
};

/**
 * Implemented by each network filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedNetworkFilterConfigFactory : public ProtocolOptionsFactory {
public:
  ~NamedNetworkFilterConfigFactory() override = default;

  /**
   * Create a particular network filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException. The returned
   * callback should always be initialized.
   * @param config supplies the general json configuration for the filter
   * @param filter_chain_factory_context supplies the filter's context.
   * @return Network::FilterFactoryCb the factory creation function.
   */
  virtual Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               FactoryContext& filter_chain_factory_context) PURE;

  std::string category() const override { return "envoy.filters.network"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() { return false; }
};

/**
 * Implemented by each upstream cluster network filter and registered via
 * Registry::registerFactory() or the convenience class RegisterFactory.
 */
class NamedUpstreamNetworkFilterConfigFactory : public ProtocolOptionsFactory {
public:
  ~NamedUpstreamNetworkFilterConfigFactory() override = default;

  /**
   * Create a particular upstream network filter factory implementation. If the implementation is
   * unable to produce a factory with the provided parameters, it should throw an EnvoyException in
   * the case of general error. The returned callback should always be initialized.
   */
  virtual Network::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                                CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.upstream_network"; }
};

/**
 * Implemented by each HTTP filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedHttpFilterConfigFactory : public ProtocolOptionsFactory {
public:
  ~NamedHttpFilterConfigFactory() override = default;

  /**
   * Create a particular http filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException. The returned
   * callback should always be initialized.
   * @param config supplies the general Protobuf message to be marshaled into a filter-specific
   * configuration.
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's context.
   * @return Http::FilterFactoryCb the factory creation function.
   */
  virtual Http::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                             const std::string& stat_prefix,
                                                             FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create an empty virtual host, route, or weighted
   *         cluster-local config proto message for v2. The filter config, which arrives in an
   *         opaque message, will be parsed into this empty proto. By default, this method
   *         returns the same value as createEmptyConfigProto, and can be optionally overridden
   *         in implementations.
   */
  virtual ProtobufTypes::MessagePtr createEmptyRouteConfigProto() {
    return createEmptyConfigProto();
  }

  /**
   * @return RouteSpecificFilterConfigConstSharedPtr allow the filter to pre-process per route
   * config. Returned object will be stored in the loaded route configuration.
   */
  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&, ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  std::string category() const override { return "envoy.filters.http"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() { return false; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
