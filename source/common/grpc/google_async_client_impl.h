#pragma once

#include <queue>

#include "envoy/api/api.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/stats/scope.h"
#include "envoy/thread/thread.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/linked_object.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/grpc/google_grpc_context.h"
#include "common/grpc/stat_names.h"
#include "common/grpc/typed_async_client.h"
#include "common/tracing/http_tracer_impl.h"

#include "grpcpp/generic/generic_stub.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/support/proto_buffer_writer.h"

namespace Envoy {
namespace Grpc {

class GoogleAsyncStreamImpl;
class GoogleAsyncRequestImpl;

struct GoogleAsyncTag {
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

  GoogleAsyncTag(GoogleAsyncStreamImpl& stream, Operation op) : stream_(stream), op_(op) {}

  GoogleAsyncStreamImpl& stream_;
  const Operation op_;

  // Generate a void* tag for a given Operation.
  static void* tag(Operation op) { return reinterpret_cast<void*>(op); }
  // Extract Operation from void* tag.
  static Operation operation(void* tag) {
    return static_cast<Operation>(reinterpret_cast<intptr_t>(tag));
  }
};

class GoogleAsyncClientThreadLocal : public ThreadLocal::ThreadLocalObject,
                                     Logger::Loggable<Logger::Id::grpc> {
public:
  GoogleAsyncClientThreadLocal(Api::Api& api);
  ~GoogleAsyncClientThreadLocal() override;

  grpc::CompletionQueue& completionQueue() { return cq_; }

  void registerStream(GoogleAsyncStreamImpl* stream) {
    ASSERT(streams_.find(stream) == streams_.end());
    streams_.insert(stream);
  }

  void unregisterStream(GoogleAsyncStreamImpl* stream) {
    auto it = streams_.find(stream);
    ASSERT(it != streams_.end());
    streams_.erase(it);
  }

private:
  void completionThread();

  // There is blanket google-grpc initialization in MainCommonBase, but that
  // doesn't cover unit tests. However, putting blanket coverage in ProcessWide
  // causes background threaded memory allocation in all unit tests making it
  // hard to measure memory. Thus we also initialize grpc using our idempotent
  // wrapper-class in classes that need it. See
  // https://github.com/envoyproxy/envoy/issues/8282 for details.
  GoogleGrpcContext google_grpc_context_;

  // The CompletionQueue for in-flight operations. This must precede completion_thread_ to ensure it
  // is constructed before the thread runs.
  grpc::CompletionQueue cq_;
  // The threading model for the Google gRPC C++ library is not directly compatible with Envoy's
  // siloed model. We resolve this by issuing non-blocking asynchronous
  // operations on the GoogleAsyncClientImpl silo thread, and then synchronously
  // blocking on a completion queue, cq_, on a distinct thread. When cq_ events
  // are delivered, we cross-post to the silo dispatcher to continue the
  // operation.
  //
  // We have an independent completion thread for each TLS silo (i.e. one per worker and
  // also one for the main thread).
  Thread::ThreadPtr completion_thread_;
  // Track all streams that are currently using this CQ, so we can notify them
  // on shutdown.
  std::unordered_set<GoogleAsyncStreamImpl*> streams_;
};

// Google gRPC client stats. TODO(htuch): consider how a wider set of stats collected by the
// library, such as the census related ones, can be externalized as needed.
struct GoogleAsyncClientStats {
  // .streams_total
  Stats::Counter* streams_total_;
  // .streams_closed_<gRPC status code>
  std::array<Stats::Counter*, Status::WellKnownGrpcStatus::MaximumKnown + 1> streams_closed_;
};

// Interface to allow the gRPC stub to be mocked out by tests.
class GoogleStub {
public:
  virtual ~GoogleStub() = default;

  // See grpc::PrepareCall().
  virtual std::unique_ptr<grpc::GenericClientAsyncReaderWriter>
  PrepareCall(grpc::ClientContext* context, const grpc::string& method,
              grpc::CompletionQueue* cq) PURE;
};

class GoogleGenericStub : public GoogleStub {
public:
  GoogleGenericStub(std::shared_ptr<grpc::Channel> channel) : stub_(channel) {}

