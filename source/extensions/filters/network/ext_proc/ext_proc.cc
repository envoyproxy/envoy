#include "source/extensions/filters/network/ext_proc/ext_proc.h"

#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

MessageTimeoutManager::MessageTimeoutManager(NetworkExtProcFilter& filter,
                                             Event::Dispatcher& dispatcher)
    : filter_(filter), read_timer_(dispatcher.createTimer([this]() -> void { onTimeout(true); })),
      write_timer_(dispatcher.createTimer([this]() -> void { onTimeout(false); })) {}

void MessageTimeoutManager::startTimer(bool is_read) {
  const auto timeout = filter_.getMessageTimeout();
  if (timeout.count() == 0) {
    // Zero timeout means no timeout
    return;
  }

  if (is_read) {
    ENVOY_LOG(debug, "Starting read message timer with timeout {} ms", timeout.count());
    read_timer_->enableTimer(timeout);
    read_timer_active_ = true;
  } else {
    ENVOY_LOG(debug, "Starting write message timer with timeout {} ms", timeout.count());
    write_timer_->enableTimer(timeout);
    write_timer_active_ = true;
  }
}

void MessageTimeoutManager::stopTimer(bool is_read) {
  if (is_read && read_timer_active_) {
    ENVOY_LOG(debug, "Stopping read message timer");
    read_timer_->disableTimer();
    read_timer_active_ = false;
  } else if (!is_read && write_timer_active_) {
    ENVOY_LOG(debug, "Stopping write message timer");
    write_timer_->disableTimer();
    write_timer_active_ = false;
  }
}

void MessageTimeoutManager::stopAllTimers() {
  stopTimer(true);  // Stop read timer
  stopTimer(false); // Stop write timer
}

void MessageTimeoutManager::onTimeout(bool is_read) {
  ENVOY_LOG(warn, "{} message timeout occurred", is_read ? "Read" : "Write");
  filter_.handleMessageTimeout(is_read);
}

void NetworkExtProcLoggingInfo::recordGrpcCall(std::chrono::microseconds latency,
                                               Grpc::Status::GrpcStatus call_status,
                                               bool is_read_direction) {
  DirectionalStats& stats = is_read_direction ? read_stats_ : write_stats_;

  // Update counters.
  stats.grpc_calls_++;
  if (call_status != Grpc::Status::WellKnownGrpcStatus::Ok) {
    stats.grpc_errors_++;
  }

  stats.total_latency_ += latency;
  stats.max_latency_ = std::max(stats.max_latency_, latency);
  stats.min_latency_ = std::min(stats.min_latency_, latency);
  last_call_status_ = call_status;
}

void NetworkExtProcLoggingInfo::addBytesProcessed(uint64_t bytes, bool is_read_direction) {
  DirectionalStats& stats = is_read_direction ? read_stats_ : write_stats_;
  stats.bytes_processed_ += bytes;
  stats.message_count_++;
}

void NetworkExtProcLoggingInfo::setConnectionInfo(const Network::Connection* connection) {
  if (connection == nullptr) {
    return;
  }

  const auto& remote_address = connection->connectionInfoProvider().remoteAddress();
  if (remote_address != nullptr) {
    peer_address_ = remote_address->asString();
  }

  const auto& local_address = connection->connectionInfoProvider().localAddress();
  if (local_address != nullptr) {
    local_address_ = local_address->asString();
  }
}

NetworkExtProcFilter::NetworkExtProcFilter(ConfigConstSharedPtr config,
                                           ExternalProcessorClientPtr&& client)
    : config_(config), stats_(config->stats()), client_(std::move(client)),
      config_with_hash_key_(config_->grpcService()), downstream_callbacks_(*this) {}

NetworkExtProcFilter::~NetworkExtProcFilter() { closeStream(); }

