#include "source/extensions/filters/network/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

namespace {

Network::FilterStatus
toEnvoyFilterStatus(envoy_dynamic_module_type_on_network_filter_data_status status) {
  switch (status) {
  case envoy_dynamic_module_type_on_network_filter_data_status_Continue:
    return Network::FilterStatus::Continue;
  case envoy_dynamic_module_type_on_network_filter_data_status_StopIteration:
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

envoy_dynamic_module_type_network_connection_event
toAbiConnectionEvent(Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    return envoy_dynamic_module_type_network_connection_event_RemoteClose;
  case Network::ConnectionEvent::LocalClose:
    return envoy_dynamic_module_type_network_connection_event_LocalClose;
  case Network::ConnectionEvent::Connected:
    return envoy_dynamic_module_type_network_connection_event_Connected;
  case Network::ConnectionEvent::ConnectedZeroRtt:
    return envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt;
  }
  return envoy_dynamic_module_type_network_connection_event_LocalClose;
}

} // namespace

DynamicModuleNetworkFilter::DynamicModuleNetworkFilter(
    DynamicModuleNetworkFilterConfigSharedPtr config)
    : config_(config) {}

DynamicModuleNetworkFilter::~DynamicModuleNetworkFilter() { destroy(); }

void DynamicModuleNetworkFilter::initializeInModuleFilter() {
  in_module_filter_ = config_->on_network_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleNetworkFilter::destroy() {
  // Cancel all pending HTTP callouts before destroying the filter.
  for (auto& callout : http_callouts_) {
    if (callout.second->request_ != nullptr) {
      callout.second->request_->cancel();
    }
  }
  http_callouts_.clear();

  if (in_module_filter_ != nullptr) {
    config_->on_network_filter_destroy_(in_module_filter_);
    in_module_filter_ = nullptr;
  }
  destroyed_ = true;
}

void DynamicModuleNetworkFilter::initializeReadFilterCallbacks(
    Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  // Register for connection events.
  read_callbacks_->connection().addConnectionCallbacks(*this);
}

void DynamicModuleNetworkFilter::initializeWriteFilterCallbacks(
    Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

Network::FilterStatus DynamicModuleNetworkFilter::onNewConnection() {
  if (in_module_filter_ == nullptr) {
    if (read_callbacks_ != nullptr) {
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    return Network::FilterStatus::StopIteration;
  }
  auto status = config_->on_network_filter_new_connection_(thisAsVoidPtr(), in_module_filter_);
  return toEnvoyFilterStatus(status);
}

Network::FilterStatus DynamicModuleNetworkFilter::onData(Buffer::Instance& data, bool end_stream) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }
  // Set the current read buffer for ABI callbacks.
  current_read_buffer_ = &data;
  auto status = config_->on_network_filter_read_(thisAsVoidPtr(), in_module_filter_, data.length(),
                                                 end_stream);
  current_read_buffer_ = nullptr;
  return toEnvoyFilterStatus(status);
}

Network::FilterStatus DynamicModuleNetworkFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }
  // Set the current write buffer for ABI callbacks.
  current_write_buffer_ = &data;
  auto status = config_->on_network_filter_write_(thisAsVoidPtr(), in_module_filter_, data.length(),
                                                  end_stream);
  current_write_buffer_ = nullptr;
  return toEnvoyFilterStatus(status);
}

void DynamicModuleNetworkFilter::onEvent(Network::ConnectionEvent event) {
  if (in_module_filter_ == nullptr) {
    return;
  }
  config_->on_network_filter_event_(thisAsVoidPtr(), in_module_filter_,
                                    toAbiConnectionEvent(event));
}

void DynamicModuleNetworkFilter::onAboveWriteBufferHighWatermark() {
  // Not currently exposed to dynamic modules.
}

void DynamicModuleNetworkFilter::onBelowWriteBufferLowWatermark() {
  // Not currently exposed to dynamic modules.
}

void DynamicModuleNetworkFilter::continueReading() {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->continueReading();
  }
}

void DynamicModuleNetworkFilter::close(Network::ConnectionCloseType close_type) {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->connection().close(close_type);
  }
}

void DynamicModuleNetworkFilter::write(Buffer::Instance& data, bool end_stream) {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->connection().write(data, end_stream);
  }
}

