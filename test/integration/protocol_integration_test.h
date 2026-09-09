#pragma once

#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {

// Tests for ProtocolIntegrationTest will be run with the full mesh of H1/H2
// downstream and H1/H2 upstreams.
class ProtocolIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void TearDown() override {
    std::string counter = "cluster.cluster_0.upstream_rq_total";
    if (!testing_upstream_intentionally_ && test_server_ &&
        (!test_server_->counter(counter) || test_server_->counter(counter)->value() == 0)) {
      // Avoid the CPU cost of running the test for both HTTP/1 and HTTP/2
      // upstreams if no upstream connections are used.
      FAIL() << "This test does not appear to use upstream connections."
             << "Please use DownstreamProtocolIntegrationTest instead of ProtocolIntegrationTest.";
    }
  }
  // Allow exceptions to the rule. There are some tests which will do upstream
  // calls for some downstream protocols and not for others, and those still
  // need the full mesh.
  bool testing_upstream_intentionally_{};

protected:
  // Configures the downstream listener to request a client certificate and validate it against
  // `trusted_ca` (a file under test/config/integration/certs). With `accept_untrusted` a
  // certificate that fails validation is still accepted; QUIC rejects that combination at
  // configuration load time, so it is only usable with TCP-based downstream protocols.
  void setDownstreamClientCertValidation(const std::string& trusted_ca,
                                         bool accept_untrusted = false);
  // Creates a downstream connection presenting the client certificate selected by `options`.
  Network::ClientConnectionPtr
  makeDownstreamMtlsConnection(const Ssl::ClientSslTransportOptions& options);

private:
  // Keeps the TLS client transport socket factory alive for the connection it created.
  Network::UpstreamTransportSocketFactoryPtr downstream_mtls_transport_socket_factory_;
};

// Tests for DownstreamProtocolIntegrationTest will be run with all protocols
// (H1/H2 downstream) but only H1 upstreams.
//
// This is useful for things which will likely not differ based on upstream
// behavior, for example "how does Envoy handle duplicate content lengths from
// downstream"?
class DownstreamProtocolIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  template <class T> void changeHeadersForStopAllTests(T& headers, bool set_buffer_limit) {
    headers.addCopy("content_size", std::to_string(count_ * size_));
    headers.addCopy("added_size", std::to_string(added_decoded_data_size_));
    headers.addCopy("is_first_trigger", "value");
    if (set_buffer_limit) {
      headers.addCopy("buffer_limit", std::to_string(buffer_limit_));
    }
  }

  void verifyUpStreamRequestAfterStopAllFilter() {
    if (downstreamProtocol() != Http::CodecType::HTTP1) {
      // decode-headers-return-stop-all-filter calls addDecodedData in decodeData and
      // decodeTrailers. 2 decoded data were added.
      EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * 2, upstream_request_->bodyLength());
    } else {
      EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * 1, upstream_request_->bodyLength());
    }
    EXPECT_EQ(true, upstream_request_->complete());
  }

  const int count_ = 70;
  const int size_ = 1000;
  const int added_decoded_data_size_ = 1;
  const int buffer_limit_ = 100;
};

} // namespace Envoy
