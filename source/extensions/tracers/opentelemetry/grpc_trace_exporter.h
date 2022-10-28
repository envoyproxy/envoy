#pragma once

#include "envoy/grpc/async_client_manager.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;

/**
 * Exporter client for OTLP Traces. Provides abstraction on top of gRPC stream.
 */
class OpenTelemetryGrpcTraceExporterClient : Logger::Loggable<Logger::Id::tracing> {
public:
  OpenTelemetryGrpcTraceExporterClient(const Grpc::RawAsyncClientSharedPtr& client,
                                       const Protobuf::MethodDescriptor& service_method)
      : client_(client), service_method_(service_method) {}

  struct LocalStream : public Grpc::AsyncStreamCallbacks<
                           opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse> {
    LocalStream(OpenTelemetryGrpcTraceExporterClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(
        std::unique_ptr<opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse>&&)
        override {}
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.stream_ != nullptr);
      if (parent_.stream_->stream_ != nullptr) {
        // Only reset if we have a stream. Otherwise we had an inline failure and we will clear the
        // stream data in send().
        parent_.stream_.reset();
      }
    }

    OpenTelemetryGrpcTraceExporterClient& parent_;
    Grpc::AsyncStream<opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest>
        stream_{};
  };

  bool log(const ExportTraceServiceRequest& request) {
    // If we don't have a stream already, we need to initialize it.
    if (!stream_) {
      stream_ = std::make_unique<LocalStream>(*this);
    }

    // If we don't have a Grpc AsyncStream, we need to initialize it.
    if (stream_->stream_ == nullptr) {
      stream_->stream_ =
          client_->start(service_method_, *stream_, Http::AsyncClient::StreamOptions());
    }

    // If we do have a Grpc AsyncStream, we can first check if we are above the write buffer, and
    // send message if it's ok; if we don't have a stream, we need to clear out the stream data
    // after stream creation failed.
    if (stream_->stream_ != nullptr) {
      if (stream_->stream_->isAboveWriteBufferHighWatermark()) {
        return false;
      }
      stream_->stream_->sendMessage(request, false);
    } else {
      stream_.reset();
    }
    return true;
  }

  Grpc::AsyncClient<ExportTraceServiceRequest, ExportTraceServiceResponse> client_;
  std::unique_ptr<LocalStream> stream_;
  const Protobuf::MethodDescriptor& service_method_;
};

class OpenTelemetryGrpcTraceExporter : Logger::Loggable<Logger::Id::tracing> {
public:
  OpenTelemetryGrpcTraceExporter(const Grpc::RawAsyncClientSharedPtr& client);

  bool log(const ExportTraceServiceRequest& request);

private:
  OpenTelemetryGrpcTraceExporterClient client_;
};

using OpenTelemetryGrpcTraceExporterPtr = std::unique_ptr<OpenTelemetryGrpcTraceExporter>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
