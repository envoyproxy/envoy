#pragma once

#include <functional>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/filter.h"
#include "envoy/json/json_object.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/server/server_context.h"
#include "envoy/tracing/http_tracer.h"

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
class FactoryContext : public ServerContext {
public:
  virtual ~FactoryContext() {}

  /**
   * @return const Network::DrainDecision& a drain decision that filters can use to determine if
   *         they should be doing graceful closes on connections when possible.
   */
  virtual const Network::DrainDecision& drainDecision() PURE;

  /**
   * @return Stats::Scope& the filter's stats scope.
   */
  virtual Stats::Scope& scope() PURE;

  /**
   * @return const envoy::api::v2::core::Metadata& the config metadata associated with this
   * listener.
   */
  virtual const envoy::api::v2::core::Metadata& listenerMetadata() const PURE;

  /**
   * @return Stats::Scope& the listener's stats scope.
   */
  virtual Stats::Scope& listenerScope() PURE;

  /**
   * Store socket options to be set on the listen socket before listening.
   */
  virtual void setListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) PURE;
};

/**
 * This function is used to wrap the creation of a listener filter chain for new sockets as they are
 * created. Filter factories create the lambda at configuration initialization time, and then they
 * are used at runtime.
 * @param filter_manager supplies the filter manager for the listener to install filters to.
 * Typically the function will install a single filter, but it's technically possibly to install
 * more than one if desired.
 */
typedef std::function<void(Network::ListenerFilterManager& filter_manager)> ListenerFilterFactoryCb;

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
   * @return ListenerFilterFactoryCb the factory creation function.
   */
  virtual ListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted to
   *         JSON and then parsed into this empty proto. Optional today, will be compulsory when v1
   *         is deprecated.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a listener filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

/**
 * This function is used to wrap the creation of a network filter chain for new connections as
 * they come in. Filter factories create the lambda at configuration initialization time, and then
 * they are used at runtime.
 * @param filter_manager supplies the filter manager for the connection to install filters
 * to. Typically the function will install a single filter, but it's technically possibly to
 * install more than one if desired.
 */
typedef std::function<void(Network::FilterManager& filter_manager)> NetworkFilterFactoryCb;

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
   * @return NetworkFilterFactoryCb the factory creation function.
   */
  virtual NetworkFilterFactoryCb createFilterFactory(const Json::Object& config,
                                                     FactoryContext& context) PURE;

  /**
   * v2 variant of createFilterFactory(..), where filter configs are specified as proto. This may be
   * optionally implemented today, but will in the future become compulsory once v1 is deprecated.
   */
  virtual NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
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
 * This function is used to wrap the creation of an HTTP filter chain for new streams as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
typedef std::function<void(Http::FilterChainFactoryCallbacks& callbacks)> HttpFilterFactoryCb;

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
   * @return HttpFilterFactoryCb the factory creation function.
   */
  virtual HttpFilterFactoryCb createFilterFactory(const Json::Object& config,
                                                  const std::string& stat_prefix,
                                                  FactoryContext& context) PURE;

  /**
   * v2 API variant of createFilterFactory(..), where filter configs are specified as proto. This
   * may be optionally implemented today, but will in the future become compulsory once v1 is
   * deprecated.
   */
  virtual HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
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
   * @return std::string the identifying name for a particular implementation of an http filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
