#include "source/extensions/common/tap/admin.h"

#include "envoy/admin/v3/tap.pb.h"
#include "envoy/admin/v3/tap.pb.validate.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(tap_admin_handler);

AdminHandlerSharedPtr AdminHandler::getSingleton(OptRef<Server::Admin> admin,
                                                 Singleton::Manager& singleton_manager,
                                                 Event::Dispatcher& main_thread_dispatcher) {
  return singleton_manager.getTyped<AdminHandler>(
      SINGLETON_MANAGER_REGISTERED_NAME(tap_admin_handler), [&admin, &main_thread_dispatcher] {
        return std::make_shared<AdminHandler>(admin, main_thread_dispatcher);
      });
}

AdminHandler::AdminHandler(OptRef<Server::Admin> admin, Event::Dispatcher& main_thread_dispatcher)
    : admin_(admin.value()), main_thread_dispatcher_(main_thread_dispatcher) {
  const bool rc =
      admin_.addHandler("/tap", "tap filter control", MAKE_ADMIN_HANDLER(handler), true, true);
  RELEASE_ASSERT(rc, "/tap admin endpoint is taken");
  if (admin_.socket().addressType() == Network::Address::Type::Pipe) {
    ENVOY_LOG(warn, "Admin tapping (via /tap) is unreliable when the admin endpoint is a pipe and "
                    "the connection is HTTP/1. Either use an IP address or connect using HTTP/2.");
  }
}

AdminHandler::~AdminHandler() {
  const bool rc = admin_.removeHandler("/tap");
  ASSERT(rc);
}

Http::Code AdminHandler::handler(Http::HeaderMap&, Buffer::Instance& response,
                                 Server::AdminStream& admin_stream) {
  if (attached_request_ != nullptr) {
    // TODO(mattlklein123): Consider supporting concurrent admin /tap streams. Right now we support
    // a single stream as a simplification.
    return badRequest(response, "An attached /tap admin stream already exists. Detach it.");
  }

  if (admin_stream.getRequestBody() == nullptr) {
    return badRequest(response, "/tap requires a JSON/YAML body");
  }

  envoy::admin::v3::TapRequest tap_request;
  TRY_NEEDS_AUDIT {
    MessageUtil::loadFromYamlAndValidate(admin_stream.getRequestBody()->toString(), tap_request,
                                         ProtobufMessage::getStrictValidationVisitor());
  }
  END_TRY catch (EnvoyException& e) { return badRequest(response, e.what()); }

  ENVOY_LOG(debug, "tap admin request for config_id={}", tap_request.config_id());
  if (config_id_map_.count(tap_request.config_id()) == 0) {
    return badRequest(
        response, fmt::format("Unknown config id '{}'. No extension has registered with this id.",
                              tap_request.config_id()));
  }
  for (auto config : config_id_map_[tap_request.config_id()]) {
    config->newTapConfig(tap_request.tap_config(), this);
  }

  admin_stream.setEndStreamOnComplete(false);
  admin_stream.addOnDestroyCallback([this] {
    for (auto config : config_id_map_[attached_request_->id()]) {
      ENVOY_LOG(debug, "detach tap admin request for config_id={}", attached_request_->id());
      config->clearTapConfig();
    }
    attached_request_.reset(); // remove ref to attached_request_
  });
  attached_request_ = AttachedRequest::createAttachedRequest(this, tap_request, &admin_stream);
  return Http::Code::OK;
}

Http::Code AdminHandler::badRequest(Buffer::Instance& response, absl::string_view error) {
  ENVOY_LOG(debug, "handler bad request: {}", error);
  response.add(error);
  return Http::Code::BadRequest;
}

void AdminHandler::registerConfig(ExtensionConfig& config, const std::string& config_id) {
  ASSERT(!config_id.empty());
  ASSERT(config_id_map_[config_id].count(&config) == 0);
  config_id_map_[config_id].insert(&config);
  if (attached_request_ != nullptr && attached_request_->id() == config_id) {
    config.newTapConfig(attached_request_->config(), this);
  }
}

void AdminHandler::unregisterConfig(ExtensionConfig& config) {
  ASSERT(!config.adminId().empty());
  std::string admin_id(config.adminId());
  ASSERT(config_id_map_[admin_id].count(&config) == 1);
  config_id_map_[admin_id].erase(&config);
  if (config_id_map_[admin_id].empty()) {
    config_id_map_.erase(admin_id);
  }
}

