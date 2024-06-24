#pragma once

#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/rds/config_traits.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

class ConfigTraitsImpl : public Rds::ConfigTraits {
public:
  ConfigTraitsImpl(ProtobufMessage::ValidationVisitor& validator) : validator_(validator) {}

  Rds::ConfigConstSharedPtr createNullConfig() const override;
  Rds::ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                         Server::Configuration::ServerFactoryContext& context,
                                         bool validate_clusters_default) const override;

private:
  ProtobufMessage::ValidationVisitor& validator_;
};

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(Rds::ProtoTraits& proto_traits,
                                Server::Configuration::ServerFactoryContext& factory_context)
      : config_traits_(factory_context.messageValidationContext().dynamicValidationVisitor()),
        base_(config_traits_, proto_traits, factory_context) {}

  using VirtualHostMap = std::map<std::string, envoy::config::route::v3::VirtualHost>;

  bool removeVhosts(VirtualHostMap& vhosts,
                    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  bool updateVhosts(VirtualHostMap& vhosts, const VirtualHostRefVector& added_vhosts);
  bool onDemandFetchFailed(const envoy::service::discovery::v3::Resource& resource) const;

  // Router::RouteConfigUpdateReceiver
  bool onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) override;
  bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                    const std::set<std::string>& added_resource_ids,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  uint64_t configHash() const override { return base_.configHash(); }
  const absl::optional<Rds::RouteConfigProvider::ConfigInfo>& configInfo() const override {
    return base_.configInfo();
  }
  bool vhdsConfigurationChanged() const override { return vhds_configuration_changed_; }
  const Protobuf::Message& protobufConfiguration() const override {
    return base_.protobufConfiguration();
  }
  Rds::ConfigConstSharedPtr parsedConfiguration() const override {
    return base_.parsedConfiguration();
  }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }
  const std::set<std::string>& resourceIdsInLastVhdsUpdate() override {
    return resource_ids_in_last_update_;
  }
  const envoy::config::route::v3::RouteConfiguration& protobufConfigurationCast() const override {
    ASSERT(dynamic_cast<const envoy::config::route::v3::RouteConfiguration*>(
        &RouteConfigUpdateReceiverImpl::protobufConfiguration()));
    return static_cast<const envoy::config::route::v3::RouteConfiguration&>(
        RouteConfigUpdateReceiverImpl::protobufConfiguration());
  }

private:
  ConfigTraitsImpl config_traits_;

  Rds::RouteConfigUpdateReceiverImpl base_;

  uint64_t last_vhds_config_hash_{0ul};
  // vhosts supplied by RDS, to be merged with VHDS vhosts in onVhdsUpdate.
  std::unique_ptr<VirtualHostMap> rds_virtual_hosts_;
  // vhosts supplied by VHDS, to be merged with RDS vhosts in onRdsUpdate.
  std::unique_ptr<VirtualHostMap> vhds_virtual_hosts_;
  std::set<std::string> resource_ids_in_last_update_;
  bool vhds_configuration_changed_{true};
};

} // namespace Router
} // namespace Envoy
