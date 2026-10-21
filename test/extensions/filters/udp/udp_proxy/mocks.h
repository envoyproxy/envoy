#pragma once

#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

class MockReadFilterCallbacks : public Network::UdpSessionReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks() override;

  MOCK_METHOD(uint64_t, sessionId, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(bool, continueFilterChain, (), (override));
  MOCK_METHOD(void, injectDatagramToFilterChain, (Network::UdpRecvData & data), (override));

  uint64_t session_id_{1};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class MockWriteFilterCallbacks : public Network::UdpSessionWriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks() override;

  MOCK_METHOD(uint64_t, sessionId, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(void, injectDatagramToFilterChain, (Network::UdpRecvData & data), (override));

  uint64_t session_id_{1};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class MockUdpTunnelingConfig : public UdpTunnelingConfig {
public:
  MockUdpTunnelingConfig(Http::HeaderEvaluator& header_evaluator);
  ~MockUdpTunnelingConfig() override;

  MOCK_METHOD(const std::string, proxyHost, (const StreamInfo::StreamInfo& stream_info),
              (const, override));
  MOCK_METHOD(const std::string, targetHost, (const StreamInfo::StreamInfo& stream_info),
              (const, override));
  MOCK_METHOD(const std::optional<uint32_t>&, proxyPort, (), (const, override));
  MOCK_METHOD(uint32_t, defaultTargetPort, (), (const, override));
  MOCK_METHOD(bool, usePost, (), (const, override));
  MOCK_METHOD(const std::string&, postPath, (), (const, override));
  MOCK_METHOD(Http::HeaderEvaluator&, headerEvaluator, (), (const, override));
  MOCK_METHOD(uint32_t, maxConnectAttempts, (), (const, override));
  MOCK_METHOD(const BackOffStrategyPtr&, backoffStrategy, (), (const, override));
  MOCK_METHOD(bool, bufferEnabled, (), (const, override));
  MOCK_METHOD(uint32_t, maxBufferedDatagrams, (), (const, override));
  MOCK_METHOD(uint64_t, maxBufferedBytes, (), (const, override));
  MOCK_METHOD(void, propagateResponseHeaders,
              (Http::ResponseHeaderMapPtr && headers,
               const StreamInfo::FilterStateSharedPtr& filter_state),
              (const, override));
  MOCK_METHOD(void, propagateResponseTrailers,
              (Http::ResponseTrailerMapPtr && trailers,
               const StreamInfo::FilterStateSharedPtr& filter_state),
              (const, override));

  std::string default_proxy_host_ = "default.host.com";
  std::string default_target_host_ = "default.target.host";
  const std::optional<uint32_t> default_proxy_port_ = 10;
  uint32_t default_target_port_{20};
  std::string post_path_ = "/default/post";
  Http::HeaderEvaluator& header_evaluator_;
};

class MockTunnelCreationCallbacks : public TunnelCreationCallbacks {
public:
  ~MockTunnelCreationCallbacks() override;

  MOCK_METHOD(void, onStreamSuccess, (Http::RequestEncoder & request_encoder), (override));
  MOCK_METHOD(void, onStreamFailure, (), (override));
};

class MockUpstreamTunnelCallbacks : public UpstreamTunnelCallbacks {
public:
  ~MockUpstreamTunnelCallbacks() override;

  MOCK_METHOD(void, onUpstreamEvent, (Network::ConnectionEvent event), (override));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));
  MOCK_METHOD(void, onUpstreamData, (Buffer::Instance & data, bool end_stream), (override));
};

class MockHttpUpstream : public HttpUpstream {
public:
  ~MockHttpUpstream() override;

  MOCK_METHOD(void, encodeData, (Buffer::Instance & data), (override));
  MOCK_METHOD(void, onDownstreamEvent, (Network::ConnectionEvent event), (override));
};

class MockHttpStreamCallbacks : public HttpStreamCallbacks {
public:
  ~MockHttpStreamCallbacks() override;

  MOCK_METHOD(void, onStreamReady,
              (StreamInfo::StreamInfo * info, std::unique_ptr<HttpUpstream>&& upstream,
               const Upstream::HostDescription& host,
               const Network::ConnectionInfoProvider& address_provider,
               Ssl::ConnectionInfoConstSharedPtr ssl_info),
              (override));
  MOCK_METHOD(void, onStreamFailure,
              (ConnectionPool::PoolFailureReason reason, absl::string_view failure_reason,
               const Upstream::HostDescription& host),
              (override));
  MOCK_METHOD(void, resetIdleTimer, (), (override));
};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
