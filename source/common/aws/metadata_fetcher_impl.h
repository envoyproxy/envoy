#pragma once

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/aws/metadata_fetcher.h"
#include "common/common/logger.h"
#include "common/network/filter_impl.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class MetadataFetcherImpl : public MetadataFetcher, Logger::Loggable<Logger::Id::aws> {
public:
  // Factory abstraction used for creating test http codecs
  typedef std::function<Http::ClientConnection*(Network::Connection&, Http::ConnectionCallbacks&)>
      HttpCodecFactory;

  class MetadataSession;
  typedef std::function<MetadataSession*(Network::ClientConnectionPtr&&, Event::Dispatcher&,
                                         Http::StreamDecoder&, Http::StreamCallbacks&,
                                         const Http::HeaderMap&, HttpCodecFactory)>
      MetadataSessionFactory;

  class StringBufferDecoder;
  typedef std::function<StringBufferDecoder*()> StringBufferDecoderFactory;

  MetadataFetcherImpl(MetadataSessionFactory session_factory = createSession,
                      StringBufferDecoderFactory decoder_factory = createDecoder,
                      HttpCodecFactory codec_factory = createCodec)
      : session_factory_(session_factory), decoder_factory_(decoder_factory),
        codec_factory_(codec_factory) {}

  absl::optional<std::string> getMetadata(
      Event::Dispatcher& dispatcher, const std::string& host, const std::string& path,
      const absl::optional<std::string>& auth_token = absl::optional<std::string>()) const override;

  class MetadataSession : public Network::ConnectionCallbacks, public Http::ConnectionCallbacks {
  public:
    MetadataSession(Network::ClientConnectionPtr&& connection, Event::Dispatcher& dispatcher,
                    Http::StreamDecoder& decoder, Http::StreamCallbacks& callbacks,
                    const Http::HeaderMap& headers, HttpCodecFactory codec_factory);

    virtual ~MetadataSession() = default;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Http::ConnectionCallbacks
    void onGoAway() override {}

    void onData(Buffer::Instance& data);

    virtual void close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

  protected:
    MetadataSession(){};

  private:
    Network::ClientConnectionPtr connection_{};
    Http::ClientConnectionPtr codec_{};
    Event::TimerPtr timeout_timer_{};
    Http::StreamEncoder* encoder_{};
    bool connected_{};
  };

  class StringBufferDecoder : public Http::StreamDecoder, public Http::StreamCallbacks {
  public:
    virtual ~StringBufferDecoder() = default;

    // Http::StreamDecoder
    void decode100ContinueHeaders(Http::HeaderMapPtr&&) override {}
    void decodeHeaders(Http::HeaderMapPtr&&, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::HeaderMapPtr&&) override {}
    void decodeMetadata(Http::MetadataMapPtr&&) override {}

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason) override { complete(); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    virtual const std::string& body() const { return body_; }

    void setCompleteCallback(std::function<void()> complete_cb) { complete_cb_ = complete_cb; }

  private:
    friend class MetadataFetcherImplTest;

    void complete() {
      if (complete_cb_) {
        complete_cb_();
      }
    }

    std::string body_;
    std::function<void()> complete_cb_{};
  };

  class CodecReadFilter : public Network::ReadFilterBaseImpl {
  public:
    CodecReadFilter(MetadataSession& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

  private:
    MetadataSession& parent_;
  };

private:
  friend class MetadataFetcherImplTest;

  static Http::ClientConnection* createCodec(Network::Connection& connection,
                                             Http::ConnectionCallbacks& callbacks);

  static MetadataSession* createSession(Network::ClientConnectionPtr&& connection,
                                        Event::Dispatcher& dispatcher, Http::StreamDecoder& decoder,
                                        Http::StreamCallbacks& callbacks,
                                        const Http::HeaderMap& headers,
                                        HttpCodecFactory codec_factory) {
    return new MetadataSession(std::move(connection), dispatcher, decoder, callbacks, headers,
                               codec_factory);
  }

  static StringBufferDecoder* createDecoder() { return new StringBufferDecoder(); }

  static const size_t MAX_RETRIES;
  static const std::chrono::milliseconds NO_DELAY;
  static const std::chrono::milliseconds RETRY_DELAY;
  static const std::chrono::milliseconds TIMEOUT;

  MetadataSessionFactory session_factory_;
  StringBufferDecoderFactory decoder_factory_;
  HttpCodecFactory codec_factory_;
};

} // namespace Auth
} // namespace Aws
} // namespace Envoy