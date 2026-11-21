#include "source/extensions/filters/network/ext_proc/ext_proc.h"

#include "source/common/network/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

MessageTimeoutManager::MessageTimeoutManager(NetworkExtProcFilter& filter,
                                             Event::Dispatcher& dispatcher)
    : filter_(filter), dispatcher_(dispatcher),
      read_timer_(dispatcher.createTimer([this]() -> void { onTimeout(true); })),
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

void MessageTimeoutManager::startConnectionTimer() {
  const auto timeout = filter_.getMessageTimeout();
  if (timeout.count() == 0) {
    return;
  }
  if (connection_timer_ == nullptr) {
    connection_timer_ = dispatcher_.createTimer([this]() -> void { onConnectionTimeout(); });
  }
  ENVOY_LOG(debug, "Starting connection message timer with timeout {} ms", timeout.count());
  connection_timer_->enableTimer(timeout);
  connection_timer_active_ = true;
}

void MessageTimeoutManager::stopConnectionTimer() {
  if (connection_timer_active_ && connection_timer_ != nullptr) {
    ENVOY_LOG(debug, "Stopping connection message timer");
    connection_timer_->disableTimer();
    connection_timer_active_ = false;
  }
}

void MessageTimeoutManager::stopAllTimers() {
  stopTimer(true);       // Stop read timer
  stopTimer(false);      // Stop write timer
  stopConnectionTimer(); // Stop connection timer
}

void MessageTimeoutManager::onTimeout(bool is_read) {
  ENVOY_LOG(warn, "{} message timeout occurred", is_read ? "Read" : "Write");
  filter_.handleMessageTimeout(is_read);
}

void MessageTimeoutManager::onConnectionTimeout() {
  ENVOY_LOG(warn, "NewConnection message timeout occurred");
  filter_.handleConnectionMessageTimeout();
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

Config::Config(
    const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& config,
    Stats::Scope& scope, Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    const LocalInfo::LocalInfo* local_info, absl::Status& creation_status)
    : failure_mode_allow_(config.failure_mode_allow()), processing_mode_(config.processing_mode()),
      grpc_service_(config.grpc_service()),
      untyped_forwarding_namespaces_(
          config.metadata_options().forwarding_namespaces().untyped().begin(),
          config.metadata_options().forwarding_namespaces().untyped().end()),
      typed_forwarding_namespaces_(
          config.metadata_options().forwarding_namespaces().typed().begin(),
          config.metadata_options().forwarding_namespaces().typed().end()),
      untyped_receiving_namespaces_(
          config.metadata_options().receiving_namespaces().untyped().begin(),
          config.metadata_options().receiving_namespaces().untyped().end()),
      send_once_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.metadata_options(), send_once, false)),
      stats_(generateStats(config.stat_prefix(), scope)),
      message_timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, message_timeout, DefaultMessageTimeoutMs))),
      expression_manager_(builder, local_info, config.connection_attributes(), creation_status) {}

Config::Config(
    const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& config,
    Stats::Scope& scope)
    : Config(config, scope, nullptr, nullptr, []() -> absl::Status& {
        static absl::Status* status = new absl::Status(absl::OkStatus());
        return *status;
      }()) {}

NetworkExtProcFilter::NetworkExtProcFilter(ConfigConstSharedPtr config,
                                           ExternalProcessorClientPtr&& client)
    : config_(config), stats_(config->stats()), client_(std::move(client)),
      config_with_hash_key_(config_->grpcService()), downstream_callbacks_(*this) {}

