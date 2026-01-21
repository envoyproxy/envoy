#include "source/extensions/upstreams/http/dynamic_modules/config.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/upstreams/http/dynamic_modules/bridge_config.h"
#include "source/extensions/upstreams/http/dynamic_modules/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

namespace {

// Extract configuration bytes from the Any field.
std::string extractConfigBytes(const Protobuf::Any& any_config) {
  if (any_config.type_url().empty()) {
    return "";
  }

  const std::string& type_url = any_config.type_url();

  // Handle well-known types that can be passed directly as bytes.
  if (type_url == "type.googleapis.com/google.protobuf.StringValue") {
    Protobuf::StringValue string_value;
    if (any_config.UnpackTo(&string_value)) {
      return string_value.value();
    }
  } else if (type_url == "type.googleapis.com/google.protobuf.BytesValue") {
    Protobuf::BytesValue bytes_value;
    if (any_config.UnpackTo(&bytes_value)) {
      return bytes_value.value();
    }
  } else if (type_url == "type.googleapis.com/google.protobuf.Struct") {
    Protobuf::Struct struct_value;
    if (any_config.UnpackTo(&struct_value)) {
      return MessageUtil::getJsonStringFromMessageOrError(struct_value, false);
    }
  }

  // For unknown types, use the serialized bytes.
  return any_config.value();
}

} // namespace

Router::GenericConnPoolPtr DynamicModuleGenericConnPoolFactory::createGenericConnPool(
    Upstream::HostConstSharedPtr, Upstream::ThreadLocalCluster& thread_local_cluster,
    Router::GenericConnPoolFactory::UpstreamProtocol upstream_protocol,
    Upstream::ResourcePriority priority, absl::optional<Envoy::Http::Protocol>,
    Upstream::LoadBalancerContext* ctx, const Protobuf::Message& config) const {

  // Dynamic modules HTTP-TCP bridge only supports HTTP protocol.
  if (upstream_protocol != Router::GenericConnPoolFactory::UpstreamProtocol::HTTP) {
    return nullptr;
  }

  const auto& typed_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::upstreams::http::dynamic_modules::
                                           v3::DynamicModuleHttpTcpBridgeConfig&>(
          config, ProtobufMessage::getStrictValidationVisitor());

  const auto& module_config = typed_config.dynamic_module_config();
  const std::string& module_name = module_config.name();

  // Load the dynamic module.
  auto module_or_error = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_name, module_config.do_not_close(), module_config.load_globally());
  if (!module_or_error.ok()) {
    ENVOY_LOG_MISC(error, "failed to load dynamic module '{}': {}", module_name,
                   module_or_error.status().message());
    return nullptr;
  }

  // Create the bridge configuration.
  std::string config_bytes = extractConfigBytes(typed_config.bridge_config());
  auto config_or_error = DynamicModuleHttpTcpBridgeConfig::create(
      typed_config.bridge_name(), config_bytes, std::move(module_or_error.value()));
  if (!config_or_error.ok()) {
    ENVOY_LOG_MISC(error, "failed to create bridge config for module '{}': {}", module_name,
                   config_or_error.status().message());
    return nullptr;
  }

  auto conn_pool = std::make_unique<DynamicModuleTcpConnPool>(std::move(config_or_error.value()),
                                                              thread_local_cluster, priority, ctx);

  return conn_pool->valid() ? std::move(conn_pool) : nullptr;
}

REGISTER_FACTORY(DynamicModuleGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
