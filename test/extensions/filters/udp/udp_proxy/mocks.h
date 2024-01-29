#pragma once

#include "source/extensions/filters/udp/udp_proxy/session_filters/filter.h"
#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks() override;

  MOCK_METHOD(uint64_t, sessionId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(bool, continueFilterChain, ());
  MOCK_METHOD(void, injectDatagramToFilterChain, (Network::UdpRecvData & data));

  uint64_t session_id_{1};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class MockWriteFilterCallbacks : public WriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks() override;

  MOCK_METHOD(uint64_t, sessionId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(void, injectDatagramToFilterChain, (Network::UdpRecvData & data));

  uint64_t session_id_{1};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class MockUdpTunnelingConfig : public UdpTunnelingConfig {
public:
  MockUdpTunnelingConfig(Http::HeaderEvaluator& header_evaluator);
  ~MockUdpTunnelingConfig() override;

  MOCK_METHOD(const std::string, proxyHost, (const StreamInfo::StreamInfo& stream_info), (const));
  MOCK_METHOD(const std::string, targetHost, (const StreamInfo::StreamInfo& stream_info), (const));
  MOCK_METHOD(const absl::optional<uint32_t>&, proxyPort, (), (const));
  MOCK_METHOD(uint32_t, defaultTargetPort, (), (const));
  MOCK_METHOD(bool, usePost, (), (const));
  MOCK_METHOD(const std::string&, postPath, (), (const));
  MOCK_METHOD(Http::HeaderEvaluator&, headerEvaluator, (), (const));
  MOCK_METHOD(uint32_t, maxConnectAttempts, (), (const));
  MOCK_METHOD(bool, bufferEnabled, (), (const));
  MOCK_METHOD(uint32_t, maxBufferedDatagrams, (), (const));
  MOCK_METHOD(uint64_t, maxBufferedBytes, (), (const));
  MOCK_METHOD(void, propagateResponseHeaders,
              (Http::ResponseHeaderMapPtr && headers,
               const StreamInfo::FilterStateSharedPtr& filter_state),
              (const));
  MOCK_METHOD(void, propagateResponseTrailers,
              (Http::ResponseTrailerMapPtr && trailers,
               const StreamInfo::FilterStateSharedPtr& filter_state),
              (const));

  std::string default_proxy_host_ = "default.host.com";
  std::string default_target_host_ = "default.target.host";
  const absl::optional<uint32_t> default_proxy_port_ = 10;
  uint32_t default_target_port_{20};
  std::string post_path_ = "/default/post";
  Http::HeaderEvaluator& header_evaluator_;
};

class MockTunnelCreationCallbacks : public TunnelCreationCallbacks {
public:
  ~MockTunnelCreationCallbacks() override;

  MOCK_METHOD(void, onStreamSuccess, (Http::RequestEncoder & request_encoder));
  MOCK_METHOD(void, onStreamFailure, ());
};

class MockUpstreamTunnelCallbacks : public UpstreamTunnelCallbacks {
public:
  ~MockUpstreamTunnelCallbacks() override;

  MOCK_METHOD(void, onUpstreamEvent, (Network::ConnectionEvent event));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(void, onUpstreamData, (Buffer::Instance & data, bool end_stream));
};

class MockHttpStreamCallbacks : public HttpStreamCallbacks {
public:
  ~MockHttpStreamCallbacks() override;

  MOCK_METHOD(void, onStreamReady,
              (StreamInfo::StreamInfo * info, std::unique_ptr<HttpUpstream>&& upstream,
               Upstream::HostDescriptionConstSharedPtr& host,
               const Network::ConnectionInfoProvider& address_provider,
               Ssl::ConnectionInfoConstSharedPtr ssl_info));
  MOCK_METHOD(void, onStreamFailure,
              (ConnectionPool::PoolFailureReason reason, absl::string_view failure_reason,
               Upstream::HostDescriptionConstSharedPtr host));
};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