NetworkExtProcFilter::~NetworkExtProcFilter() {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->connection().removeConnectionCallbacks(downstream_callbacks_);
  }
  read_disabled_for_new_connection_ = false;
  closeStream();
}

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

  if (config_->processingMode().process_new_connection() ==
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::NONE) {
    return Network::FilterStatus::Continue;
  }

  StreamOpenState state = openStream();
  if (state != StreamOpenState::Ok) {
    return (state == StreamOpenState::Error) ? handleStreamError()
                                             : Network::FilterStatus::Continue;
  }

  in_new_connection_ = true;
  sendNewConnectionRequest();
  in_new_connection_ = false;

  if (!new_connection_pending_) {
    if (connection_closed_ ||
        read_callbacks_->connection().state() != Network::Connection::State::Open) {
      return Network::FilterStatus::StopIteration;
    }
    return Network::FilterStatus::Continue;
  }

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus NetworkExtProcFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "ext_proc: received {} bytes of data, end stream={}",
                 read_callbacks_->connection(), data.length(), end_stream);

  if (new_connection_pending_) {
    ENVOY_CONN_LOG(debug,
                   "ext_proc: new connection processing in progress, buffering {} bytes of read "
                   "data, end stream={}",
                   read_callbacks_->connection(), data.length(), end_stream);
    pending_read_data_.move(data);
    pending_read_end_stream_ |= end_stream;
    has_pending_read_data_ = true;
    return Network::FilterStatus::StopIteration;
  }

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

  if (new_connection_pending_) {
    ENVOY_CONN_LOG(debug,
                   "ext_proc: new connection processing in progress, buffering {} bytes of write "
                   "data, end stream={}",
                   write_callbacks_->connection(), data.length(), end_stream);
    pending_write_data_.move(data);
    pending_write_end_stream_ |= end_stream;
    has_pending_write_data_ = true;
    return Network::FilterStatus::StopIteration;
  }

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
    connection_closed_ = true;
    read_disabled_for_new_connection_ = false;
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
    } else if (disable_count_read_ > 0) {
      disable_count_read_--;
      if (disable_count_read_ == 0) {
        read_callbacks_->disableClose(false);
      }
    }
  } else {
    if (enable) {
      disable_count_write_++;
      write_callbacks_->disableClose(true);
    } else if (disable_count_write_ > 0) {
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
    ENVOY_CONN_LOG(debug, "Failed to create gRPC stream to external processor",
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

void NetworkExtProcFilter::handleConnectionMessageTimeout() {
  ENVOY_CONN_LOG(warn, "NewConnection message timeout occurred", read_callbacks_->connection());

  stats_.message_timeouts_.inc();
  processing_complete_ = true;
  new_connection_pending_ = false;

  if (config_->failureModeAllow()) {
    ENVOY_CONN_LOG(debug, "NewConnection message timeout with failure_mode_allow=true, continuing",
                   read_callbacks_->connection());
    stats_.failure_mode_allowed_.inc();
    resumeAfterNewConnection(/*close_stream=*/true);
  } else {
    ENVOY_CONN_LOG(
        info, "NewConnection message timeout with failure_mode_allow=false, closing connection",
        read_callbacks_->connection());
    closeConnection("ext_proc_new_connection_message_timeout",
                    Network::ConnectionCloseType::FlushWrite);
  }
}

const std::chrono::milliseconds& NetworkExtProcFilter::getMessageTimeout() {
  return config_->messageTimeout();
}

void NetworkExtProcFilter::sendNewConnectionRequest() {
  if (stream_ == nullptr) {
    ENVOY_CONN_LOG(debug, "Cannot send new connection request: stream is null",
                   read_callbacks_->connection());
    return;
  }

  ENVOY_CONN_LOG(debug, "Sending new connection request", read_callbacks_->connection());

  // Prevent connection close and pause socket reads while waiting for processor response.
  updateCloseCallbackStatus(true, /*is_read=*/true);
  if (read_callbacks_->connection().state() == Network::Connection::State::Open) {
    read_callbacks_->connection().readDisable(true);
    read_disabled_for_new_connection_ = true;
  }

  // Prepare request.
  ProcessingRequest request;
  addDynamicMetadata(request);
  addAttributes(request);

  auto* new_conn = request.mutable_new_connection();
  const auto& conn = read_callbacks_->connection();
  const auto& info = conn.connectionInfoProvider();

  if (info.remoteAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(*info.remoteAddress(),
                                               *new_conn->mutable_downstream_remote_address());
  }
  if (info.localAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(*info.localAddress(),
                                               *new_conn->mutable_downstream_local_address());
  }
  if (!conn.requestedServerName().empty()) {
    new_conn->set_requested_server_name(std::string(conn.requestedServerName()));
  }
  if (!conn.nextProtocol().empty()) {
    new_conn->set_negotiated_protocol(conn.nextProtocol());
  }

  new_connection_pending_ = true;
  new_connection_call_start_time_ =
      read_callbacks_->connection().dispatcher().timeSource().monotonicTime();
  stats_.new_connection_sent_.inc();

  if (timeout_manager_) {
    timeout_manager_->startConnectionTimer();
  }

  stats_.stream_msgs_sent_.inc();
  stream_->send(std::move(request), false);
}

void NetworkExtProcFilter::processDynamicMetadata(const ProcessingResponse& response) {
  if (!response.has_dynamic_metadata()) {
    return;
  }
  const auto& response_metadata = response.dynamic_metadata();
  for (const auto& [key, value] : response_metadata.fields()) {
    if (config_->untypedReceivingMetadataNamespaces().contains(key)) {
      if (value.has_struct_value()) {
        read_callbacks_->connection().streamInfo().setDynamicMetadata(key, value.struct_value());
      }
    }
  }
}

void NetworkExtProcFilter::enableReadIfDisabled() {
  if (read_disabled_for_new_connection_) {
    read_disabled_for_new_connection_ = false;
    if (!connection_closed_ && read_callbacks_ != nullptr &&
        read_callbacks_->connection().state() == Network::Connection::State::Open) {
      read_callbacks_->connection().readDisable(false);
    }
  }
}

void NetworkExtProcFilter::clearPendingData() {
  pending_read_data_.drain(pending_read_data_.length());
  has_pending_read_data_ = false;
  pending_read_end_stream_ = false;
  pending_write_data_.drain(pending_write_data_.length());
  has_pending_write_data_ = false;
  pending_write_end_stream_ = false;
}

void NetworkExtProcFilter::flushPendingDataAfterNewConnection() {
  if (has_pending_read_data_) {
    Buffer::OwnedImpl buffered_read;
    buffered_read.move(pending_read_data_);
    const bool end_stream = pending_read_end_stream_;
    has_pending_read_data_ = false;
    pending_read_end_stream_ = false;

    if (!processing_complete_ &&
        config_->processingMode().process_read() !=
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP) {
      sendRequest(buffered_read, end_stream, /*is_read=*/true);
    } else {
      read_callbacks_->injectReadDataToFilterChain(buffered_read, end_stream);
    }
  }

  if (has_pending_write_data_) {
    if (!connection_closed_ &&
        read_callbacks_->connection().state() == Network::Connection::State::Open) {
      Buffer::OwnedImpl buffered_write;
      buffered_write.move(pending_write_data_);
      const bool end_stream = pending_write_end_stream_;
      has_pending_write_data_ = false;
      pending_write_end_stream_ = false;

      if (!processing_complete_ &&
          config_->processingMode().process_write() !=
              envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP) {
        sendRequest(buffered_write, end_stream, /*is_read=*/false);
      } else if (write_callbacks_ != nullptr) {
        write_callbacks_->injectWriteDataToFilterChain(buffered_write, end_stream);
      }
    } else {
      pending_write_data_.drain(pending_write_data_.length());
      has_pending_write_data_ = false;
      pending_write_end_stream_ = false;
    }
  }
}

void NetworkExtProcFilter::resumeAfterNewConnection(bool close_stream) {
  if (close_stream) {
    // Close the gRPC stream and stop timers before resuming downstream filters so no further
    // gRPC callbacks interleave, while keeping close-disabling active until resume completes.
    closeGrpcStreamOnly();
  }

  enableReadIfDisabled();

  if (!in_new_connection_) {
    read_callbacks_->continueReading();
  }

  if (!connection_closed_ &&
      read_callbacks_->connection().state() == Network::Connection::State::Open) {
    flushPendingDataAfterNewConnection();
  } else {
    clearPendingData();
  }

  if (close_stream) {
    resetCloseCallbackStatus();
  } else {
    updateCloseCallbackStatus(false, /*is_read=*/true);
  }
}

void NetworkExtProcFilter::handleNewConnectionResponse(const ProcessingResponse& response) {
  if (timeout_manager_) {
    timeout_manager_->stopConnectionTimer();
  }
  new_connection_pending_ = false;
  recordNewConnectionCallCompletion(Grpc::Status::WellKnownGrpcStatus::Ok);

  if (response.has_read_data() || response.has_write_data()) {
    ENVOY_CONN_LOG(warn,
                   "Ignoring unexpected read_data or write_data in response to new connection "
                   "event",
                   read_callbacks_->connection());
  }

  // Apply dynamic metadata before handling connection status so access logs capture metadata
  // even when the external processor closes the connection.
  processDynamicMetadata(response);

  handleConnectionStatus(response);
  if (processing_complete_) {
    return;
  }

  const bool should_close_stream = response.close_stream_to_ext_proc_server();
  if (should_close_stream) {
    ENVOY_CONN_LOG(
        debug,
        "External processor requested to close sidestream. Future data will bypass ext_proc.",
        read_callbacks_->connection());
    processing_complete_ = true;
  }

  resumeAfterNewConnection(should_close_stream);
}

void NetworkExtProcFilter::sendRequest(Buffer::Instance& data, bool end_stream, bool is_read) {
  if (stream_ == nullptr) {
    ENVOY_CONN_LOG(debug, "Cannot send request: stream is null", read_callbacks_->connection());
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
  addAttributes(request);

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

void NetworkExtProcFilter::onReceiveMessage(Grpc::ResponsePtr<ProcessingResponse>&& res) {
  if (processing_complete_) {
    ENVOY_CONN_LOG(debug, "Ignoring response message: processing already completed",
                   read_callbacks_->connection());
    stats_.spurious_msgs_received_.inc();
    return;
  }

  auto response = std::move(res);
  ENVOY_CONN_LOG(debug, "Received response from external processor", read_callbacks_->connection());
  stats_.stream_msgs_received_.inc();

  if (new_connection_pending_) {
    handleNewConnectionResponse(*response);
    return;
  }

  bool is_read = response->has_read_data();
  recordCallCompletion(Grpc::Status::WellKnownGrpcStatus::Ok, is_read);

  // Handle connection status before processing data
  handleConnectionStatus(*response);
  if (processing_complete_) {
    return;
  }

  processDynamicMetadata(*response);

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
      timeout_manager_->stopTimer(false);
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

  // Check if we should close the sidestream and bypass further processing.
  if (response->close_stream_to_ext_proc_server()) {
    ENVOY_CONN_LOG(
        debug,
        "External processor requested to close sidestream. Future data will bypass ext_proc.",
        read_callbacks_->connection());
    processing_complete_ = true;

    closeStream();
  }
}

void NetworkExtProcFilter::onGrpcError(Grpc::Status::GrpcStatus status,
                                       const std::string& message) {
  ENVOY_CONN_LOG(warn, "ext_proc: gRPC error: {}, message: {}", read_callbacks_->connection(),
                 status, message);
  // Mark processing as complete to avoid further gRPC calls
  processing_complete_ = true;
  const bool was_new_conn = new_connection_pending_;
  if (read_pending_) {
    recordCallCompletion(status, true);
  } else if (write_pending_) {
    recordCallCompletion(status, false);
  } else if (new_connection_pending_) {
    recordNewConnectionCallCompletion(status);
  }
  new_connection_pending_ = false;

  stats_.streams_grpc_error_.inc();

  // If failure mode is not to allow, close the connection
  if (!config_->failureModeAllow()) {
    ENVOY_CONN_LOG(debug, "Closing connection since failure model allow is not enabled",
                   read_callbacks_->connection());
    closeConnection("ext_proc_grpc_error", Network::ConnectionCloseType::FlushWrite);
    return;
  }

  stats_.failure_mode_allowed_.inc();
  if (was_new_conn) {
    resumeAfterNewConnection(/*close_stream=*/true);
  } else {
    closeStream();
  }
}

void NetworkExtProcFilter::onGrpcClose() {
  ENVOY_CONN_LOG(debug, "gRPC stream closed by peer", read_callbacks_->connection());
  processing_complete_ = true;
  const bool was_new_conn = new_connection_pending_;
  new_connection_pending_ = false;
  stats_.streams_grpc_close_.inc();

  if (was_new_conn) {
    if (!config_->failureModeAllow()) {
      closeConnection("ext_proc_grpc_close", Network::ConnectionCloseType::FlushWrite);
      return;
    }
    stats_.failure_mode_allowed_.inc();
    resumeAfterNewConnection(/*close_stream=*/true);
    return;
  }

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
    call_start_time = std::nullopt;
  }
}

void NetworkExtProcFilter::recordNewConnectionCallCompletion(Grpc::Status::GrpcStatus status) {
  if (logging_info_ == nullptr || !new_connection_call_start_time_.has_value()) {
    return;
  }

  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      read_callbacks_->connection().dispatcher().timeSource().monotonicTime() -
      new_connection_call_start_time_.value());

  logging_info_->recordGrpcCall(duration, status, /*is_read_direction=*/true);
  new_connection_call_start_time_ = std::nullopt;
}

void NetworkExtProcFilter::closeGrpcStreamOnly() {
  if (timeout_manager_) {
    timeout_manager_->stopAllTimers();
    timeout_manager_.reset();
  }

  // Clear pending flags
  new_connection_pending_ = false;
  new_connection_call_start_time_ = std::nullopt;
  read_pending_ = false;
  write_pending_ = false;
  write_call_start_time_ = std::nullopt;
  read_call_start_time_ = std::nullopt;

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

void NetworkExtProcFilter::resetCloseCallbackStatus() {
  // Ensure that any pending close-disabling is balanced and re-enabled.
  // If the sidestream is closed early (e.g. with close_stream_to_ext_proc_server, timeout,
  // or gRPC error), the close callbacks would remain disabled indefinitely, leading to
  // connection leaks. Regardless of how many outstanding requests were sent, calling
  // disableClose(false) once completely re-enables closure.
  if (disable_count_read_ > 0 && read_callbacks_ != nullptr) {
    read_callbacks_->disableClose(false);
    disable_count_read_ = 0;
  }
  if (disable_count_write_ > 0 && write_callbacks_ != nullptr) {
    write_callbacks_->disableClose(false);
    disable_count_write_ = 0;
  }
}

// Update closeStream to stop all timers
void NetworkExtProcFilter::closeStream() {
  closeGrpcStreamOnly();
  clearPendingData();
  resetCloseCallbackStatus();
}

void NetworkExtProcFilter::closeConnection(const std::string& reason,
                                           Network::ConnectionCloseType close_type) {
  ENVOY_CONN_LOG(
      info, "Closing connection: {}, close_type: {}", read_callbacks_->connection(), reason,
      close_type == Network::ConnectionCloseType::FlushWrite ? "FlushWrite" : "AbortReset");

  connection_closed_ = true;
  read_disabled_for_new_connection_ = false;
  closeStream();
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
  if ((metadata_sent_ && config_->sendOnce()) ||
      (config_->untypedForwardingMetadataNamespaces().empty() &&
       config_->typedForwardingMetadataNamespaces().empty())) {
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
  metadata_sent_ = true;
}

void NetworkExtProcFilter::addAttributes(ProcessingRequest& req) {
  if (attributes_sent_ || !config_->expressionManager().hasConnectionExpr()) {
    return;
  }

  auto activation_ptr = Filters::Common::Expr::createActivation(
      config_->expressionManager().localInfo(), read_callbacks_->connection().streamInfo(), nullptr,
      nullptr, nullptr);
  auto attributes = config_->expressionManager().evaluateConnectionAttributes(*activation_ptr);

  attributes_sent_ = true;
  (*req.mutable_attributes())[NetworkFilterNames::get().NetworkExternalProcessor] =
      std::move(attributes);
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