void NetworkExtProcFilter::initializeLoggingInfo() {
  if (read_callbacks_ == nullptr) {
    return;
  }

  const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
      read_callbacks_->connection().streamInfo().filterState();

  if (!filter_state->hasData<NetworkExtProcLoggingInfo>(
          NetworkFilterNames::get().NetworkExternalProcessor)) {
    filter_state->setData(NetworkFilterNames::get().NetworkExternalProcessor,
                          std::make_shared<NetworkExtProcLoggingInfo>(),
                          Envoy::StreamInfo::FilterState::StateType::Mutable,
                          Envoy::StreamInfo::FilterState::LifeSpan::Connection);
  }

  logging_info_ = filter_state->getDataMutable<NetworkExtProcLoggingInfo>(
      NetworkFilterNames::get().NetworkExternalProcessor);
  if (logging_info_ != nullptr) {
    logging_info_->setConnectionInfo(&read_callbacks_->connection());
  }
}

void NetworkExtProcFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);

  if (!timeout_manager_) {
    timeout_manager_ =
        std::make_unique<MessageTimeoutManager>(*this, read_callbacks_->connection().dispatcher());
  }

  initializeLoggingInfo();
}

void NetworkExtProcFilter::initializeWriteFilterCallbacks(
    Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

Network::FilterStatus NetworkExtProcFilter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "ext_proc: new connection", read_callbacks_->connection());
  return Network::FilterStatus::Continue;
}

Network::FilterStatus NetworkExtProcFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "ext_proc: received {} bytes of data, end stream={}",
                 read_callbacks_->connection(), data.length(), end_stream);

  if (config_->processingMode().process_read() ==
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP) {
    return Network::FilterStatus::Continue;
  }

  StreamOpenState state = openStream();
  if (state != StreamOpenState::Ok) {
    return (state == StreamOpenState::Error) ? handleStreamError()
                                             : Network::FilterStatus::Continue;
  }
  sendRequest(data, end_stream, /*is_read=*/true);
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus NetworkExtProcFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "ext_proc: writing {} bytes of data, end stream={}",
                 write_callbacks_->connection(), data.length(), end_stream);

  if (config_->processingMode().process_write() ==
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP) {
    return Network::FilterStatus::Continue;
  }

  StreamOpenState state = openStream();
  if (state != StreamOpenState::Ok) {
    return (state == StreamOpenState::Error) ? handleStreamError()
                                             : Network::FilterStatus::Continue;
  }

  sendRequest(data, end_stream, /*is_read=*/false);
  return Network::FilterStatus::StopIteration;
}

void NetworkExtProcFilter::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    closeStream();
  }
}

