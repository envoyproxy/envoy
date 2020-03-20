#pragma once

#include "envoy/http/codec.h"

#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {
namespace Http2 {

class TestCodecSettingsProvider {
public:
  // Returns the value of the SETTINGS parameter keyed by |identifier| sent by the remote endpoint.
  absl::optional<uint32_t> getRemoteSettingsParameterValue(int32_t identifier) const {
    const auto it = settings_.find({identifier, 0});
    if (it == settings_.end()) {
      return absl::nullopt;
    }
    return it->value;
  }

protected:
  // Stores SETTINGS parameters contained in |settings_frame| to make them available via
  // getRemoteSettingsParameterValue().
  void onSettingsFrame(const nghttp2_settings& settings_frame) {
    for (uint32_t i = 0; i < settings_frame.niv; ++i) {
      auto result = settings_.insert(settings_frame.iv[i]);
      ASSERT(result.second);
    }
  }

private:
  std::unordered_set<nghttp2_settings_entry, ::Envoy::Http2::Utility::SettingsEntryHash,
                     ::Envoy::Http2::Utility::SettingsEntryEquals>
      settings_;
};

class TestServerConnectionImpl : public ServerConnectionImpl, public TestCodecSettingsProvider {
public:
  TestServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                           Stats::Scope& scope,
                           const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                           uint32_t max_request_headers_kb, uint32_t max_request_headers_count)
      : ServerConnectionImpl(connection, callbacks, scope, http2_options, max_request_headers_kb,
                             max_request_headers_count) {}
  nghttp2_session* session() { return session_; }
  using ServerConnectionImpl::getStream;

protected:
  // Overrides ServerConnectionImpl::onSettingsForTest().
  void onSettingsForTest(const nghttp2_settings& settings) override { onSettingsFrame(settings); }
};

class TestClientConnectionImpl : public ClientConnectionImpl, public TestCodecSettingsProvider {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope,
                           const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                           uint32_t max_request_headers_kb, uint32_t max_request_headers_count)
      : ClientConnectionImpl(connection, callbacks, scope, http2_options, max_request_headers_kb,
                             max_request_headers_count) {}
  nghttp2_session* session() { return session_; }
  using ClientConnectionImpl::getStream;
  using ConnectionImpl::sendPendingFrames;

protected:
  // Overrides ClientConnectionImpl::onSettingsForTest().
  void onSettingsForTest(const nghttp2_settings& settings) override { onSettingsFrame(settings); }
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
