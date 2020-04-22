#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/codec_client.h"
#include "common/stats/isolated_store_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/test_time.h"

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
  void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&&) override {}
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;

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
  using ReadCallback = std::function<void(Network::ClientConnection&, const Buffer::Instance&)>;

  RawConnectionDriver(uint32_t port, Buffer::Instance& initial_data, ReadCallback data_callback,
                      Network::Address::IpVersion version);
  ~RawConnectionDriver();
  const Network::Connection& connection() { return *client_; }
  bool connecting() { return callbacks_->connecting_; }
  void run(Event::Dispatcher::RunType run_type = Event::Dispatcher::RunType::Block);
  void close();
  Network::ConnectionEvent last_connection_event() const {
    return callbacks_->last_connection_event_;
  }

private:
  struct ForwardingFilter : public Network::ReadFilterBaseImpl {
    ForwardingFilter(RawConnectionDriver& parent, ReadCallback cb)
        : parent_(parent), data_callback_(cb) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      data_callback_(*parent_.client_, data);
      data.drain(data.length());
      return Network::FilterStatus::StopIteration;
    }

    RawConnectionDriver& parent_;
    ReadCallback data_callback_;
  };

  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    void onEvent(Network::ConnectionEvent event) override {
      last_connection_event_ = event;
      connecting_ = false;
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    bool connecting_{true};
    Network::ConnectionEvent last_connection_event_;
  };

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<ConnectionCallbacks> callbacks_;
  Network::ClientConnectionPtr client_;
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
                    const std::string& url, const std::string& body, Http::CodecClient::Type type,
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
  static BufferingStreamDecoderPtr
  makeSingleRequest(uint32_t port, const std::string& method, const std::string& url,
                    const std::string& body, Http::CodecClient::Type type,
                    Network::Address::IpVersion ip_version, const std::string& host = "host",
                    const std::string& content_type = "");
};

// A set of connection callbacks which tracks connection state.
class ConnectionStatusCallbacks : public Network::ConnectionCallbacks {
public:
  bool connected() const { return connected_; }
  bool closed() const { return closed_; }

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

  void set_data_to_wait_for(const std::string& data, bool exact_match = true) {
    data_to_wait_for_ = data;
    exact_match_ = exact_match;
  }
  void setLengthToWaitFor(size_t length) {
    ASSERT(!wait_for_length_);
    length_to_wait_for_ = length;
    wait_for_length_ = true;
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
