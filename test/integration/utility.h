#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/utility.h"
#include "source/common/http/codec_client.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
/**
 * A buffering response decoder used for testing.
 */
class BufferingStreamDecoder : public Http::ResponseDecoder, public Http::StreamCallbacks {
public:
  BufferingStreamDecoder(std::function<void()> on_complete_cb) : on_complete_cb_(on_complete_cb) {}

  bool complete() { return complete_; }
  const Http::ResponseHeaderMap& headers() { return *headers_; }
  const std::string& body() { return body_; }

  // Http::StreamDecoder
  void decodeData(Buffer::Instance&, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&&) override {}

  // Http::ResponseDecoder
  void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void dumpState(std::ostream& os, int indent_level) const override {
    DUMP_STATE_UNIMPLEMENTED(BufferingStreamDecoder);
  }

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void onComplete();

  Http::ResponseHeaderMapPtr headers_;
  std::string body_;
  bool complete_{};
  std::function<void()> on_complete_cb_;
};

using BufferingStreamDecoderPtr = std::unique_ptr<BufferingStreamDecoder>;

/**
 * Basic driver for a raw connection.
 */
class RawConnectionDriver {
public:
  // Callback that is executed to write data to connection. The provided buffer
  // should be populated with the data to write. If the callback returns true,
  // the connection will be closed after writing.
  using DoWriteCallback = std::function<bool(Buffer::Instance&)>;
  using ReadCallback = std::function<void(Network::ClientConnection&, const Buffer::Instance&)>;

  RawConnectionDriver(uint32_t port, DoWriteCallback write_request_callback,
                      ReadCallback response_data_callback, Network::Address::IpVersion version,
                      Event::Dispatcher& dispatcher,
                      Network::TransportSocketPtr transport_socket = nullptr);
  // Similar to the constructor above but accepts the request as a constructor argument.
  RawConnectionDriver(uint32_t port, Buffer::Instance& request_data,
                      ReadCallback response_data_callback, Network::Address::IpVersion version,
                      Event::Dispatcher& dispatcher,
                      Network::TransportSocketPtr transport_socket = nullptr);
  ~RawConnectionDriver();

  testing::AssertionResult
  run(Event::Dispatcher::RunType run_type = Event::Dispatcher::RunType::Block,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  void close();
  Network::ConnectionEvent lastConnectionEvent() const {
    return callbacks_->last_connection_event_;
  }
  // Wait until connected or closed().
  ABSL_MUST_USE_RESULT testing::AssertionResult waitForConnection();

  bool closed() { return callbacks_->closed(); }
  bool allBytesSent() const;

private:
  struct ForwardingFilter : public Network::ReadFilterBaseImpl {
    ForwardingFilter(RawConnectionDriver& parent, ReadCallback cb)
        : parent_(parent), response_data_callback_(cb) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      response_data_callback_(*parent_.client_, data);
      data.drain(data.length());
      return Network::FilterStatus::StopIteration;
    }

    RawConnectionDriver& parent_;
    ReadCallback response_data_callback_;
  };

  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    using WriteCb = std::function<void()>;

    ConnectionCallbacks(WriteCb write_cb, Event::Dispatcher& dispatcher)
        : write_cb_(write_cb), dispatcher_(dispatcher) {}
    bool connected() const { return connected_; }
    bool closed() const { return closed_; }

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      if (!connected_ && event == Network::ConnectionEvent::Connected) {
        write_cb_();
      }
      last_connection_event_ = event;

      closed_ |= (event == Network::ConnectionEvent::RemoteClose ||
                  event == Network::ConnectionEvent::LocalClose);
      connected_ |= (event == Network::ConnectionEvent::Connected);

      if (closed_) {
        dispatcher_.exit();
      }
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override { write_cb_(); }

    Network::ConnectionEvent last_connection_event_;

  private:
    WriteCb write_cb_;
    Event::Dispatcher& dispatcher_;
    bool connected_{false};
    bool closed_{false};
  };

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<ConnectionCallbacks> callbacks_;
  Network::ClientConnectionPtr client_;
  uint64_t remaining_bytes_to_send_;
};

/**
 * Utility routines for integration tests.
 */