PerTapSinkHandlePtr
AdminHandler::createPerTapSinkHandle(uint64_t trace_id,
                                     envoy::config::tap::v3::OutputSink::OutputSinkTypeCase type) {
  UNREFERENCED_PARAMETER(trace_id);
  using ProtoOutputSinkType = envoy::config::tap::v3::OutputSink::OutputSinkTypeCase;
  ASSERT(type == ProtoOutputSinkType::kStreamingAdmin ||
         type == ProtoOutputSinkType::kBufferedAdmin);

  /**
   * Switching on the sink type here again after doing so in TapConfigBaseImpl constructor
   * seems a bit strange. A possible refactor in the future could involve moving all Sinks
   * to live where the FilePerTapSink lives, and passing in the sink to use into the AdminHandler.
   */

  // Select the sink implementation to use based on type specified in YAML request body
  if (type == ProtoOutputSinkType::kStreamingAdmin) {
    return std::make_unique<AdminPerTapSinkHandle>(*this);
  }
  return std::make_unique<BufferedPerTapSinkHandle>(*this);
}

void AdminHandler::TraceBuffer::bufferTrace(
    const std::shared_ptr<envoy::data::tap::v3::TraceWrapper>& trace) {
  // Ignore traces once the buffer is full or flushed
  if (flushed() || full()) {
    return;
  }

  buffer_->emplace_back(std::move(*trace));
}

void AdminHandler::AdminPerTapSinkHandle::submitTrace(TraceWrapperPtr&& trace,
                                                      envoy::config::tap::v3::OutputSink::Format) {
  ENVOY_LOG(debug, "admin submitting buffered trace to main thread");
  // Convert to a shared_ptr, so we can send it to the main thread.
  std::shared_ptr<envoy::data::tap::v3::TraceWrapper> shared_trace{std::move(trace)};
  // Non owning pointer to the attached request, does not preserve lifetime unless in use.
  std::weak_ptr<AttachedRequest> weak_attached_request(parent_.attached_request_);

  // The handle can be destroyed before the cross thread post is complete. Thus, we capture a
  // reference to our parent.
  parent_.main_thread_dispatcher_.post([weak_attached_request, shared_trace] {
    // Take temporary ownership - extend lifetime
    std::shared_ptr<AttachedRequest> attached_request = weak_attached_request.lock();
    if (!attached_request) {
      // NOTE: Cannot do much here in response to the failed post as an HTTP response code has
      // already been sent on completion of AdminHandler::handler.
      ENVOY_LOG(debug, "attached request does not exist, not streaming trace");
      return; // No attached request, flushed already
    }

    attached_request->streamMsg(*shared_trace, false);
  });
}

void AdminHandler::BufferedPerTapSinkHandle::submitTrace(
    TraceWrapperPtr&& trace, envoy::config::tap::v3::OutputSink::Format) {
  // Convert to a shared_ptr to extend lifetime so we can send it to the main thread.
  std::shared_ptr<envoy::data::tap::v3::TraceWrapper> shared_trace(std::move(trace));
  // Non owning pointer to the attached request, does not preserve lifetime unless in use
  std::weak_ptr<AttachedRequest> weak_attached_request(parent_.attached_request_);

  parent_.main_thread_dispatcher_.post([shared_trace, weak_attached_request] {
    // Take temporary ownership - extend lifetime
    std::shared_ptr<AttachedRequest> attached_request = weak_attached_request.lock();
    if (!attached_request) {
      // NOTE: Cannot do much here in response to the failed post as an HTTP response code has
      // already been sent on completion of AdminHandler::handler. Additionally we probably don't
      // want to take any action in this event as this case may be hit in "normal" usage, depending
      // on when the destruction of attached_request_ occurs.
      ENVOY_LOG(debug, "attached request does not exist, not buffering trace");
      return; // No attached request, flushed already.
    }
    TraceBuffer* trace_buffer = attached_request->traceBuffer();

    // Check if we already responded to the client
    // Hit when posts to buffer traces are on the dispatcher queue and the buffer is flushed
    if (trace_buffer->flushed()) {
      return;
    }
    // Main thread dispatcher serializes access to the trace_buffer
    trace_buffer->bufferTrace(shared_trace);
    // If the trace buffer is not full yet, wait to buffer more traces
    if (!trace_buffer->full()) {
      return;
    }

    std::vector<envoy::data::tap::v3::TraceWrapper> buffer = trace_buffer->flush();

    ENVOY_LOG(debug, "admin writing buffered trace list to response");

    // Serialize writes to the stream
    for (size_t i = 0; i < buffer.size(); i++) {
      // Close stream on final message
      attached_request->streamMsg(buffer[i], (i + 1) == buffer.size());
    }
  });
}

