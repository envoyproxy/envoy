#pragma once

#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/dispatcher_thread_deletable.h"
#include "envoy/http/codes.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {

using OnAiFilterConfigNewType = decltype(&envoy_dynamic_module_on_ai_filter_config_new);
using OnAiFilterConfigDestroyType = decltype(&envoy_dynamic_module_on_ai_filter_config_destroy);
using OnAiFilterNewType = decltype(&envoy_dynamic_module_on_ai_filter_new);
using OnAiFilterDestroyType = decltype(&envoy_dynamic_module_on_ai_filter_destroy);
using OnAiFilterRequestType = decltype(&envoy_dynamic_module_on_ai_filter_request);

class DynamicModuleAiFilterConfig;
using DynamicModuleAiFilterConfigSharedPtr = std::shared_ptr<DynamicModuleAiFilterConfig>;

// A module's AI filter configuration, shared by the filter of every stream.
class DynamicModuleAiFilterConfig : public Event::DispatcherThreadDeletable {
public:
  // Resolves the AI filter hooks of `dynamic_module` and creates its configuration for the filter
  // `name`. NotFound when a hook is missing, which tells a broken module apart from a configuration
  // the module rejected (InvalidArgument). Called on the main thread; the configuration is
  // destroyed on `main_thread_dispatcher`, which must outlive it.
  static absl::StatusOr<DynamicModuleAiFilterConfigSharedPtr>
  create(absl::string_view name, absl::string_view config,
         Extensions::DynamicModules::DynamicModulePtr dynamic_module,
         Event::Dispatcher& main_thread_dispatcher);

  ~DynamicModuleAiFilterConfig() override;

private:
  friend class DynamicModuleAiFilter;

  struct Hooks {
    OnAiFilterConfigDestroyType config_destroy{nullptr};
    OnAiFilterNewType filter_new{nullptr};
    OnAiFilterDestroyType filter_destroy{nullptr};
    OnAiFilterRequestType request{nullptr};
  };

  DynamicModuleAiFilterConfig(Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                              const Hooks& hooks);

  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  const Hooks hooks_;
  envoy_dynamic_module_type_ai_filter_config_module_ptr in_module_config_{nullptr};
};

// The AI filter of one stream, backed by an in-module filter. The ABI callbacks reach the stream
// through it.
class DynamicModuleAiFilter : public Common::SyncAiFilter,
                              public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  // Creates the filter of a stream, or returns nullptr when the module skips the stream.
  static std::shared_ptr<DynamicModuleAiFilter>
  create(DynamicModuleAiFilterConfigSharedPtr config,
         const HttpFilters::AiProtocolManager::AiFilterContext& context);

  DynamicModuleAiFilter(DynamicModuleAiFilterConfigSharedPtr config,
                        const HttpFilters::AiProtocolManager::AiFilterContext& context);
  ~DynamicModuleAiFilter() override;

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

  const HttpFilters::AiProtocolManager::AiFilterContext& context() const { return context_; }

  // The request in scope; null outside the request hook.
  HttpFilters::AiProtocolManager::AiRequest* request() const { return request_; }

  // The last payload value the module read, which it borrows until its next read.
  std::string& jsonResult() { return json_result_; }

  // False outside the request hook, or once a reply was sent.
  bool sendLocalReply(Http::Code code, absl::string_view body);

private:
  const DynamicModuleAiFilterConfigSharedPtr config_;
  const HttpFilters::AiProtocolManager::AiFilterContext context_;
  envoy_dynamic_module_type_ai_filter_module_ptr in_module_filter_{nullptr};
  HttpFilters::AiProtocolManager::AiRequest* request_{nullptr};
  HttpFilters::AiProtocolManager::LocalReplier reply_locally_;
  std::string json_result_;
};

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
