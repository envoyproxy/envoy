#include "common/grpc/google_async_client_impl.h"

#include "common/common/empty_string.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Grpc {

GoogleAsyncClientImpl::GoogleAsyncClientImpl(
    Event::Dispatcher& dispatcher, Stats::Scope& scope,
    const envoy::api::v2::core::GrpcService::GoogleGrpc& config)
    : dispatcher_(dispatcher), stat_prefix_(config.stat_prefix()), scope_(scope) {
  // We rebuild the channel each time we construct the channel. It appears that the gRPC library is
  // smart enough to do connection pooling and reuse with identical channel args, so this should
  // have comparable overhead to what we are doing in Grpc::AsyncClientImpl, i.e. no expensive
  // new connection implied.
  std::shared_ptr<grpc::Channel> channel = createChannel(config);
  stub_ = std::make_unique<grpc::GenericStub>(channel);
  // Initialize client stats.
  stats_.streams_total_ = &scope_.counter("streams_total");
  for (uint32_t i = 0; i <= Status::GrpcStatus::MaximumValid; ++i) {
    stats_.streams_closed_[i] = &scope_.counter(fmt::format("streams_closed_{}", i));
  }
}

GoogleAsyncClientImpl::~GoogleAsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
  }
}

AsyncRequest* GoogleAsyncClientImpl::send(const Protobuf::MethodDescriptor& service_method,
                                          const Protobuf::Message& request,
                                          AsyncRequestCallbacks& callbacks,
                                          Tracing::Span& parent_span,
                                          const Optional<std::chrono::milliseconds>& timeout) {
  auto* const async_request =
      new GoogleAsyncRequestImpl(*this, service_method, request, callbacks, parent_span, timeout);
  std::unique_ptr<GoogleAsyncStreamImpl> grpc_stream{async_request};

  grpc_stream->initialize(true);
  if (grpc_stream->call_failed()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return async_request;
}

AsyncStream* GoogleAsyncClientImpl::start(const Protobuf::MethodDescriptor& service_method,
                                          AsyncStreamCallbacks& callbacks) {
  const Optional<std::chrono::milliseconds> no_timeout;
  auto grpc_stream =
      std::make_unique<GoogleAsyncStreamImpl>(*this, service_method, callbacks, no_timeout);

  grpc_stream->initialize(false);
  if (grpc_stream->call_failed()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return active_streams_.front().get();
}

GoogleAsyncStreamImpl::GoogleAsyncStreamImpl(GoogleAsyncClientImpl& parent,
                                             const Protobuf::MethodDescriptor& service_method,
                                             AsyncStreamCallbacks& callbacks,
                                             const Optional<std::chrono::milliseconds>& timeout)
    : parent_(parent), service_method_(service_method), callbacks_(callbacks), timeout_(timeout),
      completion_thread_(new Thread::Thread([this] { completionThread(); })) {}

GoogleAsyncStreamImpl::~GoogleAsyncStreamImpl() { ASSERT(rw_ == nullptr); }

// TODO(htuch): figure out how to propagate "this request should be buffered for
// retry" bit to Google gRPC library.
void GoogleAsyncStreamImpl::initialize(bool /*buffer_body_for_retry*/) {
  parent_.stats_.streams_total_->inc();
  gpr_timespec abs_deadline =
      timeout_.valid() ? gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                      gpr_time_from_millis(timeout_.value().count(), GPR_TIMESPAN))
                       : gpr_inf_future(GPR_CLOCK_REALTIME);
  ctxt_.set_deadline(abs_deadline);
  // Due to the different HTTP header implementations, we effectively double
  // copy headers here.
  Http::HeaderMapImpl initial_metadata;
  callbacks_.onCreateInitialMetadata(initial_metadata);
  initial_metadata.iterate(
      [](const Http::HeaderEntry& header, void* ctxt) {
        auto* client_context = static_cast<grpc::ClientContext*>(ctxt);
        client_context->AddMetadata(header.key().c_str(), header.value().c_str());
        return Http::HeaderMap::Iterate::Continue;
      },
      &ctxt_);
  // Invoke stub call.
  rw_ = parent_.stub_->Call(
      &ctxt_, "/" + service_method_.service()->full_name() + "/" + service_method_.name(), &cq_,
      tag(Operation::Init));
  if (rw_ == nullptr) {
    notifyRemoteClose(Status::GrpcStatus::Unavailable, nullptr, EMPTY_STRING);
    call_failed_ = true;
  }
}

void GoogleAsyncStreamImpl::notifyRemoteClose(Status::GrpcStatus grpc_status,
                                              Http::HeaderMapPtr trailing_metadata,
                                              const std::string& message) {
  if (grpc_status > Status::GrpcStatus::MaximumValid) {
    grpc_status = Status::GrpcStatus::Unknown;
  }
  ENVOY_LOG(debug, "notifyRemoteClose {} {}", grpc_status, message);
  parent_.stats_.streams_closed_[grpc_status]->inc();
  callbacks_.onReceiveTrailingMetadata(trailing_metadata ? std::move(trailing_metadata)
                                                         : std::make_unique<Http::HeaderMapImpl>());
  callbacks_.onRemoteClose(grpc_status, message);
}

void GoogleAsyncStreamImpl::sendMessage(const Protobuf::Message& request, bool end_stream) {
  write_pending_queue_.emplace(request, end_stream);
  ENVOY_LOG(trace, "Queued message to write ({} bytes)",
            write_pending_queue_.back().buf_.value().Length());
  writeQueued();
}

void GoogleAsyncStreamImpl::closeStream() {
  // Empty EOS write queued.
  write_pending_queue_.emplace();
  writeQueued();
}

void GoogleAsyncStreamImpl::resetStream() { cleanup(); }

void GoogleAsyncStreamImpl::writeQueued() {
  if (!call_initialized_ || finish_pending_ || write_pending_ || write_pending_queue_.empty()) {
    return;
  }
  write_pending_ = true;
  const PendingMessage& msg = write_pending_queue_.front();

  if (!msg.buf_.valid()) {
    ASSERT(msg.end_stream_);
    rw_->WritesDone(tag(Operation::WriteLast));
  } else if (msg.end_stream_) {
    grpc::WriteOptions write_options;
    rw_->WriteLast(msg.buf_.value(), write_options, tag(Operation::WriteLast));
  } else {
    rw_->Write(msg.buf_.value(), tag(Operation::Write));
  }
}

void GoogleAsyncStreamImpl::handleOpCompletion(Operation op, bool ok) {
  ENVOY_LOG(trace, "handleOpCompletion {} {}", op, ok);
  // Ignore op completions while CQ is shutting down.
  if (cq_shutdown_in_progress_) {
    return;
  }
  // Consider failure cases first.
  if (!ok) {
    // Early fails can be just treated as Internal.
    if (op == Operation::Init || op == Operation::ReadInitialMetadata) {
      notifyRemoteClose(Status::GrpcStatus::Internal, nullptr, EMPTY_STRING);
      resetStream();
      return;
    }
    // Remote server has closed, we can pick up some meaningful status.
    // TODO(htuch): We're assuming here that a failed Write/WriteLast operation will result in
    // stream termination, and pick up on the failed Read here. Confirm that this assumption is
    // valid.
    if (op == Operation::Read) {
      finish_pending_ = true;
      rw_->Finish(&status_, tag(Operation::Finish));
    }
    return;
  }
  switch (op) {
  case Operation::Init: {
    ASSERT(ok);
    ASSERT(!call_initialized_);
    call_initialized_ = true;
    rw_->ReadInitialMetadata(tag(Operation::ReadInitialMetadata));
    writeQueued();
    break;
  }
  case Operation::ReadInitialMetadata: {
    ASSERT(ok);
    ASSERT(call_initialized_);
    rw_->Read(&read_buf_, tag(Operation::Read));
    Http::HeaderMapPtr initial_metadata = std::make_unique<Http::HeaderMapImpl>();
    metadataTranslate(ctxt_.GetServerInitialMetadata(), *initial_metadata);
    callbacks_.onReceiveInitialMetadata(std::move(initial_metadata));
    break;
  }
  case Operation::Write: {
    ASSERT(ok);
    write_pending_ = false;
    write_pending_queue_.pop();
    writeQueued();
    break;
  }
  case Operation::WriteLast: {
    ASSERT(ok);
    write_pending_ = false;
    break;
  }
  case Operation::Read: {
    ASSERT(ok);
    std::vector<grpc::Slice> slices;
    // Assuming this only fails due to OOM.
    RELEASE_ASSERT(read_buf_.Dump(&slices).ok());
    // TODO(htuch): As with PendingMessage serialization, the deserialization
    // here is not optimal, as we are converting between string representation
    // and have unnecessary copies. We should use ParseFromCodedStream as done
    // by TensorFlow
    // https://github.com/tensorflow/tensorflow/blob/f9462e82ac3981d7a3b5bf392477a585fb6e6912/tensorflow/core/distributed_runtime/rpc/grpc_serialization_traits.h#L210
    // at the cost of some additional implementation complexity.
    // Also see
    // https://github.com/grpc/grpc/blob/5e82dddc056bd488e0ba1ba0057247ab23e442d4/include/grpc%2B%2B/impl/codegen/proto_utils.h#L113
    // which gives us what we want for zero copy, but relies on grpc::internal details; we can't get
    // a grpc_byte_buffer from grpc::ByteBuffer to use this.
    grpc::string buf;
    buf.reserve(read_buf_.Length());
    for (const auto& slice : slices) {
      buf.append(reinterpret_cast<const char*>(slice.begin()), slice.size());
    }
    ProtobufTypes::MessagePtr response = callbacks_.createEmptyResponse();
    if (!response->ParseFromString(buf)) {
      // This is basically streamError in Grpc::AsyncClientImpl.
      notifyRemoteClose(Status::GrpcStatus::Internal, nullptr, EMPTY_STRING);
      resetStream();
      break;
    };
    callbacks_.onReceiveMessageUntyped(std::move(response));
    rw_->Read(&read_buf_, tag(Operation::Read));
    break;
  }
  case Operation::Finish: {
    ASSERT(finish_pending_);
    ENVOY_LOG(debug, "Finish with grpc-status code {}", status_.error_code());
    Http::HeaderMapPtr trailing_metadata = std::make_unique<Http::HeaderMapImpl>();
    metadataTranslate(ctxt_.GetServerTrailingMetadata(), *trailing_metadata);
    const Status::GrpcStatus grpc_status =
        status_.error_code() <= grpc::StatusCode::DATA_LOSS
            ? static_cast<Status::GrpcStatus>(status_.error_code())
            : Status::GrpcStatus::InvalidCode;
    notifyRemoteClose(grpc_status, std::move(trailing_metadata), status_.error_message());
    cleanup();
    break;
  }
  default:
    NOT_REACHED;
  }
}

void GoogleAsyncStreamImpl::metadataTranslate(
    const std::multimap<grpc::string_ref, grpc::string_ref>& grpc_metadata,
    Http::HeaderMap& header_map) {
  // More painful copying, this time due to the mismatch in header
  // representation data structures in Envoy and Google gRPC.
  for (auto it : grpc_metadata) {
    header_map.addCopy(Http::LowerCaseString(std::string(it.first.data(), it.first.size())),
                       std::string(it.second.data(), it.second.size()));
  }
}

void GoogleAsyncStreamImpl::completionThread() {
  ENVOY_LOG(debug, "completionThread running");
  void* tag;
  bool ok;
  while (cq_.Next(&tag, &ok)) {
    ENVOY_LOG(trace, "completionThread CQ event {} {}", operation(tag), ok);
    parent_.dispatcher_.post([ this, op = operation(tag), ok ] { handleOpCompletion(op, ok); });
  }
  ENVOY_LOG(debug, "completionThread exiting");
}

void GoogleAsyncStreamImpl::cleanup() {
  ENVOY_LOG(debug, "Stream cleanup");

  cq_shutdown_in_progress_ = true;
  cq_.Shutdown();
  ctxt_.TryCancel();
  ENVOY_LOG(debug, "CQ shutdown in progress");

  // We join the completionThread during stream shutdown. This might appear to
  // be a dangerous blocking operation, but should (?) be relatively
  // non-contended; the completionThread is dedicated to this stream and should
  // not be holding locks.
  // TODO(htuch): Validate this assertion, and/or pursue per-silo CQ thread that
  // doesn't require joining on stream shutdown.
  ENVOY_LOG(debug, "Joining completionThread");
  completion_thread_->join();
  ENVOY_LOG(debug, "Joined completionThread");
  rw_ = nullptr;

  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (LinkedObject<GoogleAsyncStreamImpl>::inserted()) {
    parent_.dispatcher_.deferredDelete(
        LinkedObject<GoogleAsyncStreamImpl>::removeFromList(parent_.active_streams_));
  }
}

GoogleAsyncRequestImpl::GoogleAsyncRequestImpl(GoogleAsyncClientImpl& parent,
                                               const Protobuf::MethodDescriptor& service_method,
                                               const Protobuf::Message& request,
                                               AsyncRequestCallbacks& callbacks,
                                               Tracing::Span& parent_span,
                                               const Optional<std::chrono::milliseconds>& timeout)
    : GoogleAsyncStreamImpl(parent, service_method, *this, timeout), request_(request),
      callbacks_(callbacks) {
  current_span_ = parent_span.spawnChild(Tracing::EgressConfig::get(),
                                         "async " + parent.stat_prefix_ + " egress",
                                         ProdSystemTimeSource::instance_.currentTime());
  current_span_->setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, parent.stat_prefix_);
  current_span_->setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY);
}