  std::unique_ptr<grpc::GenericClientAsyncReaderWriter>
  PrepareCall(grpc::ClientContext* context, const grpc::string& method,
              grpc::CompletionQueue* cq) override {
    return stub_.PrepareCall(context, method, cq);
  }

private:
  grpc::GenericStub stub_;
};

// Interface to allow the gRPC stub creation to be mocked out by tests.
class GoogleStubFactory {
public:
  virtual ~GoogleStubFactory() = default;

  // Create a stub from a given channel.
  virtual std::shared_ptr<GoogleStub> createStub(std::shared_ptr<grpc::Channel> channel) PURE;
};

class GoogleGenericStubFactory : public GoogleStubFactory {
public:
  std::shared_ptr<GoogleStub> createStub(std::shared_ptr<grpc::Channel> channel) override {
    return std::make_shared<GoogleGenericStub>(channel);
  }
};

// Google gRPC C++ client library implementation of Grpc::AsyncClient.
class GoogleAsyncClientImpl final : public RawAsyncClient, Logger::Loggable<Logger::Id::grpc> {
public:
  GoogleAsyncClientImpl(Event::Dispatcher& dispatcher, GoogleAsyncClientThreadLocal& tls,
                        GoogleStubFactory& stub_factory, Stats::ScopeSharedPtr scope,
                        const envoy::config::core::v3::GrpcService& config, Api::Api& api,
                        const StatNames& stat_names);
  ~GoogleAsyncClientImpl() override;

  // Grpc::AsyncClient
  AsyncRequest* sendRaw(absl::string_view service_full_name, absl::string_view method_name,
                        Buffer::InstancePtr&& request, RawAsyncRequestCallbacks& callbacks,
                        Tracing::Span& parent_span,
                        const Http::AsyncClient::RequestOptions& options) override;
  RawAsyncStream* startRaw(absl::string_view service_full_name, absl::string_view method_name,
                           RawAsyncStreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions& options) override;

  TimeSource& timeSource() { return dispatcher_.timeSource(); }

private:
  Event::Dispatcher& dispatcher_;
  GoogleAsyncClientThreadLocal& tls_;
  // This is shared with child streams, so that they can cleanup independent of
  // the client if it gets destructed. The streams need to wait for their tags
  // to drain from the CQ.
  std::shared_ptr<GoogleStub> stub_;
  std::list<std::unique_ptr<GoogleAsyncStreamImpl>> active_streams_;
  const std::string stat_prefix_;
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue> initial_metadata_;
  Stats::ScopeSharedPtr scope_;
  GoogleAsyncClientStats stats_;

  friend class GoogleAsyncClientThreadLocal;
  friend class GoogleAsyncRequestImpl;
  friend class GoogleAsyncStreamImpl;
};

class GoogleAsyncStreamImpl : public RawAsyncStream,
                              public Event::DeferredDeletable,
                              Logger::Loggable<Logger::Id::grpc>,
                              LinkedObject<GoogleAsyncStreamImpl> {
public:
  GoogleAsyncStreamImpl(GoogleAsyncClientImpl& parent, absl::string_view service_full_name,
                        absl::string_view method_name, RawAsyncStreamCallbacks& callbacks,
                        const Http::AsyncClient::StreamOptions& options);
  ~GoogleAsyncStreamImpl() override;

  virtual void initialize(bool buffer_body_for_retry);

  // Grpc::RawAsyncStream
  void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) override;
  void closeStream() override;
  void resetStream() override;

protected:
  bool call_failed() const { return call_failed_; }

private:
  // Process queued events in completed_ops_ with handleOpCompletion() on
  // GoogleAsyncClient silo thread.
  void onCompletedOps();
  // Handle Operation completion on GoogleAsyncClient silo thread. This is posted by
  // GoogleAsyncClientThreadLocal::completionThread() when a message is received on cq_.
  void handleOpCompletion(GoogleAsyncTag::Operation op, bool ok);
  // Convert from Google gRPC client std::multimap metadata to Envoy Http::HeaderMap.
  void metadataTranslate(const std::multimap<grpc::string_ref, grpc::string_ref>& grpc_metadata,
                         Http::HeaderMap& header_map);
  // Write the first PendingMessage in the write queue if non-empty.
  void writeQueued();
  // Deliver notification and update stats when the connection closes.
  void notifyRemoteClose(Status::GrpcStatus grpc_status,
                         Http::ResponseTrailerMapPtr trailing_metadata, const std::string& message);
  // Schedule stream for deferred deletion.
  void deferredDelete();
  // Cleanup and schedule stream for deferred deletion if no inflight
  // completions.
  void cleanup();

