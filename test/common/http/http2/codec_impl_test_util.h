#pragma once

#include "envoy/http/codec.h"

#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"

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

protected:
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
  std::unordered_map<int32_t, uint32_t> settings_;
};

class TestServerConnectionImpl : public TestCodecStatsProvider,
                                 public ServerConnectionImpl,
                                 public TestCodecSettingsProvider {
public:
  TestServerConnectionImpl(
      Network::Connection& connection, ServerConnectionCallbacks& callbacks, Stats::Scope& scope,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action)
      : TestCodecStatsProvider(scope),
        ServerConnectionImpl(connection, callbacks, http2CodecStats(), http2_options,
                             max_request_headers_kb, max_request_headers_count,
                             headers_with_underscores_action) {}
  nghttp2_session* session() { return session_; }
  using ServerConnectionImpl::getStream;

protected:
  // Overrides ServerConnectionImpl::onSettingsForTest().
  void onSettingsForTest(const nghttp2_settings& settings) override { onSettingsFrame(settings); }
};

class TestClientConnectionImpl : public TestCodecStatsProvider,
                                 public ClientConnectionImpl,
                                 public TestCodecSettingsProvider {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope,
                           const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                           uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
                           Nghttp2SessionFactory& http2_session_factory)
      : TestCodecStatsProvider(scope),
        ClientConnectionImpl(connection, callbacks, http2CodecStats(), http2_options,
                             max_request_headers_kb, max_request_headers_count,
                             http2_session_factory) {}

  nghttp2_session* session() { return session_; }

  // Submits an H/2 METADATA frame to the peer.
  // Returns true on success, false otherwise.
  virtual bool submitMetadata(const MetadataMapVector& mm_vector, int32_t stream_id) {
    UNREFERENCED_PARAMETER(mm_vector);
    UNREFERENCED_PARAMETER(stream_id);
    return false;
  }

  using ClientConnectionImpl::getStream;
  using ConnectionImpl::sendPendingFrames;

protected:
  // Overrides ClientConnectionImpl::onSettingsForTest().
  void onSettingsForTest(const nghttp2_settings& settings) override { onSettingsFrame(settings); }
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
