#pragma once

#include "envoy/http/codec.h"

#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/utility.h"

#include "test/mocks/common.h"

#include "quiche/http2/adapter/http2_adapter.h"

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
  absl::node_hash_map<int32_t, uint32_t> settings_;
};

class TestServerConnectionImpl : public TestCodecStatsProvider,
                                 public TestCodecSettingsProvider,
                                 public ServerConnectionImpl {
public:
  TestServerConnectionImpl(
      Network::Connection& connection, ServerConnectionCallbacks& callbacks, Stats::Scope& scope,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      Random::RandomGenerator& random, uint32_t max_request_headers_kb,
      uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action)
      : TestCodecStatsProvider(scope),
        ServerConnectionImpl(connection, callbacks, http2CodecStats(), random, http2_options,
                             max_request_headers_kb, max_request_headers_count,
                             headers_with_underscores_action) {}

  http2::adapter::Http2Adapter* adapter() { return adapter_.get(); }
  using ServerConnectionImpl::getStream;
  using ServerConnectionImpl::sendPendingFrames;

protected:
  // Overrides ServerConnectionImpl::onSettings().
  void onSettings(const nghttp2_settings& settings) override { onSettingsFrame(settings); }

  testing::NiceMock<Random::MockRandomGenerator> random_;
};

class TestClientConnectionImpl : public TestCodecStatsProvider,
                                 public TestCodecSettingsProvider,
                                 public ClientConnectionImpl {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope,
                           const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                           Random::RandomGenerator& random, uint32_t max_request_headers_kb,
                           uint32_t max_request_headers_count,
                           Http2SessionFactory& http2_session_factory)
      : TestCodecStatsProvider(scope),
        ClientConnectionImpl(connection, callbacks, http2CodecStats(), random, http2_options,
                             max_request_headers_kb, max_request_headers_count,
                             http2_session_factory) {}

  http2::adapter::Http2Adapter* adapter() { return adapter_.get(); }
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
  // Overrides ClientConnectionImpl::onSettings().
  void onSettings(const nghttp2_settings& settings) override { onSettingsFrame(settings); }
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
