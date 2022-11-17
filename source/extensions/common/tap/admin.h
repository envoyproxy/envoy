#pragma once

#include <vector>

#include "envoy/server/admin.h"
#include "envoy/singleton/manager.h"

#include "source/extensions/common/tap/tap.h"

#include "absl/container/node_hash_set.h"
#include "absl/types/optional.h"

namespace envoy {
namespace admin {
namespace v3 {
class TapRequest;
}
} // namespace admin
} // namespace envoy

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

class AdminHandler;
using AdminHandlerSharedPtr = std::shared_ptr<AdminHandler>;

/**
 * Singleton /tap admin handler for admin management of tap configurations and output. This
 * handler is not installed and active unless the tap configuration specifically configures it.
 * TODO(mattklein123): We should allow the admin handler to always be installed in read only mode
 *                     so it's easier to debug the active tap configuration.
 */
class AdminHandler : public Singleton::Instance,
                     public Extensions::Common::Tap::Sink,
                     Logger::Loggable<Logger::Id::tap> {
public:
  AdminHandler(OptRef<Server::Admin> admin, Event::Dispatcher& main_thread_dispatcher);
  ~AdminHandler() override;

  /**
   * Get the singleton admin handler. The handler will be created if it doesn't already exist,
   * otherwise the existing handler will be returned.
   */
  static AdminHandlerSharedPtr getSingleton(OptRef<Server::Admin> admin,
                                            Singleton::Manager& singleton_manager,
                                            Event::Dispatcher& main_thread_dispatcher);

  /**
   * Register a new extension config to the handler so that it can be admin managed.
   * @param config supplies the config to register.
   * @param config_id supplies the ID to use for managing the configuration. Multiple extensions
   *        can use the same ID so they can be managed in aggregate (e.g., an HTTP filter on
   *        many listeners).
   */
  void registerConfig(ExtensionConfig& config, const std::string& config_id);

  /**
   * Unregister an extension config from the handler.
   * @param config supplies the previously registered config.
   */
  void unregisterConfig(ExtensionConfig& config);

  // Extensions::Common::Tap::Sink
  PerTapSinkHandlePtr
  createPerTapSinkHandle(uint64_t trace_id,
                         envoy::config::tap::v3::OutputSink::OutputSinkTypeCase type) override;

private:
  /**
   * TraceBuffer internally buffers TraceWrappers in a vector. The size of the
   * internal buffer is determined on construction by max_buf_size. TraceBuffer will continue to
   * buffer traces via calls to bufferTrace until there is no remaining room in the buffer, or
   * traceList is called, transfering ownership of the buffered data.
   * Note that TraceBuffer is not threadsafe by itself - accesses to TraceBuffer need to be
   * serialized. Serialization is currently done by the main thread dispatcher.
   */
  class TraceBuffer {
    using TraceWrapper = envoy::data::tap::v3::TraceWrapper;

  public:
    TraceBuffer(uint64_t max_buf_size)
        : max_buf_size_(max_buf_size), buffer_(std::vector<TraceWrapper>()) {
      buffer_->reserve(max_buf_size);
    }

    // Buffers trace internally if there is space available
    // This function takes exclusive ownership of trace and may destroy the content of trace.
    // A unique_ptr is semantically more correct here, but a shared pointer is
    // needed for traces to be captured in lambda function (submitTrace).
    void bufferTrace(const std::shared_ptr<envoy::data::tap::v3::TraceWrapper>& trace);

    // Returns true if the trace buffer is full (reached max_buf_size_) false otherwise
    bool full() const {
      if (!buffer_) {
        return false;
      }
      return (buffer_->size() == max_buf_size_);
    }

    // Return true if the buffer has already been flushed, false otherwise.
    bool flushed() const { return !buffer_; }

    // Take ownership of the internally managed trace list
    std::vector<TraceWrapper> flush() {
      std::vector<TraceWrapper> buffer = std::move(*buffer_);
      buffer_.reset(); // set optional to empty
      return buffer;
    }

  private:
    const size_t max_buf_size_; // Number of traces to buffer
    absl::optional<std::vector<TraceWrapper>> buffer_;
  };

  /**
   * This object's lifetime is tied to the lifetime of the admin_stream, and is responsible for
   * managing all data that has lifetime tied to the admin_stream.
   */
  class AttachedRequest {
  public:
    AttachedRequest(AdminHandler* admin_handler, const envoy::admin::v3::TapRequest& tap_request,
                    Server::AdminStream* admin_stream);
    virtual ~AttachedRequest() = default;
    // Stream the protobuf message to the admin_stream using the configured format
    // Requires the admin_stream to be open
    virtual void streamMsg(const Protobuf::Message& message, bool end_stream = false);

    // Explicitly close the admin_stream. Stream must be open.
    virtual void endStream();

    // Factory method for AttachedRequests - uses protobuf input to determine the subtype of
    // AttachedRequest to create
    static std::shared_ptr<AttachedRequest>
    createAttachedRequest(AdminHandler* admin_handler,
                          const envoy::admin::v3::TapRequest& tap_request,
                          Server::AdminStream* admin_stream);

    // --------- Accessors ---------
    // Get a pointer to the internal trace buffer. This method only applies for
    // the Buffered sink type, but exists in the generic API to avoid
    // dynamic casting of the AttachedRequest type elsewhere
    virtual TraceBuffer* traceBuffer() const { return nullptr; }
    const std::string& id() const { return config_id_; }
    const envoy::config::tap::v3::TapConfig& config() const { return config_; }
    envoy::config::tap::v3::OutputSink::Format format() const {
      return config_.output_config().sinks()[0].format();
    }

  protected:
    Event::Dispatcher& dispatcher() { return main_thread_dispatcher_; }
    const Server::AdminStream* stream() const { return admin_stream_; }

  private:
    const std::string config_id_;
    const envoy::config::tap::v3::TapConfig config_;
    const Server::AdminStream* admin_stream_;
    Event::Dispatcher& main_thread_dispatcher_;
    friend class BaseAdminHandlerTest;
  };

  /**
   * AttachedRequest with additional data specific to the Buffered Sink type
   */
  class AttachedRequestBuffered : public AttachedRequest {
    // Callback fired on timer expiry
    void onTimeout(const std::weak_ptr<AttachedRequest>& attached_request);

  public:
    TraceBuffer* traceBuffer() const override { return trace_buffer_.get(); }

    AttachedRequestBuffered(AdminHandler* admin_handler,
                            const envoy::admin::v3::TapRequest& tap_request,
                            Server::AdminStream* admin_stream);

  private:
    Event::TimerPtr timer_;
    // Pointer to buffered traces, only exists if the sink type requires buffering multiple traces
    std::unique_ptr<TraceBuffer> trace_buffer_;
    friend class BufferedAdminHandlerTest; // For testing Purposes
  };

  struct AdminPerTapSinkHandle : public PerTapSinkHandle {
    AdminPerTapSinkHandle(AdminHandler& parent) : parent_(parent) {}

    // Extensions::Common::Tap::PerTapSinkHandle
    void submitTrace(TraceWrapperPtr&& trace,
                     envoy::config::tap::v3::OutputSink::Format format) override;

    AdminHandler& parent_;
  };

  /**
   * Sink for buffering a variable number of traces in a TraceBuffer
   */
  struct BufferedPerTapSinkHandle : public PerTapSinkHandle {
    BufferedPerTapSinkHandle(AdminHandler& parent) : parent_(parent) {}

    // Extensions::Common::Tap::PerTapSinkHandle
    void submitTrace(TraceWrapperPtr&& trace,
                     envoy::config::tap::v3::OutputSink::Format format) override;

    AdminHandler& parent_;
  };

  Http::Code handler(Http::HeaderMap& response_headers, Buffer::Instance& response,
                     Server::AdminStream& admin_stream);
  Http::Code badRequest(Buffer::Instance& response, absl::string_view error);

  Server::Admin& admin_;
  Event::Dispatcher& main_thread_dispatcher_;
  absl::node_hash_map<std::string, absl::node_hash_set<ExtensionConfig*>> config_id_map_;
  std::shared_ptr<AttachedRequest> attached_request_;
  friend class BaseAdminHandlerTest;     // For testing purposes
  friend class BufferedAdminHandlerTest; // For testing Purposes
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
