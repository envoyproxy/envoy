#pragma once

#include <cstdlib>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/lifecycle_notifier.h"

#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/golang/v3alpha/golang.pb.h"
#include "contrib/envoy/extensions/filters/network/golang/v3alpha/golang.pb.validate.h"
#include "contrib/golang/common/dso/dso.h"
#include "contrib/golang/filters/network/source/golang.h"
#include "contrib/golang/filters/network/source/upstream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

constexpr char CanonicalName[] = "envoy.filters.network.golang";

/**
 * Config registration for the golang filter. @see NamedNetworkFilterConfigFactory.
 */
class GolangConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::golang::v3alpha::Config> {
public:
  GolangConfigFactory() : FactoryBase(CanonicalName) {}

  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return is_terminal_filter_;
  }

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::golang::v3alpha::Config& proto_config,
      Server::Configuration::FactoryContext& context) override;

  bool is_terminal_filter_{true};
};

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
