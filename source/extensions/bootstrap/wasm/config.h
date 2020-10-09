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

class WasmService {
public:
  WasmService(Common::Wasm::WasmHandleSharedPtr singleton) : singleton_(std::move(singleton)) {}
  WasmService(ThreadLocal::SlotPtr tls_slot) : tls_slot_(std::move(tls_slot)) {}

private:
  Common::Wasm::WasmHandleSharedPtr singleton_;
  ThreadLocal::SlotPtr tls_slot_;
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