  // Pending serialized message on write queue. Only one Operation::Write is in-flight at any
  // point-in-time, so we queue pending writes here.
  struct PendingMessage {
    PendingMessage(Buffer::InstancePtr request, bool end_stream);
    // End-of-stream with no additional message.
    PendingMessage() = default;

    const absl::optional<grpc::ByteBuffer> buf_;
    const bool end_stream_{true};
  };

  GoogleAsyncTag init_tag_{*this, GoogleAsyncTag::Operation::Init};
  GoogleAsyncTag read_initial_metadata_tag_{*this, GoogleAsyncTag::Operation::ReadInitialMetadata};
  GoogleAsyncTag read_tag_{*this, GoogleAsyncTag::Operation::Read};
  GoogleAsyncTag write_tag_{*this, GoogleAsyncTag::Operation::Write};
  GoogleAsyncTag write_last_tag_{*this, GoogleAsyncTag::Operation::WriteLast};
  GoogleAsyncTag finish_tag_{*this, GoogleAsyncTag::Operation::Finish};

  GoogleAsyncClientImpl& parent_;
  GoogleAsyncClientThreadLocal& tls_;
  // Latch our own version of this reference, so that completionThread() doesn't
  // try and access via parent_, which might not exist in teardown. We assume
  // that the dispatcher lives longer than completionThread() life, which should
  // hold for the expected server object lifetimes.
  Event::Dispatcher& dispatcher_;
  // We hold a ref count on the stub_ to allow the stream to wait for its tags
  // to drain from the CQ on cleanup.
  std::shared_ptr<GoogleStub> stub_;
  std::string service_full_name_;
  std::string method_name_;
  RawAsyncStreamCallbacks& callbacks_;
  const Http::AsyncClient::StreamOptions& options_;
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
  // Have we entered CQ draining state? If so, we're just waiting for all our
  // ops on the CQ to drain away before freeing the stream.
  bool draining_cq_{};
  // Count of the tags in-flight. This must hit zero before the stream can be
  // freed.
  uint32_t inflight_tags_{};
  // Queue of completed (op, ok) passed from completionThread() to
  // handleOpCompletion().
  std::deque<std::pair<GoogleAsyncTag::Operation, bool>>
      completed_ops_ ABSL_GUARDED_BY(completed_ops_lock_);
  Thread::MutexBasicLockable completed_ops_lock_;

  friend class GoogleAsyncClientImpl;
  friend class GoogleAsyncClientThreadLocal;
};

class GoogleAsyncRequestImpl : public AsyncRequest,
                               public GoogleAsyncStreamImpl,
                               RawAsyncStreamCallbacks {
public:
  GoogleAsyncRequestImpl(GoogleAsyncClientImpl& parent, absl::string_view service_full_name,
                         absl::string_view method_name, Buffer::InstancePtr request,
                         RawAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                         const Http::AsyncClient::RequestOptions& options);

  void initialize(bool buffer_body_for_retry) override;

  // Grpc::AsyncRequest
  void cancel() override;

private:
  // Grpc::RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override;
  bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  Buffer::InstancePtr request_;
  RawAsyncRequestCallbacks& callbacks_;
  Tracing::SpanPtr current_span_;
  Buffer::InstancePtr response_;
};

} // namespace Grpc
} // namespace Envoy
