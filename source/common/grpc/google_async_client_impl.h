#pragma once

#include <queue>

#include "envoy/grpc/async_client.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/linked_object.h"
#include "common/common/thread.h"

#include "grpc++/generic/generic_stub.h"
#include "grpc++/grpc++.h"

namespace Envoy {
namespace Grpc {

class GoogleAsyncStreamImpl;
class GoogleAsyncRequestImpl;

// Google gRPC client stats. TODO(htuch): consider how a wider set of stats collected by the
// library, such as the census related ones, can be externalized as needed.
struct GoogleAsyncClientStats {
  // .streams_total
  Stats::Counter* streams_total_;
  // .streams_closed_<gRPC status code>
  std::array<Stats::Counter*, Status::GrpcStatus::MaximumValid + 1> streams_closed_;
};

// Google gRPC C++ client library implementation of Grpc::AsyncClient.
class GoogleAsyncClientImpl final : public AsyncClient {
public:
  GoogleAsyncClientImpl(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                        const envoy::api::v2::core::GrpcService::GoogleGrpc& config);
  ~GoogleAsyncClientImpl() override;

  // Grpc::AsyncClient
  AsyncRequest* send(const Protobuf::MethodDescriptor& service_method,
                     const Protobuf::Message& request, AsyncRequestCallbacks& callbacks,
                     Tracing::Span& parent_span,
                     const Optional<std::chrono::milliseconds>& timeout) override;
  AsyncStream* start(const Protobuf::MethodDescriptor& service_method,
                     AsyncStreamCallbacks& callbacks) override;

private:
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<grpc::GenericStub> stub_;
  std::list<std::unique_ptr<GoogleAsyncStreamImpl>> active_streams_;
  const std::string stat_prefix_;
  Stats::Scope& scope_;
  GoogleAsyncClientStats stats_;

