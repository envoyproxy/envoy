#include "source/extensions/filters/network/ext_proc/ext_proc.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

NetworkExtProcFilter::~NetworkExtProcFilter() {
  ENVOY_CONN_LOG(debug, "boteng NetworkExtProcFilter::~NetworkExtProcFilter",
                  read_callbacks_->connection());
  if (stream_ != nullptr) {
    ENVOY_CONN_LOG(
        debug,
        "boteng NetworkExtProcFilter::~NetworkExtProcFilter stream_ != nullptr",
        read_callbacks_->connection());
    stream_->close();
  }
}

Network::FilterStatus NetworkExtProcFilter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "ext_proc: new connection", read_callbacks_->connection());
  return Network::FilterStatus::Continue;
}

Network::FilterStatus NetworkExtProcFilter::onData(
  Envoy::Buffer::Instance& data, bool end_stream) {
  auto state = openStream();
  ENVOY_CONN_LOG(debug, "onData openStreamState: {}",
                read_callbacks_->connection(), static_cast<int>(state));
  switch (state) {
    case StreamOpenState::Ok:
      break;
    case StreamOpenState::Error:
      return Network::FilterStatus::Continue;
    case StreamOpenState::IgnoreError:
      return Network::FilterStatus::Continue;
  }

  if (stream_ == nullptr) {
    return Envoy::Network::FilterStatus::Continue;
  }

  sendRequest(data, end_stream, /*is_read=*/true);
  return Envoy::Network::FilterStatus::StopIteration;
}

Network::FilterStatus NetworkExtProcFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "ext_proc: writing {} bytes of data", write_callbacks_->connection(),
                 data.length());
  auto state = openStream();
  ENVOY_CONN_LOG(debug, "onData openStreamState: {}",
                read_callbacks_->connection(), static_cast<int>(state));
  switch (state) {
    case StreamOpenState::Ok:
      break;
    case StreamOpenState::Error:
      return Envoy::Network::FilterStatus::Continue;
    case StreamOpenState::IgnoreError:
      return Envoy::Network::FilterStatus::Continue;
  }

  sendRequest(data, end_stream, /*is_read=*/true);
  return Envoy::Network::FilterStatus::StopIteration;
}

NetworkExtProcFilter::StreamOpenState NetworkExtProcFilter::openStream() {
  if (processing_complete_) {
    ENVOY_CONN_LOG(
        debug,
        "External processing is completed when trying to open the gRPC stream",
        read_callbacks_->connection());
    return StreamOpenState::IgnoreError;
  }

  if (!stream_) {
    ENVOY_CONN_LOG(debug, "Opening gRPC stream to external processor",
                   read_callbacks_->connection());

    Envoy::Http::AsyncClient::ParentContext grpc_context;
    grpc_context.stream_info = &read_callbacks_->connection().streamInfo();
    auto options = Envoy::Http::AsyncClient::StreamOptions()
                       .setParentContext(grpc_context)
                       .setBufferBodyForRetry(grpc_service_.has_retry_policy());

    ExternalProcessorStreamPtr stream_object =
    client_->start(*this, config_with_hash_key_, options, nullptr);

    ENVOY_CONN_LOG(info, "After start gRPC stream to external processor",
                   read_callbacks_->connection());

    if (stream_object == nullptr) {
      return StreamOpenState::Error;
    }
    stream_ = std::move(stream_object);
  }

  return StreamOpenState::Ok;
}

void NetworkExtProcFilter::sendRequest(Envoy::Buffer::Instance& data, bool end_stream, bool is_read) {
  ENVOY_CONN_LOG(debug, "boteng sendRequest", read_callbacks_->connection());
  if (stream_ == nullptr) {
    return;
  }
  ProcessingRequest request;
  if (is_read) {
    read_callbacks_->connection().readDisable(true);
    auto read_data = request.mutable_read_data();
    read_data->set_data(data.toString());
    read_data->set_end_stream(end_stream);
    ENVOY_CONN_LOG(info, "boteng sendRequest {}", read_callbacks_->connection(), request.DebugString());
    stream_->send(std::move(request), false);
  } else {
    auto write_data = request.mutable_write_data();
    write_data->set_data(data.toString());
    write_data->set_end_stream(end_stream);
    ENVOY_CONN_LOG(info, "boteng sendRequest {}", read_callbacks_->connection(),
    request.DebugString());
    stream_->send(std::move(request), false);
  }
  data.drain(data.length());
}

void NetworkExtProcFilter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& response) {
  auto response_internal = std::move(response);
  ENVOY_CONN_LOG(debug, "boteng received message from external processor",
                read_callbacks_->connection());
  switch (response_internal->data_case()) {
    case ProcessingResponse::kReadData: {
      auto data = response_internal->read_data();
      ENVOY_CONN_LOG(info,
                    "boteng received read data from external processor {}",
                    read_callbacks_->connection(), data.DebugString());
      auto buffer = Envoy::Buffer::OwnedImpl(data.data());
      read_callbacks_->injectReadDataToFilterChain(buffer, /*end_stream=*/data.end_stream());
      read_callbacks_->connection().readDisable(false);
      break;
    }
    case ProcessingResponse::kWriteData: {
      auto data = response_internal->write_data();
      ENVOY_CONN_LOG(info,
                    "boteng received write data from external processor {}",
                    read_callbacks_->connection(), data.DebugString());
      auto buffer = Envoy::Buffer::OwnedImpl(data.data());
      write_callbacks_->injectWriteDataToFilterChain(buffer, data.end_stream());
      if (read_callbacks_->connection().state() ==
          Envoy::Network::Connection::State::Closing) {
        read_callbacks_->connection().close(
            Envoy::Network::ConnectionCloseType::FlushWrite);
      }
      break;
    }
    default:
      ENVOY_LOG(debug, "Received message from external processor with no data");
      break;
}

ENVOY_CONN_LOG(debug, "Received message from external processor",
               read_callbacks_->connection());
}

void NetworkExtProcFilter::onGrpcError(
  Envoy::Grpc::Status::GrpcStatus grpc_status) {
    ENVOY_CONN_LOG(debug, "Received gRPC error on stream: {}",
                  read_callbacks_->connection(), grpc_status);

    if (processing_complete_) {
      return;
    }

    processing_complete_ = true;
    // Since the stream failed, there is no need to handle timeouts, so
    // make sure that they do not fire now.
    closeStream();
}

void NetworkExtProcFilter::onGrpcClose() {
    ENVOY_CONN_LOG(debug, "Received gRPC close on stream",
                  read_callbacks_->connection());

    processing_complete_ = true;
    closeStream();
}

void NetworkExtProcFilter::closeStream() {
  ENVOY_CONN_LOG(debug, "Closing stream", read_callbacks_->connection());
  if (stream_ == nullptr) {
    return;
  }

  auto closed = stream_->close();
  ENVOY_CONN_LOG(debug, "Calling close on stream, closed: {}.",
                read_callbacks_->connection(), closed);
  stream_ = nullptr;
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
