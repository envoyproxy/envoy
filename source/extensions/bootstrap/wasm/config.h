#pragma once

#include "envoy/common/pure.h"
#include "envoy/extensions/wasm/v3/wasm.pb.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"

#include "common/protobuf/protobuf.h"

#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::WasmHandle;

class WasmService {
public:
  WasmService(Common::Wasm::PluginSharedPtr plugin, Common::Wasm::WasmHandleSharedPtr singleton)
      : plugin_(plugin), singleton_(std::move(singleton)) {}
  WasmService(Common::Wasm::PluginSharedPtr plugin,
              ThreadLocal::TypedSlotPtr<WasmHandle>&& tls_slot)
      : plugin_(plugin), tls_slot_(std::move(tls_slot)) {}

  virtual ~WasmService() {
    if (singleton_) {
      singleton_->wasm()->startShutdown(plugin_);
    } else if (tls_slot_->currentThreadRegistered()) {
      tls_slot_->runOnAllThreads([plugin = plugin_](OptRef<WasmHandle> handle) {
        if (handle.has_value()) {
          handle->wasm()->startShutdown(plugin);
        }
      });
    }
  }

private:
  Common::Wasm::PluginSharedPtr plugin_;
  Common::Wasm::WasmHandleSharedPtr singleton_;
  ThreadLocal::TypedSlotPtr<WasmHandle> tls_slot_;
};

using WasmServicePtr = std::unique_ptr<WasmService>;
using CreateWasmServiceCallback = std::function<void(WasmServicePtr)>;

class WasmFactory : public Server::Configuration::BootstrapExtensionFactory,
                    Logger::Loggable<Logger::Id::wasm> {
public:
  ~WasmFactory() override = default;
  std::string name() const override { return "envoy.bootstrap.wasm"; }
  void createWasm(const envoy::extensions::wasm::v3::WasmService& config,
                  Server::Configuration::ServerFactoryContext& context,
                  CreateWasmServiceCallback&& cb);
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::wasm::v3::WasmService>();
  }

private:
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

class WasmServiceExtension : public Server::BootstrapExtension {
public:
  WasmService& wasmService() {
    ASSERT(wasm_service_ != nullptr);
    return *wasm_service_;
  }

private:
  WasmServicePtr wasm_service_;
  friend class WasmFactory;
};

} // namespace Wasm
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
