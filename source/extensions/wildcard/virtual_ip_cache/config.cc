#include "source/extensions/wildcard/virtual_ip_cache/config.h"

#include "envoy/extensions/wildcard/virtual_ip_cache/v3/virtual_ip_cache.pb.h"
#include "envoy/extensions/wildcard/virtual_ip_cache/v3/virtual_ip_cache.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace VirtualIp {

SINGLETON_MANAGER_REGISTRATION(virtual_ip_cache_manager);

/**
 * Bootstrap extension that creates the VirtualIpCacheManager singleton.
 */
class VirtualIpCacheBootstrap : public Server::BootstrapExtension {
public:
  VirtualIpCacheBootstrap(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::wildcard::virtual_ip_cache::v3::VirtualIpCache& config)
      : cache_manager_(std::make_shared<VirtualIpCacheManager>(
            context.threadLocal(), context.mainThreadDispatcher(), context.timeSource(),
            config.cidr_block().address_prefix(), config.cidr_block().prefix_len().value())) {
    context.singletonManager().getTyped<VirtualIpCacheManager>(
        SINGLETON_MANAGER_REGISTERED_NAME(virtual_ip_cache_manager),
        [cache_manager = cache_manager_]() { return cache_manager; });
  }

  void onServerInitialized() override {}

  void onWorkerThreadInitialized() override {}

private:
  VirtualIpCacheManagerSharedPtr cache_manager_;
};

Server::BootstrapExtensionPtr VirtualIpCacheConfigFactory::createBootstrapExtension(
    const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context) {

  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::wildcard::virtual_ip_cache::v3::VirtualIpCache&>(
      proto_config, context.messageValidationVisitor());

  return std::make_unique<VirtualIpCacheBootstrap>(context, config);
}

ProtobufTypes::MessagePtr VirtualIpCacheConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::wildcard::virtual_ip_cache::v3::VirtualIpCache>();
}

std::string VirtualIpCacheConfigFactory::name() const { return "envoy.bootstrap.virtual_ip_cache"; }

REGISTER_FACTORY(VirtualIpCacheConfigFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace VirtualIp
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
