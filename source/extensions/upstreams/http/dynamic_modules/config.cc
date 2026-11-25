#include "source/extensions/upstreams/http/dynamic_modules/config.h"

#include "source/common/common/macros.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

namespace {

// The cluster stores its options under the name of the factory, which every request looks up.
const std::string& factoryName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.upstreams.http.dynamic_modules");
}

// The bridge configuration that a cluster creates on the main thread, which its workers share.
class BridgeClusterOptions : public Upstream::ProtocolOptionsConfig {
public:
  explicit BridgeClusterOptions(BridgeConfigSharedPtr bridge_config)
      : bridge_config_(std::move(bridge_config)) {}

  const BridgeConfigSharedPtr bridge_config_;
};

} // namespace

std::string DynamicModuleGenericConnPoolFactory::name() const { return factoryName(); }

absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr>
DynamicModuleGenericConnPoolFactory::createClusterOptions(
    const Protobuf::Any& typed_config, Server::Configuration::ServerFactoryContext& context) const {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  envoy::extensions::upstreams::http::dynamic_modules::v3::Config proto_config;
  RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      typed_config, context.messageValidationVisitor(), proto_config));

  // Without a factory context, newDynamicModuleByConfig rejects remote module sources, which a
  // cluster could not wait for.
  auto load_result = Envoy::Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.bridge_name());
  RETURN_IF_NOT_OK_REF(load_result.status());

  std::string bridge_config_bytes;
  if (proto_config.has_bridge_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.bridge_config());
    if (!config_or_error.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to parse bridge_config: ", config_or_error.status().message()));
    }
    bridge_config_bytes = std::move(config_or_error.value());
  }

  auto bridge_config = BridgeConfig::create(proto_config.bridge_name(), bridge_config_bytes,
                                            std::move(load_result->loaded));
  RETURN_IF_NOT_OK_REF(bridge_config.status());
  return std::make_shared<const BridgeClusterOptions>(std::move(bridge_config.value()));
}

Router::GenericConnPoolPtr DynamicModuleGenericConnPoolFactory::createGenericConnPool(
    Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& thread_local_cluster,
    Router::GenericConnPoolFactory::UpstreamProtocol, Upstream::ResourcePriority priority,
    std::optional<Envoy::Http::Protocol>, Upstream::LoadBalancerContext* ctx,
    const Protobuf::Message&) const {
  const auto options =
      thread_local_cluster.info()->extensionProtocolOptionsTyped<BridgeClusterOptions>(
          factoryName());
  if (options == nullptr) {
    ENVOY_BUG(false, "the cluster has no dynamic module bridge configuration");
    return nullptr;
  }

  auto ret = std::make_unique<TcpConnPool>(host, thread_local_cluster, priority, ctx,
                                           options->bridge_config_);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(DynamicModuleGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
