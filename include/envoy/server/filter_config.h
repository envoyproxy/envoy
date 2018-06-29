#pragma once

#include <functional>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/filter.h"
#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/admin.h"
#include "envoy/singleton/manager.h"
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
 * Context passed to network and HTTP filters to access server resources.
 * TODO(mattklein123): When we lock down visibility of the rest of the code, filters should only
 * access the rest of the server via interfaces exposed here.
 */
class FactoryContext {
public:
  virtual ~FactoryContext() {}

  /**
   * @return AccessLogManager for use by the entire server.
   */
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;

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
   * @return information about the local environment the server is running in.
   */
  virtual const LocalInfo::LocalInfo& localInfo() const PURE;

  /**
   * @return RandomGenerator& the random generator for the server.
   */
  virtual Envoy::Runtime::RandomGenerator& random() PURE;

  /**
   * @return a new ratelimit client. The implementation depends on the configuration of the server.
   */
  virtual RateLimit::ClientPtr
  rateLimitClient(const absl::optional<std::chrono::milliseconds>& timeout) PURE;

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
   * @return Stats::Scope& the listener's stats scope.
   */
  virtual Stats::Scope& listenerScope() PURE;

  /**
   * @return const envoy::api::v2::core::Metadata& the config metadata associated with this
   * listener.
   */
  virtual const envoy::api::v2::core::Metadata& listenerMetadata() const PURE;

  /**
   * @return SystemTimeSource& a reference to the top-level SystemTime source.
   */
  virtual SystemTimeSource& systemTimeSource() PURE;
};

class ListenerFactoryContext : public FactoryContext {
public:
  /**
   * Store socket options to be set on the listen socket before listening.
   */
  virtual void addListenSocketOption(const Network::Socket::OptionConstSharedPtr& option) PURE;

  virtual void addListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) PURE;
};

/**
 * Implemented by each listener filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedListenerFilterConfigFactory {
public:
  virtual ~NamedListenerFilterConfigFactory() {}

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

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque message, will be parsed into this empty proto.
   *         Optional today, will be compulsory when v1 is deprecated.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a listener filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

/**
 * Implemented by each network filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedNetworkFilterConfigFactory {
public:
  virtual ~NamedNetworkFilterConfigFactory() {}

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
    NOT_IMPLEMENTED;
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
};

/**
 * Implemented by each HTTP filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedHttpFilterConfigFactory {
public:
  virtual ~NamedHttpFilterConfigFactory() {}

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
    NOT_IMPLEMENTED;
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
  createRouteSpecificFilterConfig(const Protobuf::Message&, FactoryContext&) {
    return nullptr;
  }

  /**
   * @return std::string the identifying name for a particular implementation of an http filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
