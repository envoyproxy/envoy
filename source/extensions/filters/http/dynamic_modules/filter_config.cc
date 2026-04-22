#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

#include <cstdint>

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

DynamicModuleHttpFilterConfig::DynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
    Server::Configuration::ServerFactoryContext& context)
    : cluster_manager_(context.clusterManager()),
      main_thread_dispatcher_(context.mainThreadDispatcher()),
      stats_scope_(stats_scope.createScope(absl::StrCat(metrics_namespace, "."))),
      stat_name_pool_(stats_scope_->symbolTable()), filter_name_(filter_name),
      filter_config_(filter_config), metrics_namespace_(metrics_namespace),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleHttpFilterConfig::~DynamicModuleHttpFilterConfig() {
  // When the initialization of the dynamic module fails, the in_module_config_ is nullptr,
  // and there's nothing to destroy from the module's point of view.
  if (on_http_filter_config_destroy_) {
    (*on_http_filter_config_destroy_)(in_module_config_);
  }
  // Null out in_module_config_ so that pending callout/stream callbacks won't invoke a destroyed
  // module.
  in_module_config_ = nullptr;

  // Cancel all pending one-shot callouts.
  while (!http_callouts_.empty()) {
    auto it = http_callouts_.begin();
    auto callout = std::move(it->second);
    http_callouts_.erase(it);
    if (callout->request_ != nullptr) {
      auto request = callout->request_;
      callout->request_ = nullptr;
      request->cancel();
    }
  }

  // Reset all pending streams.
  while (!http_stream_callouts_.empty()) {
    auto it = http_stream_callouts_.begin();
    auto callout = std::move(it->second);
    http_stream_callouts_.erase(it);
    if (callout->stream_ != nullptr) {
      auto stream = callout->stream_;
      callout->stream_ = nullptr;
      stream->reset();
    }
  }
}

DynamicModuleHttpPerRouteFilterConfig::~DynamicModuleHttpPerRouteFilterConfig() {
  (*destroy_)(config_);
}

absl::StatusOr<DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
newDynamicModuleHttpPerRouteConfig(const absl::string_view filter_name,
                                   const absl::string_view filter_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  auto constructor =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_http_filter_per_route_config_new)>(
              "envoy_dynamic_module_on_http_filter_per_route_config_new");
  RETURN_IF_NOT_OK_REF(constructor.status());

  auto destroy = dynamic_module->getFunctionPointer<OnHttpPerRouteConfigDestroyType>(
      "envoy_dynamic_module_on_http_filter_per_route_config_destroy");
  RETURN_IF_NOT_OK_REF(destroy.status());

  const void* filter_config_envoy_ptr = (*constructor.value())(
      {filter_name.data(), filter_name.size()}, {filter_config.data(), filter_config.size()});
  if (filter_config_envoy_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize per-route dynamic module");
  }

  return std::make_shared<const DynamicModuleHttpPerRouteFilterConfig>(
      filter_config_envoy_ptr, destroy.value(), std::move(dynamic_module));
}

absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr> newDynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    const absl::string_view metrics_namespace, const bool terminal_filter,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
    Server::Configuration::ServerFactoryContext& context) {
  auto constructor =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_http_filter_config_new)>(
          "envoy_dynamic_module_on_http_filter_config_new");
  RETURN_IF_NOT_OK_REF(constructor.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnHttpConfigDestroyType>(
      "envoy_dynamic_module_on_http_filter_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_new_filter = dynamic_module->getFunctionPointer<OnHttpFilterNewType>(
      "envoy_dynamic_module_on_http_filter_new");
  RETURN_IF_NOT_OK_REF(on_new_filter.status());

  auto on_request_headers = dynamic_module->getFunctionPointer<OnHttpFilterRequestHeadersType>(
      "envoy_dynamic_module_on_http_filter_request_headers");
  RETURN_IF_NOT_OK_REF(on_request_headers.status());

  auto on_request_body = dynamic_module->getFunctionPointer<OnHttpFilterRequestBodyType>(
      "envoy_dynamic_module_on_http_filter_request_body");
  RETURN_IF_NOT_OK_REF(on_request_body.status());

  auto on_request_trailers = dynamic_module->getFunctionPointer<OnHttpFilterRequestTrailersType>(
      "envoy_dynamic_module_on_http_filter_request_trailers");
  RETURN_IF_NOT_OK_REF(on_request_trailers.status());

  auto on_response_headers = dynamic_module->getFunctionPointer<OnHttpFilterResponseHeadersType>(
      "envoy_dynamic_module_on_http_filter_response_headers");
  RETURN_IF_NOT_OK_REF(on_response_headers.status());

  auto on_response_body = dynamic_module->getFunctionPointer<OnHttpFilterResponseBodyType>(
      "envoy_dynamic_module_on_http_filter_response_body");
  RETURN_IF_NOT_OK_REF(on_response_body.status());

  auto on_response_trailers = dynamic_module->getFunctionPointer<OnHttpFilterResponseTrailersType>(
      "envoy_dynamic_module_on_http_filter_response_trailers");
  RETURN_IF_NOT_OK_REF(on_response_trailers.status());

  auto on_filter_stream_complete =
      dynamic_module->getFunctionPointer<OnHttpFilterStreamCompleteType>(
          "envoy_dynamic_module_on_http_filter_stream_complete");
  RETURN_IF_NOT_OK_REF(on_filter_stream_complete.status());

  auto on_filter_destroy = dynamic_module->getFunctionPointer<OnHttpFilterDestroyType>(
      "envoy_dynamic_module_on_http_filter_destroy");
  RETURN_IF_NOT_OK_REF(on_filter_destroy.status());

  auto on_http_callout_done = dynamic_module->getFunctionPointer<OnHttpFilterHttpCalloutDoneType>(
      "envoy_dynamic_module_on_http_filter_http_callout_done");
  RETURN_IF_NOT_OK_REF(on_http_callout_done.status());

  auto on_http_stream_headers =
      dynamic_module->getFunctionPointer<OnHttpFilterHttpStreamHeadersType>(
          "envoy_dynamic_module_on_http_filter_http_stream_headers");
  RETURN_IF_NOT_OK_REF(on_http_stream_headers.status());

  auto on_http_stream_data = dynamic_module->getFunctionPointer<OnHttpFilterHttpStreamDataType>(
      "envoy_dynamic_module_on_http_filter_http_stream_data");
  RETURN_IF_NOT_OK_REF(on_http_stream_data.status());

  auto on_http_stream_trailers =
      dynamic_module->getFunctionPointer<OnHttpFilterHttpStreamTrailersType>(
          "envoy_dynamic_module_on_http_filter_http_stream_trailers");
  RETURN_IF_NOT_OK_REF(on_http_stream_trailers.status());

  auto on_http_stream_complete =
      dynamic_module->getFunctionPointer<OnHttpFilterHttpStreamCompleteType>(
          "envoy_dynamic_module_on_http_filter_http_stream_complete");
  RETURN_IF_NOT_OK_REF(on_http_stream_complete.status());

  auto on_http_stream_reset = dynamic_module->getFunctionPointer<OnHttpFilterHttpStreamResetType>(
      "envoy_dynamic_module_on_http_filter_http_stream_reset");
  RETURN_IF_NOT_OK_REF(on_http_stream_reset.status());

  auto on_scheduled = dynamic_module->getFunctionPointer<OnHttpFilterScheduled>(
      "envoy_dynamic_module_on_http_filter_scheduled");
  RETURN_IF_NOT_OK_REF(on_scheduled.status());

  // These are optional. Modules that don't need config-level scheduling or config-level
  // callouts/streams don't need to implement them.
  auto on_config_scheduled = dynamic_module->getFunctionPointer<OnHttpFilterConfigScheduled>(
      "envoy_dynamic_module_on_http_filter_config_scheduled");
  auto on_config_http_callout_done =
      dynamic_module->getFunctionPointer<OnHttpFilterConfigHttpCalloutDoneType>(
          "envoy_dynamic_module_on_http_filter_config_http_callout_done");
  auto on_config_http_stream_headers =
      dynamic_module->getFunctionPointer<OnHttpFilterConfigHttpStreamHeadersType>(
          "envoy_dynamic_module_on_http_filter_config_http_stream_headers");
  auto on_config_http_stream_data =
      dynamic_module->getFunctionPointer<OnHttpFilterConfigHttpStreamDataType>(
          "envoy_dynamic_module_on_http_filter_config_http_stream_data");
  auto on_config_http_stream_trailers =
      dynamic_module->getFunctionPointer<OnHttpFilterConfigHttpStreamTrailersType>(
          "envoy_dynamic_module_on_http_filter_config_http_stream_trailers");
  auto on_config_http_stream_complete =
      dynamic_module->getFunctionPointer<OnHttpFilterConfigHttpStreamCompleteType>(
          "envoy_dynamic_module_on_http_filter_config_http_stream_complete");
  auto on_config_http_stream_reset =
      dynamic_module->getFunctionPointer<OnHttpFilterConfigHttpStreamResetType>(
          "envoy_dynamic_module_on_http_filter_config_http_stream_reset");

  auto on_downstream_above_write_buffer_high_watermark =
      dynamic_module->getFunctionPointer<OnHttpFilterDownstreamAboveWriteBufferHighWatermark>(
          "envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark");
  RETURN_IF_NOT_OK_REF(on_downstream_above_write_buffer_high_watermark.status());

  auto on_downstream_below_write_buffer_low_watermark =
      dynamic_module->getFunctionPointer<OnHttpFilterDownstreamBelowWriteBufferLowWatermark>(
          "envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark");
  RETURN_IF_NOT_OK_REF(on_downstream_below_write_buffer_low_watermark.status());

  auto on_local_reply = dynamic_module->getFunctionPointer<OnHttpFilterLocalReplyType>(
      "envoy_dynamic_module_on_http_filter_local_reply");

  auto config = std::make_shared<DynamicModuleHttpFilterConfig>(
      filter_name, filter_config, metrics_namespace, std::move(dynamic_module), stats_scope,
      context);

  const void* filter_config_envoy_ptr = (*constructor.value())(
      static_cast<void*>(config.get()), {filter_name.data(), filter_name.size()},
      {filter_config.data(), filter_config.size()});
  if (filter_config_envoy_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module");
  }

  config->terminal_filter_ = terminal_filter;
  config->stat_creation_frozen_ = true;

  config->in_module_config_ = filter_config_envoy_ptr;
  config->on_http_filter_config_destroy_ = on_config_destroy.value();
  config->on_http_filter_new_ = on_new_filter.value();
  config->on_http_filter_request_headers_ = on_request_headers.value();
  config->on_http_filter_request_body_ = on_request_body.value();
  config->on_http_filter_request_trailers_ = on_request_trailers.value();
  config->on_http_filter_response_headers_ = on_response_headers.value();
  config->on_http_filter_response_body_ = on_response_body.value();
  config->on_http_filter_response_trailers_ = on_response_trailers.value();
  config->on_http_filter_stream_complete_ = on_filter_stream_complete.value();
  config->on_http_filter_destroy_ = on_filter_destroy.value();
  config->on_http_filter_http_callout_done_ = on_http_callout_done.value();
  config->on_http_filter_http_stream_headers_ = on_http_stream_headers.value();
  config->on_http_filter_http_stream_data_ = on_http_stream_data.value();
  config->on_http_filter_http_stream_trailers_ = on_http_stream_trailers.value();
  config->on_http_filter_http_stream_complete_ = on_http_stream_complete.value();
  config->on_http_filter_http_stream_reset_ = on_http_stream_reset.value();
  config->on_http_filter_scheduled_ = on_scheduled.value();
  if (on_config_scheduled.ok()) {
    config->on_http_filter_config_scheduled_ = on_config_scheduled.value();
  }
  if (on_config_http_callout_done.ok()) {
    config->on_http_filter_config_http_callout_done_ = on_config_http_callout_done.value();
  }
  if (on_config_http_stream_headers.ok()) {
    config->on_http_filter_config_http_stream_headers_ = on_config_http_stream_headers.value();
  }
  if (on_config_http_stream_data.ok()) {
    config->on_http_filter_config_http_stream_data_ = on_config_http_stream_data.value();
  }
  if (on_config_http_stream_trailers.ok()) {
    config->on_http_filter_config_http_stream_trailers_ = on_config_http_stream_trailers.value();
  }
  if (on_config_http_stream_complete.ok()) {
    config->on_http_filter_config_http_stream_complete_ = on_config_http_stream_complete.value();
  }
  if (on_config_http_stream_reset.ok()) {
    config->on_http_filter_config_http_stream_reset_ = on_config_http_stream_reset.value();
  }
  config->on_http_filter_downstream_above_write_buffer_high_watermark_ =
      on_downstream_above_write_buffer_high_watermark.value();
  config->on_http_filter_downstream_below_write_buffer_low_watermark_ =
      on_downstream_below_write_buffer_low_watermark.value();
  if (on_local_reply.ok()) {
    config->on_http_filter_local_reply_ = on_local_reply.value();
  }
  return config;
}

