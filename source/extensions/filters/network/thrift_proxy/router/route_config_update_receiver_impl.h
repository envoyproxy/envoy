#pragma once

#include <string>

#include "envoy/server/factory_context.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/thrift_proxy/router/route_config_update_receiver.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(Server::Configuration::ServerFactoryContext& factory_context)
      : time_source_(factory_context.timeSource()),
        route_config_proto_(
            std::make_unique<
                envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>()),
        last_config_hash_(0ull) {}

  // Router::RouteConfigUpdateReceiver
  bool
  onRdsUpdate(const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& rc,
              const std::string& version_info) override;
  const std::string& routeConfigName() const override { return route_config_proto_->name(); }
  const std::string& configVersion() const override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override {
    return config_info_;
  }
  const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&
  protobufConfiguration() override {
    return static_cast<
        const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&>(
        *route_config_proto_);
  }
  ConfigConstSharedPtr parsedConfiguration() const override { return config_; }
  SystemTime lastUpdated() const override { return last_updated_; }

private:
  TimeSource& time_source_;
  std::unique_ptr<envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>
      route_config_proto_;
  uint64_t last_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  ConfigConstSharedPtr config_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
