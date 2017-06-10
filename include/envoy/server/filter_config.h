#pragma once

#include <functional>

#include "envoy/access_log/access_log.h"
#include "envoy/http/filter.h"
#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/network/filter.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h" // TODO(mattklein123): Remove post 1.4.0
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

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
   * @return DrainManager& singleton for use by the entire server.
   */
  virtual DrainManager& drainManager() PURE;

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
  virtual const LocalInfo::LocalInfo& localInfo() PURE;

  /**
   * @return RandomGenerator& the random generator for the server.
   */
  virtual Envoy::Runtime::RandomGenerator& random() PURE;

  /**
   * @return a new ratelimit client. The implementation depends on the configuration of the server.
   */
  virtual RateLimit::ClientPtr
  rateLimitClient(const Optional<std::chrono::milliseconds>& timeout) PURE;

  /**
   * @return Runtime::Loader& the singleton runtime loader for the server.
   */
  virtual Envoy::Runtime::Loader& runtime() PURE;

  /**
   * DEPRECATED: Do not call this function. It will be removed post 1.4.0 and is needed for other
   * deprecated functionality.
   */
  virtual Instance& server() PURE;

  /**
   * @return Stats::Scope& the filter's stats scope.
   */
  virtual Stats::Scope& scope() PURE;

  /**
   * @return ThreadLocal::Instance& the thread local storage engine for the server. This is used to
   *         allow runtime lockless updates to configuration, etc. across multiple threads.
   */
  virtual ThreadLocal::Instance& threadLocal() PURE;
};

enum class NetworkFilterType { Read, Write, Both };

/**
 * This lambda is used to wrap the creation of a network filter chain for new connections as they
 * come in. Filter factories create the lambda at configuration initialization time, and then they
 * are used at runtime. This maps JSON -> runtime configuration.
 */
typedef std::function<void(Network::FilterManager& filter_manager)> NetworkFilterFactoryCb;

/**
 * Implemented by each network filter and registered via registerNamedNetworkFilterConfigFactory()
 * or the convenience class RegisterNamedNetworkFilterConfigFactory.
 */
class NamedNetworkFilterConfigFactory {
public:
  virtual ~NamedNetworkFilterConfigFactory() {}

  /**
   * Create a particular network filter factory implementation.  If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error or a Json::Exception if the json configuration is erroneous.  The returned
   * callback should always be initialized.
   * @param config supplies the general json configuration for the filter
   * @param context supplies the filter's context.
   */
  virtual NetworkFilterFactoryCb createFilterFactory(const Json::Object& config,
                                                     FactoryContext& context) PURE;

  /**
   * Returns the identifying name for a particular implementation of a network filter produced by
   * the factory.
   */
  virtual std::string name() PURE;

  /**
   * @return NetworkFilterType the type of filter.
   */
  virtual NetworkFilterType type() PURE;
};

enum class HttpFilterType { Decoder, Encoder, Both };

/**
 * Callback lambda used for dynamic HTTP filter chain construction.
 */
typedef std::function<void(Http::FilterChainFactoryCallbacks&)> HttpFilterFactoryCb;

/**
 * Implemented by each HTTP filter and registered via registerNamedHttpFilterConfigFactory() or the
 * convenience class RegisterNamedHttpFilterConfigFactory.
 */
class NamedHttpFilterConfigFactory {
public:
  virtual ~NamedHttpFilterConfigFactory() {}

  /**
  * Create a particular http filter factory implementation.  If the implementation is unable to
  * produce a factory with the provided parameters, it should throw an EnvoyException in the case of
  * general error or a Json::Exception if the json configuration is erroneous.  The returned
  * callback should always be initialized.
  * @param config supplies the general json configuration for the filter
  * @param stat_prefix prefix for stat logging
  * @param context supplies the filter's context.
  */
  virtual HttpFilterFactoryCb createFilterFactory(const Json::Object& config,
                                                  const std::string& stat_prefix,
                                                  FactoryContext& context) PURE;

  /**
   * Returns the identifying name for a particular implementation of an http filter produced by the
   * factory.
   */
  virtual std::string name() PURE;

  /**
   * @return HttpFilterType the type of filter.
   */
  virtual HttpFilterType type() PURE;
};

} // Configuration
} // Server
} // Envoy
