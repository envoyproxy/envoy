#include "source/extensions/filters/listener/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

namespace {

Network::FilterStatus
toEnvoyFilterStatus(envoy_dynamic_module_type_on_listener_filter_status status) {
  switch (status) {
  case envoy_dynamic_module_type_on_listener_filter_status_Continue:
    return Network::FilterStatus::Continue;
  case envoy_dynamic_module_type_on_listener_filter_status_StopIteration:
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

} // namespace

DynamicModuleListenerFilter::DynamicModuleListenerFilter(
    DynamicModuleListenerFilterConfigSharedPtr config)
    : config_(config) {}

DynamicModuleListenerFilter::~DynamicModuleListenerFilter() { destroy(); }

void DynamicModuleListenerFilter::initializeInModuleFilter() {
  in_module_filter_ = config_->on_listener_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleListenerFilter::destroy() {
  // Cancel all pending HTTP callouts before destroying the filter.
  for (auto& callout : http_callouts_) {
    if (callout.second->request_ != nullptr) {
      callout.second->request_->cancel();
    }
  }
  http_callouts_.clear();

  if (in_module_filter_ != nullptr) {
    config_->on_listener_filter_destroy_(in_module_filter_);
    in_module_filter_ = nullptr;
  }
  destroyed_ = true;
}

Network::FilterStatus DynamicModuleListenerFilter::onAccept(Network::ListenerFilterCallbacks& cb) {
  callbacks_ = &cb;

  const std::string& worker_name = cb.dispatcher().name();
  auto pos = worker_name.find_first_of('_');
  ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{index}");
  if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index_)) {
    IS_ENVOY_BUG("failed to parse worker index from name");
  }
  // Delay the in-module filter initialization until callbacks are set
  // to allow accessing worker thread index during filter creation.
  initializeInModuleFilter();

  if (in_module_filter_ == nullptr) {
    // Module failed to create filter, close the connection.
    cb.socket().ioHandle().close();
    return Network::FilterStatus::StopIteration;
  }

  auto status = config_->on_listener_filter_on_accept_(thisAsVoidPtr(), in_module_filter_);
  return toEnvoyFilterStatus(status);
}

Network::FilterStatus DynamicModuleListenerFilter::onData(Network::ListenerFilterBuffer& buffer) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }

  // Set the current buffer for ABI callbacks.
  current_buffer_ = &buffer;
  auto raw_slice = buffer.rawSlice();
  auto status =
      config_->on_listener_filter_on_data_(thisAsVoidPtr(), in_module_filter_, raw_slice.len_);
  current_buffer_ = nullptr;

  return toEnvoyFilterStatus(status);
}

void DynamicModuleListenerFilter::onClose() {
  if (in_module_filter_ == nullptr) {
    return;
  }
  config_->on_listener_filter_on_close_(thisAsVoidPtr(), in_module_filter_);
}

size_t DynamicModuleListenerFilter::maxReadBytes() const {
  if (in_module_filter_ == nullptr) {
    return 0;
  }
  return config_->on_listener_filter_get_max_read_bytes_(
      const_cast<DynamicModuleListenerFilter*>(this)->thisAsVoidPtr(), in_module_filter_);
}

void DynamicModuleListenerFilter::onScheduled(uint64_t event_id) {
  // By the time this event is invoked, the filter might be destroyed.
  if (in_module_filter_ && config_->on_listener_filter_scheduled_) {
    config_->on_listener_filter_scheduled_(thisAsVoidPtr(), in_module_filter_, event_id);
  }
}

envoy_dynamic_module_type_http_callout_init_result DynamicModuleListenerFilter::sendHttpCallout(
    uint64_t* callout_id_out, absl::string_view cluster_name, Http::RequestMessagePtr&& message,
    uint64_t timeout_milliseconds) {
  Upstream::ThreadLocalCluster* cluster =
      config_->cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  // Prepare the callback and the ID.
  const uint64_t callout_id = getNextCalloutId();
  auto http_callout_callback = std::make_unique<DynamicModuleListenerFilter::HttpCalloutCallback>(
      shared_from_this(), callout_id);
  DynamicModuleListenerFilter::HttpCalloutCallback& callback = *http_callout_callback;

  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  // Register the callout.
  callback.request_ = request;
  http_callouts_.emplace(callout_id, std::move(http_callout_callback));
  *callout_id_out = callout_id;

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleListenerFilter::HttpCalloutCallback::onSuccess(
    const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
  // Copy the filter shared_ptr and callout id to the local scope since
  // on_listener_filter_http_callout_done_ might cause destruction of the filter. That eventually
  // ends up deallocating this callback itself.
  DynamicModuleListenerFilterSharedPtr filter = filter_.lock();
  uint64_t callout_id = callout_id_;
  // Check if the filter is destroyed before the callout completed.
  if (!filter || !filter->in_module_filter_) {
    return;
  }

  if (filter->config_->on_listener_filter_http_callout_done_) {
    absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
    headers_vector.reserve(response->headers().size());
    response->headers().iterate(
        [&headers_vector](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
          headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
              .key_ptr = const_cast<char*>(header.key().getStringView().data()),
              .key_length = header.key().getStringView().size(),
              .value_ptr = const_cast<char*>(header.value().getStringView().data()),
              .value_length = header.value().getStringView().size()});
          return Http::HeaderMap::Iterate::Continue;
        });

    absl::InlinedVector<envoy_dynamic_module_type_envoy_buffer, 16> body_chunks_vector;
    const Buffer::Instance& body = response->body();
    for (const Buffer::RawSlice& slice : body.getRawSlices()) {
      body_chunks_vector.emplace_back(
          envoy_dynamic_module_type_envoy_buffer{static_cast<const char*>(slice.mem_), slice.len_});
    }

    filter->config_->on_listener_filter_http_callout_done_(
        filter->thisAsVoidPtr(), filter->in_module_filter_, callout_id,
        envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
        headers_vector.size(), body_chunks_vector.data(), body_chunks_vector.size());
  }

  // Remove the callout from the map.
  filter->http_callouts_.erase(callout_id);
}

void DynamicModuleListenerFilter::HttpCalloutCallback::onFailure(
    const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Copy the filter shared_ptr and callout id to the local scope since
  // on_listener_filter_http_callout_done_ might cause destruction of the filter. That eventually
  // ends up deallocating this callback itself.
  DynamicModuleListenerFilterSharedPtr filter = filter_.lock();
  uint64_t callout_id = callout_id_;
  if (!filter || !filter->in_module_filter_) {
    return;
  }

  if (filter->config_->on_listener_filter_http_callout_done_) {
    envoy_dynamic_module_type_http_callout_result result =
        envoy_dynamic_module_type_http_callout_result_Reset;
    switch (reason) {
    case Http::AsyncClient::FailureReason::Reset:
      result = envoy_dynamic_module_type_http_callout_result_Reset;
      break;
    case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
      result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
      break;
    }

    filter->config_->on_listener_filter_http_callout_done_(filter->thisAsVoidPtr(),
                                                           filter->in_module_filter_, callout_id,
                                                           result, nullptr, 0, nullptr, 0);
  }

  // Remove the callout from the map.
  filter->http_callouts_.erase(callout_id);
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