AdminHandler::AttachedRequest::AttachedRequest(AdminHandler* admin_handler,
                                               const envoy::admin::v3::TapRequest& tap_request,
                                               Server::AdminStream* admin_stream)
    : config_id_(tap_request.config_id()), config_(tap_request.tap_config()),
      admin_stream_(admin_stream), main_thread_dispatcher_(admin_handler->main_thread_dispatcher_) {
}

AdminHandler::AttachedRequestBuffered::AttachedRequestBuffered(
    AdminHandler* admin_handler, const envoy::admin::v3::TapRequest& tap_request,
    Server::AdminStream* admin_stream)
    : AttachedRequest(admin_handler, tap_request, admin_stream) {
  const envoy::config::tap::v3::OutputSink& sink =
      tap_request.tap_config().output_config().sinks(0);

  const uint64_t max_buffered_traces = sink.buffered_admin().max_traces();
  const uint64_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(sink.buffered_admin(), timeout, 0);
  trace_buffer_ = std::make_unique<TraceBuffer>(max_buffered_traces);
  // Start the countdown if provided an actual timeout
  if (timeout_ms > 0) {
    timer_ = dispatcher().createTimer(
        [this, admin_handler] { this->onTimeout(admin_handler->attached_request_); });
    timer_->enableTimer(std::chrono::milliseconds(timeout_ms));
  }
}

void AdminHandler::AttachedRequestBuffered::onTimeout(
    const std::weak_ptr<AttachedRequest>& weak_attached_request) {
  // Flush the buffer regardless of size
  dispatcher().post([weak_attached_request] {
    // Take temporary ownership - extend lifetime
    std::shared_ptr<AttachedRequest> attached_request = weak_attached_request.lock();
    if (!attached_request) {
      // NOTE: Cannot do much here in response to the failed post as an HTTP response code has
      // already been sent on completion of AdminHandler::handler. Additionally we probably don't
      // want to take any action in this event as this case may be hit in "normal" usage, depending
      // on when the destruction of attached_request_ occurs.
      ENVOY_LOG(debug, "Timer Expiry after admin tap request completion");
      return; // No attached request, flushed already.
    }
    TraceBuffer* trace_buffer = attached_request->traceBuffer();

    // if the trace buffer has already been flushed short circuit.
    // Hit when this timeout callback is on the dispatcher queue and the buffer is flushed
    if (trace_buffer->flushed()) {
      return;
    }

    std::vector<envoy::data::tap::v3::TraceWrapper> buffer = trace_buffer->flush();

    ENVOY_LOG(debug, "Timer Expiry, admin flushing buffered traces to response");

    // Serialize writes to the stream
    for (const auto& trace : buffer) {
      attached_request->streamMsg(trace);
    }
    attached_request->endStream();
  });
}

void AdminHandler::AttachedRequest::endStream() {
  Buffer::OwnedImpl output_buffer;
  stream()->getDecoderFilterCallbacks().encodeData(output_buffer, true);
}

void AdminHandler::AttachedRequest::streamMsg(const Protobuf::Message& message, bool end_stream) {
  std::string output_string;

  switch (format()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING:
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES:
    output_string = MessageUtil::getJsonStringFromMessageOrError(message, true, true);
    break;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED: {
    Protobuf::io::StringOutputStream stream(&output_string);
    Protobuf::io::CodedOutputStream coded_stream(&stream);
    coded_stream.WriteVarint64(message.ByteSizeLong());
    message.SerializeWithCachedSizes(&coded_stream);
    break;
  }
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY:
  case envoy::config::tap::v3::OutputSink::PROTO_TEXT:
    PANIC("not implemented");
  }

  Buffer::OwnedImpl output_buffer{output_string};
  stream()->getDecoderFilterCallbacks().encodeData(output_buffer, end_stream);
}

std::shared_ptr<AdminHandler::AttachedRequest> AdminHandler::AttachedRequest::createAttachedRequest(
    AdminHandler* admin_handler, const envoy::admin::v3::TapRequest& tap_request,
    Server::AdminStream* admin_stream) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;

  const ProtoOutputSink& sink = tap_request.tap_config().output_config().sinks(0);

  switch (sink.output_sink_type_case()) {
  case ProtoOutputSink::kBufferedAdmin:
    return std::make_shared<AttachedRequestBuffered>(admin_handler, tap_request, admin_stream);
  default:
    return std::make_shared<AttachedRequest>(admin_handler, tap_request, admin_stream);
  }
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