class IntegrationUtil {
public:
  /**
   * Make a new connection, issues a request, and then disconnect when the request is complete.
   * @param addr supplies the address to connect to.
   * @param method supplies the request method.
   * @param url supplies the request url.
   * @param body supplies the optional request body to send.
   * @param type supplies the codec to use for the request.
   * @param host supplies the host header to use for the request.
   * @param content_type supplies the content-type header to use for the request, if any.
   * @return BufferingStreamDecoderPtr the complete request or a partial request if there was
   *         remote early disconnection.
   */
  static BufferingStreamDecoderPtr
  makeSingleRequest(const Network::Address::InstanceConstSharedPtr& addr, const std::string& method,
                    const std::string& url, const std::string& body, Http::CodecType type,
                    const std::string& host = "host", const std::string& content_type = "");

  /**
   * Make a new connection, issues a request, and then disconnect when the request is complete.
   * @param port supplies the port to connect to on localhost.
   * @param method supplies the request method.
   * @param url supplies the request url.
   * @param body supplies the optional request body to send.
   * @param type supplies the codec to use for the request.
   * @param version the IP address version of the client and server.
   * @param host supplies the host header to use for the request.
   * @param content_type supplies the content-type header to use for the request, if any.
   * @return BufferingStreamDecoderPtr the complete request or a partial request if there was
   *         remote early disconnection.
   */
  static BufferingStreamDecoderPtr makeSingleRequest(uint32_t port, const std::string& method,
                                                     const std::string& url,
                                                     const std::string& body, Http::CodecType type,
                                                     Network::Address::IpVersion ip_version,
                                                     const std::string& host = "host",
                                                     const std::string& content_type = "");

  /**
   * Create transport socket factory for Quic upstream transport socket.
   * @return TransportSocketFactoryPtr the client transport socket factory.
   */
  static Network::UpstreamTransportSocketFactoryPtr createQuicUpstreamTransportSocketFactory(
      Api::Api& api, Stats::Store& store, Ssl::ContextManager& context_manager,
      ThreadLocal::Instance& threadlocal, const std::string& san_to_match,
      // Allow configuring TLS to talk to upstreams instead of Envoy
      bool connect_to_fake_upstreams = false);

  static Http::HeaderValidatorFactoryPtr makeHeaderValidationFactory(
      const ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config);
};

// A set of connection callbacks which tracks connection state.
class ConnectionStatusCallbacks : public Network::ConnectionCallbacks {
public:
  bool connected() const { return connected_; }
  bool closed() const { return closed_; }
  void reset() {
    connected_ = false;
    closed_ = false;
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    closed_ |= (event == Network::ConnectionEvent::RemoteClose ||
                event == Network::ConnectionEvent::LocalClose);
    connected_ |= (event == Network::ConnectionEvent::Connected);
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  bool connected_{false};
  bool closed_{false};
};

// A read filter which waits for a given data then stops the dispatcher loop.
class WaitForPayloadReader : public Network::ReadFilterBaseImpl {
public:
  WaitForPayloadReader(Event::Dispatcher& dispatcher);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  void setDataToWaitFor(const std::string& data, bool exact_match = true) {
    data_to_wait_for_ = data;
    exact_match_ = exact_match;
  }

  ABSL_MUST_USE_RESULT testing::AssertionResult waitForLength(size_t length,
                                                              std::chrono::milliseconds timeout) {
    ASSERT(!wait_for_length_);
    length_to_wait_for_ = length;
    wait_for_length_ = true;

    Event::TimerPtr timeout_timer =
        dispatcher_.createTimer([this]() -> void { dispatcher_.exit(); });
    timeout_timer->enableTimer(timeout);

    dispatcher_.run(Event::Dispatcher::RunType::Block);

    if (timeout_timer->enabled()) {
      timeout_timer->disableTimer();
      return testing::AssertionSuccess();
    }

    length_to_wait_for_ = 0;
    wait_for_length_ = false;
    return testing::AssertionFailure() << "Timed out waiting for " << length << " bytes of data\n";
  }

  const std::string& data() { return data_; }
  bool readLastByte() { return read_end_stream_; }
  void clearData(size_t count = std::string::npos) { data_.erase(0, count); }

private:
  Event::Dispatcher& dispatcher_;
  std::string data_to_wait_for_;
  std::string data_;
  bool exact_match_{true};
  bool read_end_stream_{};
  size_t length_to_wait_for_{0};
  bool wait_for_length_{false};
};

} // namespace Envoy