void DynamicModuleHttpFilterConfig::onScheduled(uint64_t event_id) {
  if (on_http_filter_config_scheduled_) {
    (*on_http_filter_config_scheduled_)(this, in_module_config_, event_id);
  }
}

envoy_dynamic_module_type_http_callout_init_result DynamicModuleHttpFilterConfig::sendHttpCallout(
    uint64_t* callout_id_out, absl::string_view cluster_name, Http::RequestMessagePtr&& message,
    uint64_t timeout_milliseconds) {
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  const uint64_t callout_id = getNextCalloutId();
  auto http_callout_callback =
      std::make_unique<DynamicModuleHttpFilterConfig::HttpCalloutCallback>(*this, callout_id);
  DynamicModuleHttpFilterConfig::HttpCalloutCallback& callback = *http_callout_callback;

  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  callback.request_ = request;
  http_callouts_.emplace(callout_id, std::move(http_callout_callback));
  *callout_id_out = callout_id;
  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleHttpFilterConfig::HttpCalloutCallback::onSuccess(
    const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // Get the async client callback out of the map first to ensure it's cleaned up when this function
  // returns.
  auto it = config.http_callouts_.find(callout_id_);
  if (it == config.http_callouts_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  config.http_callouts_.erase(it);

  if (!config.in_module_config_ || !config.on_http_filter_config_http_callout_done_) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
  headers_vector.reserve(response->headers().size());
  response->headers().iterate([&headers_vector](
                                  const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  Envoy::Buffer::RawSliceVector body = response->body().getRawSlices(std::nullopt);
  config.on_http_filter_config_http_callout_done_(
      config.thisAsVoidPtr(), config.in_module_config_, callout_id,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());
}

void DynamicModuleHttpFilterConfig::HttpCalloutCallback::onFailure(
    const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // Get the async client callback out of the map first to ensure it's cleaned up when this function
  // returns.
  auto it = config.http_callouts_.find(callout_id_);
  if (it == config.http_callouts_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  config.http_callouts_.erase(it);

  if (!config.in_module_config_ || !config.on_http_filter_config_http_callout_done_) {
    return;
  }

  if (request_) {
    envoy_dynamic_module_type_http_callout_result result;
    switch (reason) {
    case Http::AsyncClient::FailureReason::Reset:
      result = envoy_dynamic_module_type_http_callout_result_Reset;
      break;
    case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
      result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
      break;
    }
    config.on_http_filter_config_http_callout_done_(config.thisAsVoidPtr(),
                                                    config.in_module_config_, callout_id, result,
                                                    nullptr, 0, nullptr, 0);
  }
}

envoy_dynamic_module_type_http_callout_init_result DynamicModuleHttpFilterConfig::startHttpStream(
    uint64_t* stream_id_out, absl::string_view cluster_name, Http::RequestMessagePtr&& message,
    bool end_stream, uint64_t timeout_milliseconds) {
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (cluster == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  if (!message->headers().Path() || !message->headers().Method() || !message->headers().Host()) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }

  const uint64_t callout_id = getNextCalloutId();
  auto callback =
      std::make_unique<DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback>(*this, callout_id);

  Http::AsyncClient::StreamOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  Http::AsyncClient::Stream* async_stream = cluster->httpAsyncClient().start(*callback, options);
  if (async_stream == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  bool has_initial_body = message->body().length() > 0;
  if (has_initial_body) {
    async_stream->sendHeaders(message->headers(), false /* end_stream */);
    if (callback->cleaned_up_) {
      return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
    }
    async_stream->sendData(message->body(), end_stream);
    if (callback->cleaned_up_) {
      return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
    }
  } else {
    async_stream->sendHeaders(message->headers(), end_stream);
    if (callback->cleaned_up_) {
      return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
    }
  }

  // If no any initial failure happened, we can add the callback to the map and return success.
  // The callback will be responsible for cleaning up the stream when it's done.
  callback->stream_ = async_stream;
  callback->request_message_ = std::move(message);
  http_stream_callouts_.emplace(callout_id, std::move(callback));
  *stream_id_out = callout_id;

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleHttpFilterConfig::resetHttpStream(uint64_t stream_id) {
  auto it = http_stream_callouts_.find(stream_id);
  if (it == http_stream_callouts_.end() || !it->second->stream_) {
    return;
  }
  it->second->stream_->reset();
}

bool DynamicModuleHttpFilterConfig::sendStreamData(uint64_t stream_id, Buffer::Instance& data,
                                                   bool end_stream) {
  auto it = http_stream_callouts_.find(stream_id);
  if (it == http_stream_callouts_.end() || !it->second->stream_) {
    return false;
  }
  it->second->stream_->sendData(data, end_stream);
  return true;
}

bool DynamicModuleHttpFilterConfig::sendStreamTrailers(uint64_t stream_id,
                                                       Http::RequestTrailerMapPtr trailers) {
  auto it = http_stream_callouts_.find(stream_id);
  if (it == http_stream_callouts_.end() || !it->second->stream_) {
    return false;
  }
  it->second->request_trailers_ = std::move(trailers);
  it->second->stream_->sendTrailers(*it->second->request_trailers_);
  return true;
}

void DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback::onHeaders(
    Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // If stream_ is nullptr, that means the stream haven't completed the initialization or it have
  // been reset. In either way, ignore the response.
  if (config.in_module_config_ == nullptr ||
      config.on_http_filter_config_http_stream_headers_ == nullptr || stream_ == nullptr) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
  headers_vector.reserve(headers->size());
  headers->iterate([&headers_vector](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  config.on_http_filter_config_http_stream_headers_(
      config.thisAsVoidPtr(), config.in_module_config_, callout_id, headers_vector.data(),
      headers_vector.size(), end_stream);
}

void DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback::onData(Buffer::Instance& data,
                                                                      bool end_stream) {
  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // If stream_ is nullptr, that means the stream haven't completed the initialization or it have
  // been reset. In either way, ignore the response.
  if (config.in_module_config_ == nullptr ||
      config.on_http_filter_config_http_stream_data_ == nullptr || stream_ == nullptr) {
    return;
  }

  const uint64_t length = data.length();
  if (length > 0 || end_stream) {
    std::vector<envoy_dynamic_module_type_envoy_buffer> buffers;
    const auto& slices = data.getRawSlices();
    buffers.reserve(slices.size());
    for (const auto& slice : slices) {
      buffers.push_back({static_cast<char*>(slice.mem_), slice.len_});
    }
    config.on_http_filter_config_http_stream_data_(config.thisAsVoidPtr(), config.in_module_config_,
                                                   callout_id, buffers.data(), buffers.size(),
                                                   end_stream);
  }
}

void DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback::onTrailers(
    Http::ResponseTrailerMapPtr&& trailers) {
  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // If stream_ is nullptr, that means the stream haven't completed the initialization or it have
  // been reset. In either way, ignore the response.
  if (config.in_module_config_ == nullptr ||
      config.on_http_filter_config_http_stream_trailers_ == nullptr || stream_ == nullptr) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> trailers_vector;
  trailers_vector.reserve(trailers->size());
  trailers->iterate([&trailers_vector](
                        const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    trailers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  config.on_http_filter_config_http_stream_trailers_(
      config.thisAsVoidPtr(), config.in_module_config_, callout_id, trailers_vector.data(),
      trailers_vector.size());
}

void DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback::onComplete() {
  if (cleaned_up_) {
    return;
  }
  cleaned_up_ = true;

  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // Get the async client callback out of the map first to ensure it's cleaned up when this function
  // returns.
  auto it = config.http_stream_callouts_.find(callout_id);
  if (it == config.http_stream_callouts_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  config.http_stream_callouts_.erase(it);

  // Any in map callback must have a non-null stream_.
  ASSERT(stream_ != nullptr);
  stream_ = nullptr;

  if (config.in_module_config_ == nullptr ||
      config.on_http_filter_config_http_stream_complete_ == nullptr) {
    return;
  }

  config.on_http_filter_config_http_stream_complete_(config.thisAsVoidPtr(),
                                                     config.in_module_config_, callout_id);
}

void DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback::onReset() {
  if (cleaned_up_) {
    return;
  }
  cleaned_up_ = true;

  DynamicModuleHttpFilterConfig& config = config_;
  const uint64_t callout_id = callout_id_;

  // Get the async client callback out of the map first to ensure it's cleaned up when this function
  // returns.
  auto it = config.http_stream_callouts_.find(callout_id);
  if (it == config.http_stream_callouts_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  config.http_stream_callouts_.erase(it);

  // Any in map callback must have a non-null stream_.
  ASSERT(stream_ != nullptr);
  stream_ = nullptr;

  if (config.in_module_config_ == nullptr ||
      config.on_http_filter_config_http_stream_reset_ == nullptr) {
    return;
  }

  config.on_http_filter_config_http_stream_reset_(
      config.thisAsVoidPtr(), config.in_module_config_, callout_id,
      envoy_dynamic_module_type_http_stream_reset_reason_LocalReset);
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
