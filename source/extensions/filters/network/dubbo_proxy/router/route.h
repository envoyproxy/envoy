#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"
#include "envoy/router/router.h"
#include "envoy/server/filter_config.h"

#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

using RouteConfigurations = Protobuf::RepeatedPtrField<
    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration>;

enum class RouteMatcherType : uint8_t {
  Default,
};

/**
 * Names of available Protocol implementations.
 */
class RouteMatcherNameValues {
public:
  struct RouteMatcherTypeHash {
    template <typename T> std::size_t operator()(T t) const { return static_cast<std::size_t>(t); }
  };

  using RouteMatcherNameMap =
      std::unordered_map<RouteMatcherType, std::string, RouteMatcherTypeHash>;

  const RouteMatcherNameMap routeMatcherNameMap = {
      {RouteMatcherType::Default, "default"},
  };

  const std::string& fromType(RouteMatcherType type) const {
    const auto& itor = routeMatcherNameMap.find(type);
    ASSERT(itor != routeMatcherNameMap.end());
    return itor->second;
  }
};

using RouteMatcherNames = ConstSingleton<RouteMatcherNameValues>;

class RouteMatcher {
public:
  virtual ~RouteMatcher() = default;

  virtual RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const PURE;
};

using RouteMatcherPtr = std::unique_ptr<RouteMatcher>;
using RouteMatcherConstSharedPtr = std::shared_ptr<const RouteMatcher>;

/**
 * Implemented by each Dubbo protocol and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedRouteMatcherConfigFactory : public Envoy::Config::UntypedFactory {
public:
  virtual ~NamedRouteMatcherConfigFactory() = default;

  /**
   * Create a particular Dubbo protocol.
   * @param serialization_type the serialization type of the protocol body.
   * @return protocol instance pointer.
   */
  virtual RouteMatcherPtr createRouteMatcher(const RouteConfigurations& route_configs,
                                             Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.dubbo_proxy.route_matchers"; }

  /**
   * Convenience method to lookup a factory by type.
   * @param RouteMatcherType the protocol type.
   * @return NamedRouteMatcherConfigFactory& for the RouteMatcherType.
   */
  static NamedRouteMatcherConfigFactory& getFactory(RouteMatcherType type) {
    const std::string& name = RouteMatcherNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactoryByName<NamedRouteMatcherConfigFactory>(name);
  }
};

/**
 * RouteMatcherFactoryBase provides a template for a trivial NamedProtocolConfigFactory.
 */
template <class RouteMatcherImpl>
class RouteMatcherFactoryBase : public NamedRouteMatcherConfigFactory {
public:
  RouteMatcherPtr createRouteMatcher(const RouteConfigurations& route_configs,
                                     Server::Configuration::FactoryContext& context) override {
    return std::make_unique<RouteMatcherImpl>(route_configs, context);
  }

  std::string name() const override { return name_; }

protected:
  RouteMatcherFactoryBase(RouteMatcherType type) : name_(RouteMatcherNames::get().fromType(type)) {}

private:
  const std::string name_;
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