Network::FilterStatus NetworkExtProcFilter::handleStreamError() {
  ENVOY_CONN_LOG(debug, "Stream error encountered with failure_mode_allow: {}",
                 read_callbacks_->connection(), config_->failureModeAllow());

  processing_complete_ = true;
  closeStream();

  if (config_->failureModeAllow()) {
    // In failure allow mode, continue processing despite stream errors
    stats_.failure_mode_allowed_.inc();
    return Network::FilterStatus::Continue;
  } else {
    // In strict mode, close the connection and stop processing
    closeConnection("ext_proc_stream_error", Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }
}

void NetworkExtProcFilter::updateCloseCallbackStatus(bool enable, bool is_read) {
  if (is_read) {
    if (enable) {
      disable_count_read_++;
      read_callbacks_->disableClose(true);
    } else {
      disable_count_read_--;
      if (disable_count_read_ == 0) {
        read_callbacks_->disableClose(false);
      }
    }
  } else {
    if (enable) {
      disable_count_write_++;
      write_callbacks_->disableClose(true);
    } else {
      disable_count_write_--;
      if (disable_count_write_ == 0) {
        write_callbacks_->disableClose(false);
      }
    }
  }
}

NetworkExtProcFilter::StreamOpenState NetworkExtProcFilter::openStream() {
  if (processing_complete_) {
    ENVOY_CONN_LOG(debug, "Processing already completed, skipping stream creation",
                   read_callbacks_->connection());
    return StreamOpenState::IgnoreError;
  }

  if (stream_ != nullptr) {
    return StreamOpenState::Ok;
  }

  ENVOY_CONN_LOG(debug, "Creating new gRPC stream to external processor",
                 read_callbacks_->connection());

  Http::AsyncClient::ParentContext grpc_context;
  grpc_context.stream_info = &read_callbacks_->connection().streamInfo();

  auto options = Http::AsyncClient::StreamOptions()
                     .setParentContext(grpc_context)
                     .setBufferBodyForRetry(config_->grpcService().has_retry_policy());

  ExternalProcessorStreamPtr stream_object =
      client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  if (stream_object == nullptr) {
    ENVOY_CONN_LOG(error, "Failed to create gRPC stream to external processor",
                   read_callbacks_->connection());
    stats_.stream_open_failures_.inc();
    return StreamOpenState::Error;
  }

  stream_ = std::move(stream_object);
  stats_.streams_started_.inc();
  return StreamOpenState::Ok;
}

void NetworkExtProcFilter::handleMessageTimeout(bool is_read) {
  ENVOY_CONN_LOG(warn, "{} message timeout occurred", read_callbacks_->connection(),
                 is_read ? "Read" : "Write");

  stats_.message_timeouts_.inc();
  processing_complete_ = true;

  read_pending_ = false;
  write_pending_ = false;

  // Re-enable close callbacks for both directions
  if (disable_count_read_ > 0) {
    updateCloseCallbackStatus(false, true);
  }
  if (disable_count_write_ > 0) {
    updateCloseCallbackStatus(false, false);
  }

  closeStream();

  // Handle timeout based on failure mode
  if (config_->failureModeAllow()) {
    ENVOY_CONN_LOG(debug, "Message timeout with failure_mode_allow=true, continuing",
                   read_callbacks_->connection());
    stats_.failure_mode_allowed_.inc();
  } else {
    ENVOY_CONN_LOG(info, "Message timeout with failure_mode_allow=false, closing connection",
                   read_callbacks_->connection());
    closeConnection("ext_proc_message_timeout", Network::ConnectionCloseType::FlushWrite);
  }
}

const std::chrono::milliseconds& NetworkExtProcFilter::getMessageTimeout() {
  return config_->messageTimeout();
}

void NetworkExtProcFilter::sendRequest(Buffer::Instance& data, bool end_stream, bool is_read) {
  if (stream_ == nullptr) {
    ENVOY_CONN_LOG(error, "Cannot send request: stream is null", read_callbacks_->connection());
    return;
  }

  ENVOY_CONN_LOG(debug, "Sending {} bytes of {} data, end_stream={}", read_callbacks_->connection(),
                 data.length(), is_read ? "read" : "write", end_stream);

  // Prevent connection close while waiting for processor response
  updateCloseCallbackStatus(true, is_read);

  if (logging_info_ != nullptr) {
    logging_info_->addBytesProcessed(data.length(), is_read);
  }

  // Prepare the request message
  ProcessingRequest request;
  addDynamicMetadata(request);

  if (is_read) {
    auto* read_data = request.mutable_read_data();
    read_data->set_data(data.toString());
    read_data->set_end_of_stream(end_stream);
    stats_.read_data_sent_.inc();
    read_pending_ = true;
    read_call_start_time_ = read_callbacks_->connection().dispatcher().timeSource().monotonicTime();
  } else {
    auto* write_data = request.mutable_write_data();
    write_data->set_data(data.toString());
    write_data->set_end_of_stream(end_stream);
    stats_.write_data_sent_.inc();
    write_pending_ = true;
    write_call_start_time_ =
        read_callbacks_->connection().dispatcher().timeSource().monotonicTime();
  }

  // Start timeout for this specific direction
  if (timeout_manager_) {
    timeout_manager_->startTimer(is_read);
  }

  // Send to external processor
  stream_->send(std::move(request), false);
  stats_.stream_msgs_sent_.inc();

  // Clear data buffer after sending
  data.drain(data.length());
}

void NetworkExtProcFilter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& res) {
  if (processing_complete_) {
    ENVOY_CONN_LOG(debug, "Ignoring response message: processing already completed",
                   read_callbacks_->connection());
    stats_.spurious_msgs_received_.inc();
    return;
  }

  auto response = std::move(res);
  ENVOY_CONN_LOG(debug, "Received response from external processor", read_callbacks_->connection());
  stats_.stream_msgs_received_.inc();
  bool is_read = response->has_read_data();
  recordCallCompletion(Grpc::Status::WellKnownGrpcStatus::Ok, is_read);

  // Handle connection status before processing data
  handleConnectionStatus(*response);
  if (processing_complete_) {
    return;
  }

  if (response->has_read_data()) {
    const auto& data = response->read_data();
    if (timeout_manager_ && read_pending_) {
      timeout_manager_->stopTimer(true);
    }
    read_pending_ = false;

    ENVOY_CONN_LOG(trace, "Processing READ data response: {} bytes, end_stream={}",
                   read_callbacks_->connection(), data.data().size(), data.end_of_stream());

    Buffer::OwnedImpl buffer(data.data());
    read_callbacks_->injectReadDataToFilterChain(buffer, data.end_of_stream());
    updateCloseCallbackStatus(false, true);
    stats_.read_data_injected_.inc();
  } else if (response->has_write_data()) {
    if (timeout_manager_ && write_pending_) {
      timeout_manager_->stopTimer(true);
    }
    write_pending_ = false;

    const auto& data = response->write_data();
    ENVOY_CONN_LOG(trace, "Processing WRITE data response: {} bytes, end_stream={}",
                   read_callbacks_->connection(), data.data().size(), data.end_of_stream());
    Buffer::OwnedImpl buffer(data.data());
    write_callbacks_->injectWriteDataToFilterChain(buffer, data.end_of_stream());
    updateCloseCallbackStatus(false, false);
    stats_.write_data_injected_.inc();
  } else {
    ENVOY_CONN_LOG(debug, "Response contained no data, continuing", read_callbacks_->connection());
    stats_.empty_response_received_.inc();
  }
}