void GoogleAsyncRequestImpl::initialize(bool buffer_body_for_retry) {
  GoogleAsyncStreamImpl::initialize(buffer_body_for_retry);
  if (this->call_failed()) {
    return;
  }
  this->sendMessage(request_, true);
}

void GoogleAsyncRequestImpl::cancel() {
  current_span_->setTag(Tracing::Tags::get().STATUS, Tracing::Tags::get().CANCELED);
  current_span_->finishSpan();
  this->resetStream();
}

void GoogleAsyncRequestImpl::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  current_span_->injectContext(metadata);
  callbacks_.onCreateInitialMetadata(metadata);
}

void GoogleAsyncRequestImpl::onReceiveInitialMetadata(Http::HeaderMapPtr&&) {}

void GoogleAsyncRequestImpl::onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& message) {
  response_ = std::move(message);
}

void GoogleAsyncRequestImpl::onReceiveTrailingMetadata(Http::HeaderMapPtr&&) {}

ProtobufTypes::MessagePtr GoogleAsyncRequestImpl::createEmptyResponse() {
  return callbacks_.createEmptyResponse();
}

void GoogleAsyncRequestImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                           const std::string& message) {
  current_span_->setTag(Tracing::Tags::get().GRPC_STATUS_CODE, std::to_string(status));

  if (status != Grpc::Status::GrpcStatus::Ok) {
    current_span_->setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE);
    callbacks_.onFailure(status, message, *current_span_);
  } else if (response_ == nullptr) {
    current_span_->setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE);
    callbacks_.onFailure(Status::Internal, EMPTY_STRING, *current_span_);
  } else {
    callbacks_.onSuccessUntyped(std::move(response_), *current_span_);
  }

  current_span_->finishSpan();
}

} // namespace Grpc
} // namespace Envoy