void DynamicModuleNetworkFilter::storeSocketOptionInt(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    int64_t value) {
  socket_options_.push_back(
      StoredSocketOption{level, name, state, /*is_int=*/true, value, std::string()});
}

void DynamicModuleNetworkFilter::storeSocketOptionBytes(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    absl::string_view value) {
  socket_options_.push_back(StoredSocketOption{level, name, state, /*is_int=*/false,
                                               /*int_value=*/0,
                                               std::string(value.data(), value.size())});
}

bool DynamicModuleNetworkFilter::tryGetSocketOptionInt(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    int64_t& value_out) const {
  for (const auto& opt : socket_options_) {
    if (opt.is_int && opt.level == level && opt.name == name && opt.state == state) {
      value_out = opt.int_value;
      return true;
    }
  }
  return false;
}

bool DynamicModuleNetworkFilter::tryGetSocketOptionBytes(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    absl::string_view& value_out) const {
  for (const auto& opt : socket_options_) {
    if (!opt.is_int && opt.level == level && opt.name == name && opt.state == state) {
      value_out = opt.byte_value;
      return true;
    }
  }
  return false;
}

void DynamicModuleNetworkFilter::copySocketOptions(
    envoy_dynamic_module_type_socket_option* options_out, size_t options_size,
    size_t& options_written) const {
  options_written = 0;
  for (const auto& opt : socket_options_) {
    if (options_written >= options_size) {
      break;
    }
    auto& out = options_out[options_written];
    out.level = opt.level;
    out.name = opt.name;
    out.state = opt.state;
    if (opt.is_int) {
      out.value_type = envoy_dynamic_module_type_socket_option_value_type_Int;
      out.int_value = opt.int_value;
      out.byte_value.ptr = nullptr;
      out.byte_value.length = 0;
    } else {
      out.value_type = envoy_dynamic_module_type_socket_option_value_type_Bytes;
      out.int_value = 0;
      out.byte_value.ptr = opt.byte_value.data();
      out.byte_value.length = opt.byte_value.size();
    }
    ++options_written;
  }
}

envoy_dynamic_module_type_http_callout_init_result DynamicModuleNetworkFilter::sendHttpCallout(
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
  auto http_callout_callback = std::make_unique<DynamicModuleNetworkFilter::HttpCalloutCallback>(
      shared_from_this(), callout_id);
  DynamicModuleNetworkFilter::HttpCalloutCallback& callback = *http_callout_callback;

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

void DynamicModuleNetworkFilter::HttpCalloutCallback::onSuccess(
    const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
  // Copy the filter shared_ptr and callout id to the local scope since
  // on_network_filter_http_callout_done_ might cause destruction of the filter. That eventually
  // ends up deallocating this callback itself.
  DynamicModuleNetworkFilterSharedPtr filter = filter_.lock();
  uint64_t callout_id = callout_id_;
  // Check if the filter is destroyed before the callout completed.
  if (!filter || !filter->in_module_filter_ ||
      !filter->config_->on_network_filter_http_callout_done_) {
    return;
  }

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

  filter->config_->on_network_filter_http_callout_done_(
      filter->thisAsVoidPtr(), filter->in_module_filter_, callout_id,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), body_chunks_vector.data(), body_chunks_vector.size());

  // Remove the callout from the map.
  filter->http_callouts_.erase(callout_id);
}

void DynamicModuleNetworkFilter::HttpCalloutCallback::onFailure(
    const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Copy the filter shared_ptr and callout id to the local scope since
  // on_network_filter_http_callout_done_ might cause destruction of the filter. That eventually
  // ends up deallocating this callback itself.
  DynamicModuleNetworkFilterSharedPtr filter = filter_.lock();
  uint64_t callout_id = callout_id_;
  if (!filter || !filter->in_module_filter_ ||
      !filter->config_->on_network_filter_http_callout_done_) {
    return;
  }

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

  filter->config_->on_network_filter_http_callout_done_(filter->thisAsVoidPtr(),
                                                        filter->in_module_filter_, callout_id,
                                                        result, nullptr, 0, nullptr, 0);

  // Remove the callout from the map.
  filter->http_callouts_.erase(callout_id);
}

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
