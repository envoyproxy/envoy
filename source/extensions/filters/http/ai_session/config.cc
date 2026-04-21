#include "source/extensions/filters/http/ai_session/config.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/ai_session/v3/ai_session.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_session/ai_filter_config_factory.h"
#include "source/extensions/filters/http/ai_session/ai_session_manager.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_connection_manager.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

namespace {

// ---------------------------------------------------------------------------
// AiSessionManagerSlot
//
// ThreadLocal object that holds one AiSessionManager per worker thread.
// Mirrors the pattern used by RateLimitFilter and ExtAuthz.
// ---------------------------------------------------------------------------
struct AiSessionManagerSlot : public ThreadLocal::ThreadLocalObject {
  explicit AiSessionManagerSlot(std::vector<AiFilterFactory> factories)
      : manager(std::move(factories)) {}
  AiSessionManager manager;
};

// ---------------------------------------------------------------------------
// buildFilterChain
//
// Resolves each TypedExtensionConfig entry in ai_session.ai_filters() into
// an AiFilterFactory: looks up the factory by type URL, deserializes the
// typed_config Any, and calls createAiFilterFactory().
// ---------------------------------------------------------------------------
std::vector<AiFilterFactory>
buildFilterChain(const envoy::extensions::filters::http::ai_session::v3::AiSession& proto,
                 ProtobufMessage::ValidationVisitor& validator) {
  std::vector<AiFilterFactory> factories;
  factories.reserve(proto.ai_filters_size());

  for (const auto& entry : proto.ai_filters()) {
    auto* factory =
        Config::Utility::getAndCheckFactory<NamedAiFilterConfigFactory>(entry, false);
    RELEASE_ASSERT(factory != nullptr,
                   absl::StrCat("AI filter factory not found for entry: '", entry.name(), "'"));

    auto config = Config::Utility::translateToFactoryConfig(entry, validator, *factory);

    factories.push_back(factory->createAiFilterFactory(*config));
  }

  return factories;
}

// ---------------------------------------------------------------------------
// AiSessionFilterConfig
//
// Shared config object (one per filter config entry in HCM).
// Holds the TLS slot so the slot's lifetime is tied to the config.
// ---------------------------------------------------------------------------
class AiSessionFilterConfig {
public:
  AiSessionFilterConfig(Server::Configuration::FactoryContext& context,
                        std::vector<AiFilterFactory> filter_factories) {
    tls_slot_ = context.serverFactoryContext().threadLocal().allocateSlot();
    // Capture factories by value — each worker thread gets its own
    // AiSessionManager initialised with the same factory list.
    tls_slot_->set([factories = std::move(filter_factories)](
                       Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<AiSessionManagerSlot>(factories);
    });
  }

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

Http::FilterFactoryCb AiSessionFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ai_session::v3::AiSession& proto_config,
    const std::string& /*stats_prefix*/,
    Server::Configuration::FactoryContext& context) {

  // Build the factory list once at config load time, not per-worker.
  // The factories capture all config state; the AiSessionManager is the
  // only per-worker mutable object.
  auto filter_factories =
      buildFilterChain(proto_config, context.messageValidationVisitor());

  auto config =
      std::make_shared<AiSessionFilterConfig>(context, std::move(filter_factories));

  // FilterFactoryCb — called once per HTTP request on the worker thread.
  // JsonRpcConnectionManager borrows the AiSessionManager& from the TLS slot
  // and calls newStream() for each JSON-RPC request it identifies.
  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamDecoderFilter(
        std::make_shared<JsonRpc::JsonRpcConnectionManager>(config->manager()));
  };
}

REGISTER_FACTORY(AiSessionFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
