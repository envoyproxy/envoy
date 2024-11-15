#pragma once

#include "envoy/common/pure.h"
#include "envoy/extensions/wasm/v3/wasm.pb.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"
#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Wasm {

using Common::Wasm::PluginConfig;
using Common::Wasm::PluginConfigPtr;
using Common::Wasm::PluginHandleSharedPtrThreadLocal;
using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtr;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;

class WasmFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  ~WasmFactory() override = default;
  std::string name() const override { return "envoy.bootstrap.wasm"; }
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::wasm::v3::WasmService>();
  }
};

class WasmServiceExtension : public Server::BootstrapExtension, Logger::Loggable<Logger::Id::wasm> {
public:
  WasmServiceExtension(const envoy::extensions::wasm::v3::WasmService& config,
                       Server::Configuration::ServerFactoryContext& context)
      : config_(config), context_(context) {}
  PluginConfig& wasmService() {
    ASSERT(plugin_config_ != nullptr);
    return *plugin_config_;
  }
  void onServerInitialized() override;

private:
  void createWasm(Server::Configuration::ServerFactoryContext& context);

  envoy::extensions::wasm::v3::WasmService config_;
  Server::Configuration::ServerFactoryContext& context_;
  PluginConfigPtr plugin_config_;
};

} // namespace Wasm
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