void NetworkExtProcFilter::onGrpcError(Grpc::Status::GrpcStatus status,
                                       const std::string& message) {
  ENVOY_CONN_LOG(error, "ext_proc: gRPC error: {}, message: {}", read_callbacks_->connection(),
                 status, message);
  // Mark processing as complete to avoid further gRPC calls
  processing_complete_ = true;
  if (read_pending_) {
    recordCallCompletion(status, true);
  } else if (write_pending_) {
    recordCallCompletion(status, false);
  }

  closeStream();
  stats_.streams_grpc_error_.inc();

  // If failure mode is not to allow, close the connection
  if (!config_->failureModeAllow()) {
    ENVOY_CONN_LOG(debug, "Closing connection since failure model allow is not enabled",
                   read_callbacks_->connection());
    closeConnection("ext_proc_grpc_error", Network::ConnectionCloseType::FlushWrite);
    return;
  }

  stats_.failure_mode_allowed_.inc();
}

void NetworkExtProcFilter::onGrpcClose() {
  ENVOY_CONN_LOG(debug, "gRPC stream closed by peer", read_callbacks_->connection());
  processing_complete_ = true;
  stats_.streams_grpc_close_.inc();
  closeStream();
}

void NetworkExtProcFilter::recordCallCompletion(Grpc::Status::GrpcStatus status,
                                                bool is_read_direction) {
  if (logging_info_ == nullptr) {
    return;
  }

  auto& call_start_time = is_read_direction ? read_call_start_time_ : write_call_start_time_;

  if (call_start_time.has_value()) {
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        read_callbacks_->connection().dispatcher().timeSource().monotonicTime() -
        call_start_time.value());

    logging_info_->recordGrpcCall(duration, status, is_read_direction);
    call_start_time = absl::nullopt;
  }
}

