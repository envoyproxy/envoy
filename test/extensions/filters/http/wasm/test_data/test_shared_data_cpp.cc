// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_lite.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(HttpWasmTestCpp)

class SharedDataRootContext : public RootContext {
public:
  explicit SharedDataRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}

  void onTick() override;
  void onQueueReady(uint32_t) override;
};

static RegisterContextFactory register_SharedDataRootContext(ROOT_FACTORY(SharedDataRootContext),
                                                             "shared_data");

void SharedDataRootContext::onTick() {
  setHeaderMapPairs(WasmHeaderMapType::GrpcReceiveInitialMetadata, {});
  setRequestHeaderPairs({{"foo", "bar"}});
  WasmDataPtr value0;
  if (getSharedData("shared_data_key_bad", &value0) == WasmResult::NotFound) {
    logDebug("get of bad key not found");
  }
  CHECK_RESULT(setSharedData("shared_data_key1", "shared_data_value0"));
  CHECK_RESULT(setSharedData("shared_data_key1", "shared_data_value1"));
  CHECK_RESULT(setSharedData("shared_data_key2", "shared_data_value2"));
  uint32_t cas = 0;
  auto value2 = getSharedDataValue("shared_data_key2", &cas);
  if (WasmResult::CasMismatch ==
      setSharedData("shared_data_key2", "shared_data_value3", cas + 1)) { // Bad cas.
    logInfo("set CasMismatch");
  }
}

void SharedDataRootContext::onQueueReady(uint32_t) {
  WasmDataPtr value0;
  if (getSharedData("shared_data_key_bad", &value0) == WasmResult::NotFound) {
    logDebug("second get of bad key not found");
  }
  auto value1 = getSharedDataValue("shared_data_key1");
  logDebug("get 1 " + value1->toString());
  auto value2 = getSharedDataValue("shared_data_key2");
  logCritical("get 2 " + value2->toString());
}

END_WASM_PLUGIN
