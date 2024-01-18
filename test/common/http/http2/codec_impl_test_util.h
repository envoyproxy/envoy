#pragma once

#include "envoy/http/codec.h"

#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/utility.h"

#include "test/mocks/common.h"
#include "test/mocks/server/overload_manager.h"

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
  void onSettingsFrame(absl::Span<const http2::adapter::Http2Setting> settings) {
    for (const auto& [id, value] : settings) {
      auto result = settings_.insert(std::make_pair(id, value));
      // It is possible to have duplicate settings parameters, each new parameter replaces any
      // existing value.
      // https://tools.ietf.org/html/rfc7540#section-6.5
      if (!result.second) {
        ENVOY_LOG_MISC(debug, "Duplicated settings parameter {} with value {}", id, value);
        settings_.erase(result.first);
        // Guaranteed success here.
        settings_.insert(std::make_pair(id, value));
      }
    }
  }

private:
  absl::node_hash_map<int32_t, uint32_t> settings_;
};

class TestCodecOverloadManagerProvider {
public:
  TestCodecOverloadManagerProvider() {
    ON_CALL(overload_manager_, getLoadShedPoint(testing::_))
        .WillByDefault(testing::Return(&server_go_away_on_dispatch));
  }

  testing::NiceMock<Server::MockOverloadManager> overload_manager_;
  testing::NiceMock<Server::MockLoadShedPoint> server_go_away_on_dispatch;
};

class TestServerConnectionImpl : public TestCodecStatsProvider,
                                 public TestCodecSettingsProvider,
                                 public TestCodecOverloadManagerProvider,
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
                             headers_with_underscores_action, overload_manager_) {}

  http2::adapter::Http2Adapter* adapter() { return adapter_.get(); }
  using ServerConnectionImpl::getStream;
  using ServerConnectionImpl::sendPendingFrames;

protected:
  // Overrides ServerConnectionImpl::onSettings().
  void onSettings(absl::Span<const http2::adapter::Http2Setting> settings) override {
    onSettingsFrame(settings);
  }

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

  bool useOghttp2Library() const { return use_oghttp2_library_; }

protected:
  // Overrides ClientConnectionImpl::onSettings().
  void onSettings(absl::Span<const http2::adapter::Http2Setting> settings) override {
    onSettingsFrame(settings);
  }
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
