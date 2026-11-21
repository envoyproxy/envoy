#include "source/extensions/http/ai_filters/dynamic_modules/ai_filter.h"

#include <utility>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {
namespace {

using Extensions::DynamicModules::DynamicModule;
using Extensions::DynamicModules::DynamicModulePtr;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::LocalReplier;

template <class Hook>
absl::Status resolveHook(const DynamicModule& module, absl::string_view symbol, Hook& hook) {
  absl::StatusOr<Hook> resolved = module.getFunctionPointer<Hook>(symbol);
  if (!resolved.ok()) {
    return absl::NotFoundError(resolved.status().message());
  }
  hook = *resolved;
  return absl::OkStatus();
}

} // namespace

DynamicModuleAiFilterConfig::DynamicModuleAiFilterConfig(DynamicModulePtr dynamic_module,
                                                         const Hooks& hooks)
    : dynamic_module_(std::move(dynamic_module)), hooks_(hooks) {}

DynamicModuleAiFilterConfig::~DynamicModuleAiFilterConfig() {
  if (in_module_config_ != nullptr) {
    hooks_.config_destroy(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleAiFilterConfigSharedPtr>
DynamicModuleAiFilterConfig::create(absl::string_view name, absl::string_view config,
                                    DynamicModulePtr dynamic_module,
                                    Event::Dispatcher& main_thread_dispatcher) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  OnAiFilterConfigNewType config_new = nullptr;
  Hooks hooks;
  RETURN_IF_NOT_OK(
      resolveHook(*dynamic_module, "envoy_dynamic_module_on_ai_filter_config_new", config_new));
  RETURN_IF_NOT_OK(resolveHook(*dynamic_module, "envoy_dynamic_module_on_ai_filter_config_destroy",
                               hooks.config_destroy));
  RETURN_IF_NOT_OK(
      resolveHook(*dynamic_module, "envoy_dynamic_module_on_ai_filter_new", hooks.filter_new));
  RETURN_IF_NOT_OK(resolveHook(*dynamic_module, "envoy_dynamic_module_on_ai_filter_destroy",
                               hooks.filter_destroy));
  RETURN_IF_NOT_OK(
      resolveHook(*dynamic_module, "envoy_dynamic_module_on_ai_filter_request", hooks.request));

  // Streams keep the configuration alive past a config update and drop it on worker threads, so
  // the destroy hook and the dlclose() behind the module run on the main thread instead.
  DynamicModuleAiFilterConfigSharedPtr filter_config(
      new DynamicModuleAiFilterConfig(std::move(dynamic_module), hooks),
      [&main_thread_dispatcher](DynamicModuleAiFilterConfig* config) {
        main_thread_dispatcher.deleteInDispatcherThread(
            std::unique_ptr<const Event::DispatcherThreadDeletable>(config));
      });
  filter_config->in_module_config_ =
      config_new(filter_config.get(), {name.data(), name.size()}, {config.data(), config.size()});
  if (filter_config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("dynamic module rejected the configuration of AI filter '", name, "'"));
  }
  return filter_config;
}

std::shared_ptr<DynamicModuleAiFilter>
DynamicModuleAiFilter::create(DynamicModuleAiFilterConfigSharedPtr config,
                              const AiFilterContext& context) {
  auto filter = std::make_shared<DynamicModuleAiFilter>(std::move(config), context);
  // The module may call back into the filter from its new hook, so the filter must exist first.
  envoy_dynamic_module_type_ai_filter_module_ptr in_module_filter = nullptr;
  filter->in_hook_ = true;
  const envoy_dynamic_module_type_on_ai_filter_new_status status =
      filter->config_->hooks_.filter_new(filter->config_->in_module_config_, filter.get(),
                                         &in_module_filter);
  filter->in_hook_ = false;
  if (status == envoy_dynamic_module_type_on_ai_filter_new_status_Skip) {
    ENVOY_LOG(debug, "dynamic module AI filter skipped the stream");
    return nullptr;
  }
  if (status == envoy_dynamic_module_type_on_ai_filter_new_status_Created &&
      in_module_filter != nullptr) {
    filter->in_module_filter_ = in_module_filter;
  } else {
    ENVOY_LOG(debug, "dynamic module failed to create the AI filter of the stream");
  }
  return filter;
}

DynamicModuleAiFilter::DynamicModuleAiFilter(DynamicModuleAiFilterConfigSharedPtr config,
                                             const AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

DynamicModuleAiFilter::~DynamicModuleAiFilter() {
  if (in_module_filter_ != nullptr) {
    config_->hooks_.filter_destroy(in_module_filter_);
  }
}

absl::Status DynamicModuleAiFilter::decodeSync(AiRequest& request, LocalReplier reply_locally) {
  if (in_module_filter_ == nullptr) {
    return absl::InternalError("dynamic module failed to create the AI filter of the stream");
  }
  request_ = &request;
  reply_locally_ = std::move(reply_locally);
  in_hook_ = true;
  const envoy_dynamic_module_type_on_ai_filter_request_status status =
      config_->hooks_.request(this, in_module_filter_);
  in_hook_ = false;
  const bool replied_locally = reply_locally_ == nullptr;
  request_ = nullptr;
  reply_locally_ = nullptr;
  // Frees a large read once the module can no longer borrow it, rather than at stream end.
  json_result_ = std::string();
  if (replied_locally || status == envoy_dynamic_module_type_on_ai_filter_request_status_Continue) {
    return absl::OkStatus();
  }
  return absl::InternalError("dynamic module AI filter failed the request");
}

bool DynamicModuleAiFilter::sendLocalReply(Http::Code code, absl::string_view body) {
  if (reply_locally_ == nullptr) {
    return false;
  }
  LocalReplier reply_locally = std::exchange(reply_locally_, nullptr);
  std::move(reply_locally)(code, std::string(body));
  return true;
}

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
