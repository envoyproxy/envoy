#include "source/extensions/filters/network/ext_proc/ext_proc.h"

#include "source/extensions/filters/network/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

using envoy::service::network_ext_proc::v3::ProcessingRequest;
using envoy::service::network_ext_proc::v3::ProcessingResponse;

NetworkExtProcFilter::~NetworkExtProcFilter() {
  ENVOY_CONN_LOG(debug, "boteng NetworkExtProcFilter::~NetworkExtProcFilter",
                 read_callbacks_->connection());
  if (stream_ != nullptr) {
    stream_->close();
  }
}

Network::FilterStatus NetworkExtProcFilter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "ext_proc: new connection", read_callbacks_->connection());
  return Network::FilterStatus::Continue;
}

Network::FilterStatus NetworkExtProcFilter::onData(Envoy::Buffer::Instance& data, bool end_stream) {
  auto state = openStream();
  ENVOY_CONN_LOG(debug, "onData openStreamState: {}", read_callbacks_->connection(),
                 static_cast<int>(state));
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
  ENVOY_CONN_LOG(debug, "onWrite openStreamState: {}", read_callbacks_->connection(),
                 static_cast<int>(state));
  switch (state) {
  case StreamOpenState::Ok:
    break;
  case StreamOpenState::Error:
    return Envoy::Network::FilterStatus::Continue;
  case StreamOpenState::IgnoreError:
    return Envoy::Network::FilterStatus::Continue;
  }

  sendRequest(data, end_stream, /*is_read=*/false);
  return Envoy::Network::FilterStatus::StopIteration;
}

void NetworkExtProcFilter::onDownstreamEvent(Envoy::Network::ConnectionEvent event) {
  if (event == Envoy::Network::ConnectionEvent::LocalClose ||
      event == Envoy::Network::ConnectionEvent::RemoteClose) {
    closeStream();
  }
};

NetworkExtProcFilter::StreamOpenState NetworkExtProcFilter::openStream() {
  if (processing_complete_) {
    ENVOY_CONN_LOG(debug, "External processing is completed when trying to open the gRPC stream",
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
        client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

    ENVOY_CONN_LOG(info, "After start gRPC stream to external processor",
                   read_callbacks_->connection());

    if (stream_object == nullptr) {
      return StreamOpenState::Error;
    }
    stream_ = std::move(stream_object);
  }

  return StreamOpenState::Ok;
}

void NetworkExtProcFilter::sendRequest(Envoy::Buffer::Instance& data, bool end_stream,
                                       bool is_read) {
  if (stream_ == nullptr) {
    return;
  }
  ProcessingRequest request;
  if (is_read) {
    read_callbacks_->connection().readDisable(true);
    read_callbacks_->disableClose(true);
    auto read_data = request.mutable_read_data();
    read_data->set_data(data.toString());
    read_data->set_end_of_stream(end_stream);
    ENVOY_CONN_LOG(info, "boteng sendRequest {}", read_callbacks_->connection(),
                   request.DebugString());
    stream_->send(std::move(request), false);
  } else {
    auto write_data = request.mutable_write_data();
    write_data->set_data(data.toString());
    write_data->set_end_of_stream(end_stream);
    ENVOY_CONN_LOG(info, "boteng sendRequest {}", read_callbacks_->connection(),
                   request.DebugString());
    write_callbacks_->disableClose(true);
    stream_->send(std::move(request), false);
  }
  data.drain(data.length());
}

void NetworkExtProcFilter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& response) {
  auto response_internal = std::move(response);
  ENVOY_CONN_LOG(debug, "boteng received message from external processor",
                 read_callbacks_->connection());

  // Check for read_data field
  if (response_internal->has_read_data()) {
    auto data = response_internal->read_data();
    ENVOY_CONN_LOG(info, "boteng received read data from external processor {}",
                   read_callbacks_->connection(), data.DebugString());
    auto buffer = Envoy::Buffer::OwnedImpl(data.data());
    read_callbacks_->injectReadDataToFilterChain(buffer, /*end_stream=*/data.end_of_stream());
    read_callbacks_->connection().readDisable(false);
    read_callbacks_->disableClose(false);
  }
  // Check for write_data field
  else if (response_internal->has_write_data()) {
    auto data = response_internal->write_data();
    ENVOY_CONN_LOG(info, "boteng received write data from external processor {}",
                   read_callbacks_->connection(), data.DebugString());
    auto buffer = Envoy::Buffer::OwnedImpl(data.data());
    write_callbacks_->injectWriteDataToFilterChain(buffer, data.end_of_stream());
    write_callbacks_->disableClose(false);
  }
  // No data case
  else {
    ENVOY_LOG(debug, "Received message from external processor with no data");
  }
}

void NetworkExtProcFilter::onGrpcError(Envoy::Grpc::Status::GrpcStatus grpc_status,
                                       const std::string& message) {
  ENVOY_CONN_LOG(debug, "Received gRPC error on stream: {}, with message {}",
                 read_callbacks_->connection(), grpc_status, message);

  if (processing_complete_) {
    return;
  }

  processing_complete_ = true;
  // Since the stream failed, there is no need to handle timeouts, so
  // make sure that they do not fire now.
  closeStream();
}

void NetworkExtProcFilter::onGrpcClose() {
  ENVOY_CONN_LOG(debug, "Received gRPC close on stream", read_callbacks_->connection());

  processing_complete_ = true;
  closeStream();
}

void NetworkExtProcFilter::closeStream() {
  ENVOY_CONN_LOG(debug, "Closing stream", read_callbacks_->connection());

  // Re-enable any disabled processing
  if (read_callbacks_) {
    read_callbacks_->disableClose(false);
  }

  if (write_callbacks_) {
    write_callbacks_->disableClose(false);
  }

  if (stream_ == nullptr) {
    return;
  }

  auto closed = stream_->close();
  ENVOY_CONN_LOG(debug, "Calling close on stream, closed: {}.", read_callbacks_->connection(),
                 closed);
  stream_ = nullptr;
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
