#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"
#include "source/common/matcher/validation_visitor.h"
#include "source/extensions/filters/udp/udp_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

struct RouteActionContext {
  absl::flat_hash_set<std::string> cluster_name_;
};

class RouteMatchAction
    : public Matcher::ActionBase<envoy::extensions::filters::udp::udp_proxy::v3::Route> {
public:
  explicit RouteMatchAction(const std::string& cluster) : cluster_(cluster) {}

  const std::string& cluster() const { return cluster_; }

private:
  const std::string cluster_;
};

class RouteMatchActionFactory : public Matcher::ActionFactory<RouteActionContext> {
public:
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, RouteActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;
  std::string name() const override { return "route"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::udp::udp_proxy::v3::Route>();
  }
};

class RouteActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Network::UdpMatchingData> {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Network::UdpMatchingData>& data_input,
                             absl::string_view type_url) override;
};

class RouterImpl : public Router {
public:
  RouterImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config,
             Server::Configuration::ServerFactoryContext& context);

  // Router::Router
  const std::string route(const Network::Address::Instance& destination_address,
                          const Network::Address::Instance& source_address) const override;
  const std::vector<std::string>& allClusterNames() const override;

private:
  Matcher::MatchTreeSharedPtr<Network::UdpMatchingData> matcher_;
  std::vector<std::string> cluster_names_;
};

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
