#pragma once

#include "envoy/http/codec.h"

#include "common/http/http2/codec_impl.h"
#include "common/http/http2/codec_impl_legacy.h"
#include "common/http/utility.h"

#include "test/mocks/common.h"

namespace Envoy {
namespace Http {
namespace Http2 {

class TestCodecStatsProvider {
public:
  TestCodecStatsProvider(Stats::Scope& scope) : scope_(scope) {}

  Http::Http2::CodecStats& http2CodecStats() {
    return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, scope_);
  }

  Stats::Scope& scope_;
  Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
};

class TestCodecSettingsProvider {
public:
  // Returns the value of the SETTINGS parameter keyed by |identifier| sent by the remote endpoint.
  absl::optional<uint32_t> getRemoteSettingsParameterValue(int32_t identifier) const {
    const auto it = settings_.find(identifier);
    if (it == settings_.end()) {
      return absl::nullopt;
    }
    return it->second;
  }

  // protected:
  // Stores SETTINGS parameters contained in |settings_frame| to make them available via
  // getRemoteSettingsParameterValue().
  void onSettingsFrame(const nghttp2_settings& settings_frame) {
    for (uint32_t i = 0; i < settings_frame.niv; ++i) {
      auto result = settings_.insert(
          std::make_pair(settings_frame.iv[i].settings_id, settings_frame.iv[i].value));
      // It is possible to have duplicate settings parameters, each new parameter replaces any
      // existing value.
      // https://tools.ietf.org/html/rfc7540#section-6.5
      if (!result.second) {
        ENVOY_LOG_MISC(debug, "Duplicated settings parameter {} with value {}",
                       settings_frame.iv[i].settings_id, settings_frame.iv[i].value);
        settings_.erase(result.first);
        // Guaranteed success here.
        settings_.insert(
            std::make_pair(settings_frame.iv[i].settings_id, settings_frame.iv[i].value));
      }
    }
  }

private:
  absl::node_hash_map<int32_t, uint32_t> settings_;
};

struct ServerCodecFacade : public virtual Connection {
  virtual nghttp2_session* session() PURE;
  virtual Http::Stream* getStream(int32_t stream_id) PURE;
  virtual uint32_t getStreamUnconsumedBytes(int32_t stream_id) PURE;
  virtual void setStreamWriteBufferWatermarks(int32_t stream_id, uint32_t low_watermark,
                                              uint32_t high_watermark) PURE;
};

class TestServerConnection : public TestCodecStatsProvider,
                             public TestCodecSettingsProvider,
                             public ServerCodecFacade {
public:
  TestServerConnection(Stats::Scope& scope) : TestCodecStatsProvider(scope) {}
};

template <typename CodecImplType>
class TestServerConnectionImpl : public TestServerConnection, public CodecImplType {
public:
  TestServerConnectionImpl(
      Network::Connection& connection, ServerConnectionCallbacks& callbacks, Stats::Scope& scope,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action)
      : TestServerConnection(scope),
        CodecImplType(connection, callbacks, http2CodecStats(), random_, http2_options,
                      max_request_headers_kb, max_request_headers_count,
                      headers_with_underscores_action) {}

  // ServerCodecFacade
  nghttp2_session* session() override { return CodecImplType::session_; }
  Http::Stream* getStream(int32_t stream_id) override {
    return CodecImplType::getStream(stream_id);
  }
  uint32_t getStreamUnconsumedBytes(int32_t stream_id) override {
    return CodecImplType::getStream(stream_id)->unconsumed_bytes_;
  }
  void setStreamWriteBufferWatermarks(int32_t stream_id, uint32_t low_watermark,
                                      uint32_t high_watermark) override {
    CodecImplType::getStream(stream_id)->setWriteBufferWatermarks(low_watermark, high_watermark);
  }

protected:
  // Overrides ServerConnectionImpl::onSettingsForTest().
  void onSettingsForTest(const nghttp2_settings& settings) override { onSettingsFrame(settings); }

  testing::NiceMock<Random::MockRandomGenerator> random_;
};

using TestServerConnectionImplLegacy =
    TestServerConnectionImpl<Envoy::Http::Legacy::Http2::ServerConnectionImpl>;
using TestServerConnectionImplNew =
    TestServerConnectionImpl<Envoy::Http::Http2::ServerConnectionImpl>;

struct ClientCodecFacade : public ClientConnection {
  virtual nghttp2_session* session() PURE;
  virtual Http::Stream* getStream(int32_t stream_id) PURE;
  virtual uint64_t getStreamPendingSendDataLength(int32_t stream_id) PURE;
  virtual Status sendPendingFrames() PURE;
  virtual bool submitMetadata(const MetadataMapVector& mm_vector, int32_t stream_id) PURE;
};

class TestClientConnection : public TestCodecStatsProvider,
                             public TestCodecSettingsProvider,
                             public ClientCodecFacade {
public:
  TestClientConnection(Stats::Scope& scope) : TestCodecStatsProvider(scope) {}

  testing::NiceMock<Random::MockRandomGenerator> random_generator_;
};

template <typename CodecImplType>
class TestClientConnectionImpl : public TestClientConnection, public CodecImplType {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope,
                           const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                           uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
                           typename CodecImplType::SessionFactory& http2_session_factory)
      : TestClientConnection(scope),
        CodecImplType(connection, callbacks, http2CodecStats(), random_generator_, http2_options,
                      max_request_headers_kb, max_request_headers_count, http2_session_factory) {}

  // ClientCodecFacade
  RequestEncoder& newStream(ResponseDecoder& response_decoder) override {
    return CodecImplType::newStream(response_decoder);
  }
  nghttp2_session* session() override { return CodecImplType::session_; }
  Http::Stream* getStream(int32_t stream_id) override {
    return CodecImplType::getStream(stream_id);
  }
  uint64_t getStreamPendingSendDataLength(int32_t stream_id) override {
    return CodecImplType::getStream(stream_id)->pending_send_data_.length();
  }
  Status sendPendingFrames() override;
  // Submits an H/2 METADATA frame to the peer.
  // Returns true on success, false otherwise.
  bool submitMetadata(const MetadataMapVector& mm_vector, int32_t stream_id) override {
    UNREFERENCED_PARAMETER(mm_vector);
    UNREFERENCED_PARAMETER(stream_id);
    return false;
  }

protected:
  // Overrides ClientConnectionImpl::onSettingsForTest().
  void onSettingsForTest(const nghttp2_settings& settings) override { onSettingsFrame(settings); }
};

template <typename CodecImplType>
Status TestClientConnectionImpl<CodecImplType>::sendPendingFrames() {
  return CodecImplType::sendPendingFrames();
}

template <>
Status
TestClientConnectionImpl<Envoy::Http::Legacy::Http2::ClientConnectionImpl>::sendPendingFrames() {
  Envoy::Http::Legacy::Http2::ClientConnectionImpl::sendPendingFrames();
  return okStatus();
}

using TestClientConnectionImplLegacy =
    TestClientConnectionImpl<Envoy::Http::Legacy::Http2::ClientConnectionImpl>;
using TestClientConnectionImplNew =
    TestClientConnectionImpl<Envoy::Http::Http2::ClientConnectionImpl>;

using ProdNghttp2SessionFactoryLegacy = Envoy::Http::Legacy::Http2::ProdNghttp2SessionFactory;
using ProdNghttp2SessionFactoryNew = Envoy::Http::Http2::ProdNghttp2SessionFactory;

} // namespace Http2
} // namespace Http
} // namespace Envoy
