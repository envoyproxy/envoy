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
#include "common/http/codec_client.h"

#include "test/test_common/printers.h"

namespace Envoy {
/**
 * A buffering response decoder used for testing.
 */
class BufferingStreamDecoder : public Http::StreamDecoder, public Http::StreamCallbacks {
public:
  BufferingStreamDecoder(std::function<void()> on_complete_cb) : on_complete_cb_(on_complete_cb) {}

  bool complete() { return complete_; }
  const Http::HeaderMap& headers() { return *headers_; }
  const std::string& body() { return body_; }

  // Http::StreamDecoder
  void decode100ContinueHeaders(Http::HeaderMapPtr&&) override {}
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance&, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void onComplete();

  Http::HeaderMapPtr headers_;
  std::string body_;
  bool complete_{};
  std::function<void()> on_complete_cb_;
};

typedef std::unique_ptr<BufferingStreamDecoder> BufferingStreamDecoderPtr;

/**
 * Basic driver for a raw connection.
 */
class RawConnectionDriver {
public:
  typedef std::function<void(Network::ClientConnection&, const Buffer::Instance&)> ReadCallback;

  RawConnectionDriver(uint32_t port, Buffer::Instance& initial_data, ReadCallback data_callback,
                      Network::Address::IpVersion version);
  ~RawConnectionDriver();
  void run();
  void close();

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

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
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
   *         remote easly disconnection.
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
   * @param version the IP addess version of the client and server.
   * @param host supplies the host header to use for the request.
   * @param content_type supplies the content-type header to use for the request, if any.
   * @return BufferingStreamDecoderPtr the complete request or a partial request if there was
   *         remote easly disconnection.
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
  const std::string& data() { return data_; }
  bool readLastByte() { return read_end_stream_; }

private:
  Event::Dispatcher& dispatcher_;
  std::string data_to_wait_for_;
  std::string data_;
  bool exact_match_{true};
  bool read_end_stream_{};
};

} // namespace Envoy
