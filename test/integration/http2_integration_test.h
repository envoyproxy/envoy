#pragma once

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/integration/filters/test_socket_interface.h"
#include "test/integration/http_integration.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

using Envoy::Http::Http2::Http2Frame;

namespace Envoy {
class Http2IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                             public HttpIntegrationTest {
public:
  Http2IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override { setDownstreamProtocol(Http::CodecClient::Type::HTTP2); }

  void simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes);

protected:
  // Utility function to add filters.
  void addFilters(std::vector<std::string> filters) {
    for (const auto& filter : filters) {
      config_helper_.addFilter(filter);
    }
  }
};

class Http2RingHashIntegrationTest : public Http2IntegrationTest {
public:
  Http2RingHashIntegrationTest();

  ~Http2RingHashIntegrationTest() override;

  void createUpstreams() override;

  void sendMultipleRequests(int request_bytes, Http::TestRequestHeaderMapImpl headers,
                            std::function<void(IntegrationStreamDecoder&)> cb);

  std::vector<FakeHttpConnectionPtr> fake_upstream_connections_;
  int num_upstreams_ = 5;
};

class Http2MetadataIntegrationTest : public Http2IntegrationTest {
public:
  void SetUp() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_http2_protocol_options()->set_allow_metadata(true);
        });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void testRequestMetadataWithStopAllFilter();

  void verifyHeadersOnlyTest();

  void runHeaderOnlyTest(bool send_request_body, size_t body_size);
};

class Http2FrameIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  Http2FrameIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

protected:
  void startHttp2Session();
  Http2Frame readFrame();
  void sendFrame(const Http2Frame& frame);
  virtual void beginSession();

  IntegrationTcpClientPtr tcp_client_;
};

class SocketInterfaceSwap {
public:
  // Object of this class hold the state determining the IoHandle which
  // should return EAGAIN from the `writev` call.
  struct IoHandleMatcher {
    bool shouldReturnEgain(uint32_t port) const {
      absl::ReaderMutexLock lock(&mutex_);
      return port == port_ && writev_returns_egain_;
    }

    void setSourcePort(uint32_t port) {
      absl::WriterMutexLock lock(&mutex_);
      port_ = port;
    }

    void setWritevReturnsEgain() {
      absl::WriterMutexLock lock(&mutex_);
      writev_returns_egain_ = true;
    }

  private:
    mutable absl::Mutex mutex_;
    uint32_t port_ ABSL_GUARDED_BY(mutex_) = 0;
    bool writev_returns_egain_ ABSL_GUARDED_BY(mutex_) = false;
  };

  SocketInterfaceSwap();
  ~SocketInterfaceSwap();

protected:
  Envoy::Network::SocketInterface* const previous_socket_interface_{
      Envoy::Network::SocketInterfaceSingleton::getExisting()};
  std::shared_ptr<IoHandleMatcher> writev_matcher_{std::make_shared<IoHandleMatcher>()};
  std::unique_ptr<Envoy::Network::SocketInterfaceLoader> test_socket_interface_loader_;
};

// It is important that the new socket interface is installed before any I/O activity starts and
// the previous one is restored after all I/O activity stops. Since the HttpIntegrationTest
// destructor stops Envoy the SocketInterfaceSwap destructor needs to run after it. This order of
// multiple inheritance ensures that SocketInterfaceSwap destructor runs after
// Http2FrameIntegrationTest destructor completes.
class Http2FloodMitigationTest : public SocketInterfaceSwap, public Http2FrameIntegrationTest {
public:
  Http2FloodMitigationTest();

protected:
  void floodServer(const Http2Frame& frame, const std::string& flood_stat, uint32_t num_frames);
  void floodServer(absl::string_view host, absl::string_view path,
                   Http2Frame::ResponseStatus expected_http_status, const std::string& flood_stat,
                   uint32_t num_frames);

  void setNetworkConnectionBufferSize();
  void beginSession() override;
  void prefillOutboundDownstreamQueue(uint32_t data_frame_count, uint32_t data_frame_size = 10);
  void triggerListenerDrain();
};
} // namespace Envoy
