#include "source/extensions/filters/http/ai_session/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/filters/http/ai_session/ai_session_manager.h"
#include "source/extensions/filters/http/ai_session/example_filters.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_connection_manager.h"

#include "google/protobuf/empty.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

namespace {

// ---------------------------------------------------------------------------
// AiSessionManagerSlot
//
// ThreadLocal object that holds one AiSessionManager per worker thread.
//
// Mirrors the pattern used by RateLimitFilter and ExtAuthz: shared config is
// stored in a ThreadLocal::Slot so each worker thread has its own instance of
// the mutable state (the session map) without any locking.
//
// Concurrency model:
//   - Each worker thread handles a disjoint set of connections.
//   - Sessions keyed by Mcp-Session-Id are therefore per-worker.
//   - Two reconnecting clients that land on different workers will not share
//     session state — the same limitation as any in-process session store.
//     If cross-worker session sharing is needed, replace AiSessionManager with
//     an external store (Redis, gRPC) and remove ThreadLocal.
// ---------------------------------------------------------------------------
struct AiSessionManagerSlot : public ThreadLocal::ThreadLocalObject {
  explicit AiSessionManagerSlot(std::vector<AiFilterFactory> factories)
      : manager(std::move(factories)) {}
  AiSessionManager manager;
};

// Build the default AI filter chain factories.
// Analogous to the list of http_filters in HCM bootstrap YAML.
// Replace or extend this list to change the per-request processing pipeline.
std::vector<AiFilterFactory> defaultFilterFactories() {
  return {
      []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpAuthFilter>(); },
      []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpInitFilter>(); },
      []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpContextFilter>(); },
  };
}

// ---------------------------------------------------------------------------
// AiSessionFilterConfig
//
// Shared config object (one per filter config entry in HCM).
// Holds the TLS slot so the slot's lifetime is tied to the config, not to any
// individual request or worker — mirrors how ExtAuthzFilter stores its config.
// ---------------------------------------------------------------------------
class AiSessionFilterConfig {
public:
  explicit AiSessionFilterConfig(Server::Configuration::FactoryContext& context) {
    tls_slot_ = context.threadLocal().allocateSlot();
    tls_slot_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<AiSessionManagerSlot>(defaultFilterFactories());
    });
  }

  // Returns the AiSessionManager for the calling worker thread.
  // Safe to call from any worker without locking.
  AiSessionManager& manager() {
    return tls_slot_->getTyped<AiSessionManagerSlot>().manager;
  }

private:
  ThreadLocal::SlotPtr tls_slot_;
};

} // namespace

// ---------------------------------------------------------------------------
// AiSessionFilterFactory
// ---------------------------------------------------------------------------

Http::FilterFactoryCb AiSessionFilterFactory::createFilterFactoryFromProto(
    const Protobuf::Message& /*proto_config*/,
    const std::string& /*stats_prefix*/,
    Server::Configuration::FactoryContext& context) {

  // Config is heap-allocated and shared across all FilterFactoryCb invocations
  // (i.e., across all HTTP requests on all workers for this filter config entry).
  // Mirrors how ExtAuthzFilterConfig is stored as a shared_ptr in the factory.
  auto config = std::make_shared<AiSessionFilterConfig>(context);

  // FilterFactoryCb — called once per HTTP request on the worker thread.
  //
  // Full call path once this filter is installed:
  //
  //   HCM::onData()
  //     → HTTP codec
  //       → ActiveStream::decodeHeaders()          [Envoy filter manager]
  //         → JsonRpcConnectionManager::decodeHeaders()
  //             isJsonRpcContentType? → no  → Continue (pass through)
  //                                  → yes → manager_.newStream(headers)
  //                                           → AiSessionManager::newStream()
  //                                               → getOrCreateSession()
  //                                               → AiFilterChain (McpAuth→McpInit→McpContext)
  //       → ActiveStream::decodeData()
  //         → JsonRpcConnectionManager::decodeData()
  //             → JsonRpcParser::parse()
  //                 → AiFilterChain::onMethod / onParams / onJsonRpcComplete
  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamDecoderFilter(
        std::make_shared<JsonRpc::JsonRpcConnectionManager>(config->manager()));
  };
}

ProtobufTypes::MessagePtr AiSessionFilterFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

REGISTER_FACTORY(AiSessionFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