// Update closeStream to stop all timers
void NetworkExtProcFilter::closeStream() {
  if (timeout_manager_) {
    timeout_manager_->stopAllTimers();
    timeout_manager_.reset();
  }

  // Clear pending flags
  read_pending_ = false;
  write_pending_ = false;
  write_call_start_time_ = absl::nullopt;
  read_call_start_time_ = absl::nullopt;

  if (stream_ == nullptr) {
    return;
  }

  bool closed = stream_->close();
  if (closed) {
    stats_.streams_closed_.inc();
  }
  ENVOY_CONN_LOG(debug, "Stream closed: {}", read_callbacks_->connection(), closed);
  stream_ = nullptr;
}

void NetworkExtProcFilter::closeConnection(const std::string& reason,
                                           Network::ConnectionCloseType close_type) {
  ENVOY_CONN_LOG(
      info, "Closing connection: {}, close_type: {}", read_callbacks_->connection(), reason,
      close_type == Network::ConnectionCloseType::FlushWrite ? "FlushWrite" : "AbortReset");

  // Ensure all callbacks are enabled before closing
  read_callbacks_->disableClose(false);
  write_callbacks_->disableClose(false);
  read_callbacks_->connection().close(close_type, reason);

  // Track different types of closures in stats
  if (close_type == Network::ConnectionCloseType::AbortReset) {
    stats_.connections_reset_.inc();
  }
  stats_.connections_closed_.inc();
}

void NetworkExtProcFilter::handleConnectionStatus(const ProcessingResponse& response) {
  switch (response.connection_status()) {
  case envoy::service::network_ext_proc::v3::ProcessingResponse::CONTINUE:
    ENVOY_CONN_LOG(debug, "External processor requested to continue connection",
                   read_callbacks_->connection());
    break;

  case envoy::service::network_ext_proc::v3::ProcessingResponse::CLOSE:
    // Close the connection with normal FIN
    ENVOY_CONN_LOG(info, "External processor requested to close connection with FIN",
                   read_callbacks_->connection());
    closeConnection("ext_proc_close_requested", Network::ConnectionCloseType::FlushWrite);
    processing_complete_ = true;
    break;

  case envoy::service::network_ext_proc::v3::ProcessingResponse::CLOSE_RST:
    // Immediately reset the connection
    ENVOY_CONN_LOG(info, "External processor requested to reset connection",
                   read_callbacks_->connection());
    closeConnection("ext_proc_reset_requested", Network::ConnectionCloseType::AbortReset);
    processing_complete_ = true;
    break;

  default:
    // Unknown status, log a warning and continue
    ENVOY_CONN_LOG(warn, "Unknown connection status from external processor.",
                   read_callbacks_->connection());
    break;
  }
}

void NetworkExtProcFilter::addDynamicMetadata(ProcessingRequest& req) {
  if (config_->untypedForwardingMetadataNamespaces().empty() &&
      config_->typedForwardingMetadataNamespaces().empty()) {
    return;
  }

  envoy::config::core::v3::Metadata forwarding_metadata;

  const auto& dynamic_metadata = read_callbacks_->connection().streamInfo().dynamicMetadata();
  const auto& connection_metadata = dynamic_metadata.filter_metadata();
  const auto& connection_typed_metadata = dynamic_metadata.typed_filter_metadata();

  for (const auto& context_key : config_->untypedForwardingMetadataNamespaces()) {
    if (const auto metadata_it = connection_metadata.find(context_key);
        metadata_it != connection_metadata.end()) {
      (*forwarding_metadata.mutable_filter_metadata())[metadata_it->first] = metadata_it->second;
    }
  }

  for (const auto& context_key : config_->typedForwardingMetadataNamespaces()) {
    if (const auto metadata_it = connection_typed_metadata.find(context_key);
        metadata_it != connection_typed_metadata.end()) {
      (*forwarding_metadata.mutable_typed_filter_metadata())[metadata_it->first] =
          metadata_it->second;
    }
  }

  if (!forwarding_metadata.filter_metadata().empty() ||
      !forwarding_metadata.typed_filter_metadata().empty()) {
    *req.mutable_metadata() = std::move(forwarding_metadata);
  }
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
