#pragma once

#include <functional>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/grpc/context.h"
#include "envoy/http/codes.h"
#include "envoy/http/context.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/json/json_object.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/admin.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/process_context.h"
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
   * @return AccessLogManager for use by the entire server.
   */
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;

  /**
   * @return envoy::api::v2::core::TrafficDirection the direction of the traffic relative to the
   * local proxy.
   */
  virtual envoy::api::v2::core::TrafficDirection direction() const PURE;

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
   * @return the server-wide http tracer.
   */
  virtual Tracing::HttpTracer& httpTracer() PURE;

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
   * @return const envoy::api::v2::core::Metadata& the config metadata associated with this
   * listener.
   */
  virtual const envoy::api::v2::core::Metadata& listenerMetadata() const PURE;

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
   * @return OptProcessContextRef an optional reference to the
   * process context. Will be unset when running in validation mode.
   */
  virtual OptProcessContextRef processContext() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for filter configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;
};

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
class ListenerFilterConfigFactoryBase {
public:
  virtual ~ListenerFilterConfigFactoryBase() = default;

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message. The filter
   *         config, which arrives in an opaque message, will be parsed into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a listener filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
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
};

/**
 * Implemented by filter factories that require more options to process the protocol used by the
 * upstream cluster.
 */
class ProtocolOptionsFactory {
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
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error or a Json::Exception if the json configuration is erroneous. The returned
   * callback should always be initialized.
   * @param config supplies the general json configuration for the filter
   * @param context supplies the filter's context.
   * @return Network::FilterFactoryCb the factory creation function.
   */
  virtual Network::FilterFactoryCb createFilterFactory(const Json::Object& config,
                                                       FactoryContext& context) PURE;

  /**
   * v2 variant of createFilterFactory(..), where filter configs are specified as proto. This may be
   * optionally implemented today, but will in the future become compulsory once v1 is deprecated.
   */
  virtual Network::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                                FactoryContext& context) {
    UNREFERENCED_PARAMETER(config);
    UNREFERENCED_PARAMETER(context);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted to
   *         JSON and then parsed into this empty proto. Optional today, will be compulsory when v1
   *         is deprecated.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() { return nullptr; }

  /**
   * @return std::string the identifying name for a particular implementation of a network filter
   * produced by the factory.
   */
  virtual std::string name() PURE;

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

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a network filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
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
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of
   * general error or a Json::Exception if the json configuration is erroneous. The returned
   * callback should always be initialized.
   * @param config supplies the general json configuration for the filter
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's context.
   * @return Http::FilterFactoryCb the factory creation function.
   */
  virtual Http::FilterFactoryCb createFilterFactory(const Json::Object& config,
                                                    const std::string& stat_prefix,
                                                    FactoryContext& context) PURE;

  /**
   * v2 API variant of createFilterFactory(..), where filter configs are specified as proto. This
   * may be optionally implemented today, but will in the future become compulsory once v1 is
   * deprecated.
   */
  virtual Http::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                             const std::string& stat_prefix,
                                                             FactoryContext& context) {
    UNREFERENCED_PARAMETER(config);
    UNREFERENCED_PARAMETER(stat_prefix);
    UNREFERENCED_PARAMETER(context);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted to
   *         JSON and then parsed into this empty proto. Optional today, will be compulsory when v1
   *         is deprecated.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() { return nullptr; }

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

  /**
   * @return std::string the identifying name for a particular implementation of an http filter
   * produced by the factory.
   */
  virtual std::string name() PURE;

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() { return false; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