  friend class GoogleAsyncRequestImpl;
  friend class GoogleAsyncStreamImpl;
};

class GoogleAsyncStreamImpl : public AsyncStream,
                              public Event::DeferredDeletable,
                              Logger::Loggable<Logger::Id::grpc>,
                              LinkedObject<GoogleAsyncStreamImpl> {
public:
  GoogleAsyncStreamImpl(GoogleAsyncClientImpl& parent,
                        const Protobuf::MethodDescriptor& service_method,
                        AsyncStreamCallbacks& callbacks,
                        const Optional<std::chrono::milliseconds>& timeout);
  ~GoogleAsyncStreamImpl() override;

  virtual void initialize(bool buffer_body_for_retry);

  // Grpc::AsyncStream
  void sendMessage(const Protobuf::Message& request, bool end_stream) override;
  void closeStream() override;
  void resetStream() override;

protected:
  bool call_failed() const { return call_failed_; }

private:
  // Operation defines tags that are handed to the gRPC AsyncReaderWriter for use in completion
  // notification for their namesake operations. Read* and Write* operations may be outstanding
  // simultaneously, but there will be no more than one operation of each type in-flight for a given
  // stream. Init and Finish will both be issued exclusively when no other operations are in-flight
  // for a stream. See
  // https://github.com/grpc/grpc/blob/master/include/grpc%2B%2B/impl/codegen/async_stream.h for
  // further insight into the semantics of the different gRPC client operations.
  enum Operation {
    // Initial stub call issued, waiting for initialization to complete.
    Init = 0,
    // Waiting for initial meta-data from server following Init completion.
    ReadInitialMetadata,
    // Waiting for response protobuf from server following ReadInitialMetadata completion.
    Read,
    // Waiting for write of request protobuf to server to complete.
    Write,
    // Waiting for write of request protobuf (EOS) __OR__ an EOS WritesDone to server to complete.
    WriteLast,
    // Waiting for final status. This must only be issued once all Read* and Write* operations have
    // completed.
    Finish,
  };

  // Generate a void* tag for a given Operation.
  static void* tag(Operation op) { return reinterpret_cast<void*>(op); }
  // Extract Operation from void* tag.
  static Operation operation(void* tag) {
    return static_cast<Operation>(reinterpret_cast<intptr_t>(tag));
  }

  // Handle Operation completion on GoogleAsyncClient silo thread. This is posted by
  // completionThread() when a message is received on cq_.
  void handleOpCompletion(Operation op, bool ok);
  // Convert from Google gRPC client std::multimap metadata to Envoy Http::HeaderMap.
  void metadataTranslate(const std::multimap<grpc::string_ref, grpc::string_ref>& grpc_metadata,
                         Http::HeaderMap& header_map);
  // The threading model for the Google gRPC C++ library is not directly compatible with Envoy's
  // siloed model. We resolve this by issuing non-blocking asynchronous operations on the silo
  // thread, and then synchronously blocking on a completion queue, cq_, on a distinct thread. When
  // cq_ events are delivered, we cross-post to the silo dispatcher to continue the operation.
  //
  // Currently, we have an independent thread for each stream that manages incoming events on cq_.
  //
  // TODO(htuch): Ideally we would have no more than one completion thread per silo. Having less
  // than one per silo would be bad, as we would have potential contention, but having more than one
  // per silo has unnecessary thread overhead, and in the current implementation we have thread
  // creation/teardown overhead on every gRPC stream or unary RPC, which is sub-optimal. It is
  // however, simple to grok for an initial implementation.
  void completionThread();
  // Write the first PendingMessage in the write queue if non-empty.
  void writeQueued();
  // Deliver notification and update stats when the connection closes.
  void notifyRemoteClose(Status::GrpcStatus grpc_status, Http::HeaderMapPtr trailing_metadata,
                         const std::string& message);
  // Cleanup and schedule stream for deferred deletion.
  void cleanup();

  // Pending serialized message on write queue. Only one Operation::Write is in-flight at any
  // point-in-time, so we queue pending writes here.
  struct PendingMessage {
    // We serialize the message to a grpc::ByteBuffer prior to queueing.
    // TODO(htuch): We shouldn't need to do a string serialization & copy steps here (effectively,
    // two copies!), this can be done more efficiently with SerializeToZeroCopyStream as done by
    // TensorFlow
    // https://github.com/tensorflow/tensorflow/blob/f9462e82ac3981d7a3b5bf392477a585fb6e6912/tensorflow/core/distributed_runtime/rpc/grpc_serialization_traits.h#L189
    // at the cost of some additional implementation complexity.
    // Also see
    // https://github.com/grpc/grpc/blob/5e82dddc056bd488e0ba1ba0057247ab23e442d4/include/grpc%2B%2B/impl/codegen/proto_utils.h#L42
    // which gives us what we want for zero copy, but relies on grpc::internal details; we can't get
    // a grpc_byte_buffer from grpc::ByteBuffer to use this.
    PendingMessage(const Protobuf::Message& request, bool end_stream)
        : slice_(request.SerializeAsString()), buf_(grpc::ByteBuffer(&slice_, 1)),
          end_stream_(end_stream) {}
    // End-of-stream with no additional message.
    PendingMessage() : end_stream_(true) {}

    const grpc::Slice slice_;
    const Optional<grpc::ByteBuffer> buf_;
    const bool end_stream_;
  };

  GoogleAsyncClientImpl& parent_;
  const Protobuf::MethodDescriptor& service_method_;
  AsyncStreamCallbacks& callbacks_;
  const Optional<std::chrono::milliseconds>& timeout_;
  // The CompletionQueue for in-flight operations. This must precede completion_thread_ to ensure it
  // is constructed before the thread runs.
  grpc::CompletionQueue cq_;
  // This thread is responsible for consuming cq_.
  Thread::ThreadPtr completion_thread_;
  grpc::ClientContext ctxt_;
  std::unique_ptr<grpc::GenericClientAsyncReaderWriter> rw_;
  std::queue<PendingMessage> write_pending_queue_;
  grpc::ByteBuffer read_buf_;
  grpc::Status status_;
  // Has Operation::Init completed?
  bool call_initialized_{};
  // Did the stub Call fail? If this is true, no Operation::Init completion will ever occur.
  bool call_failed_{};
  // Is there an Operation::Write[Last] in-flight?
  bool write_pending_{};
  // Is an Operation::Finish in-flight?
  bool finish_pending_{};
  // Is a cq_.Shutdown() in-progress?
  bool cq_shutdown_in_progress_{};

  friend class GoogleAsyncClientImpl;
};

class GoogleAsyncRequestImpl : public AsyncRequest,
                               public GoogleAsyncStreamImpl,
                               AsyncStreamCallbacks {
public:
  GoogleAsyncRequestImpl(GoogleAsyncClientImpl& parent,
                         const Protobuf::MethodDescriptor& service_method,
                         const Protobuf::Message& request, AsyncRequestCallbacks& callbacks,
                         Tracing::Span& parent_span,
                         const Optional<std::chrono::milliseconds>& timeout);

  void initialize(bool buffer_body_for_retry) override;

  // Grpc::AsyncRequest
  void cancel() override;

private:
  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override;
  void onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override;
  ProtobufTypes::MessagePtr createEmptyResponse() override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  const Protobuf::Message& request_;
  AsyncRequestCallbacks& callbacks_;
  Tracing::SpanPtr current_span_;
  ProtobufTypes::MessagePtr response_;
};

} // namespace Grpc
} // namespace Envoy
