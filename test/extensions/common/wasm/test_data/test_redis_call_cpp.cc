// NOLINT(namespace-envoy)
#include <string>
#include <tuple>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

#include "source/extensions/common/wasm/ext/redis_call.pb.h"

START_WASM_PLUGIN(RedisCallNullPlugin)

// A minimal module exercising the real redis_call shared-queue delivery path -- in particular
// SharedQueue::enqueue()'s cross-thread scheduling via getThreadLocalWasm() and dispatch into a
// real module's proxy_on_queue_ready -- which the lighter-weight RedisCallForeignTest fixture
// never exercises, since it calls ContextBase::onQueueReady() directly on the host-side Context
// instead of going through a loaded module (see that fixture's deliverViaOnQueueReady() comment).
class RedisCallNullPluginRootContext : public RootContext {
public:
  explicit RedisCallNullPluginRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}

  bool onConfigure(size_t configuration_size) override;
  void onQueueReady(uint32_t token) override;
};

static RegisterContextFactory
    register_RedisCallNullPluginRootContext(CONTEXT_FACTORY(Context),
                                             ROOT_FACTORY(RedisCallNullPluginRootContext));

// Exercises the real proxy_call_foreign_function ABI boundary from guest code -- guest-side
// protobuf serialization of RedisCallArguments, the foreign-function lookup/invocation itself,
// and guest-side parsing of the returned RedisCallStartResult -- rather than the host-side-direct
// invocation RedisCallNullVmTest/RedisCallForeignTest's callRedis() test helpers use. Only runs
// when the plugin is configured with a serialized RedisCallArguments payload; tests that load
// this plugin with an empty configuration (and drive calls via callRedis() instead) are
// unaffected.
bool RedisCallNullPluginRootContext::onConfigure(size_t configuration_size) {
  if (configuration_size == 0) {
    return true;
  }
  auto configuration = getBufferBytes(WasmBufferType::PluginConfiguration, 0, configuration_size);
  envoy::source::extensions::common::wasm::RedisCallArguments args;
  if (!args.ParseFromString(configuration->toString())) {
    logWarn("RedisCallNullPluginRootContext::onConfigure parse_failure");
    return true;
  }
  std::string serialized_args;
  std::ignore = args.SerializeToString(&serialized_args);
  std::string function_name = "redis_call";
  char* response = nullptr;
  size_t response_size = 0;
  WasmResult call_result = proxy_call_foreign_function(
      function_name.data(), function_name.size(), serialized_args.data(), serialized_args.size(),
      &response, &response_size);
  if (call_result != WasmResult::Ok) {
    logWarn("RedisCallNullPluginRootContext::onConfigure start_status=" +
            std::to_string(static_cast<int>(call_result)));
    return true;
  }
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  if (!start_result.ParseFromString(std::string(response, response_size))) {
    logWarn("RedisCallNullPluginRootContext::onConfigure start_parse_failure");
  } else {
    logWarn("RedisCallNullPluginRootContext::onConfigure start_ok queue_token=" +
            std::to_string(start_result.queue_token()) +
            " call_id=" + std::to_string(start_result.call_id()));
  }
  if (response != nullptr) {
    ::free(response);
  }
  return true;
}

void RedisCallNullPluginRootContext::onQueueReady(uint32_t token) {
  // Per the redis_call contract, a module must dequeue in a loop until empty rather than assume
  // exactly one result per onQueueReady invocation.
  WasmDataPtr data;
  while (dequeueSharedQueue(token, &data) == WasmResult::Ok) {
    envoy::source::extensions::common::wasm::RedisCallResult result;
    if (!result.ParseFromString(data->toString())) {
      logWarn("RedisCallNullPluginRootContext::onQueueReady parse_failure");
      continue;
    }
    logWarn("RedisCallNullPluginRootContext::onQueueReady status=" +
            std::to_string(static_cast<int>(result.status())) +
            " response=" + result.response());
  }
}

END_WASM_PLUGIN
