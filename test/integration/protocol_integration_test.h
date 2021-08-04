#pragma once

#include "test/integration/http_protocol_integration.h"

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

  void expectWireBytesSentAndReceived(int log_id, int h1_wire_bytes_sent,
                                      int h1_wire_bytes_received, int h2_wire_bytes_sent,
                                      int h2_wire_bytes_received) {
    auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 30); };
    std::string access_log = waitForAccessLog(upstream_access_log_name_, log_id);
    std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
    int wire_bytes_sent = std::stoi(log_entries[0]),
        wire_bytes_received = std::stoi(log_entries[1]);
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      EXPECT_EQ(h1_wire_bytes_sent, wire_bytes_sent);
      EXPECT_EQ(h1_wire_bytes_received, wire_bytes_received);
    }
    if (upstreamProtocol() == Http::CodecType::HTTP2) {
      // Because of non-deterministic h2 compression, the same plain text length don't map to the
      // same number of wire bytes.
      // EXPECT_EQ(h2_wire_bytes_sent, wire_bytes_sent);
      // EXPECT_EQ(h2_wire_bytes_received, wire_bytes_received);
      EXPECT_TRUE(integer_near(h2_wire_bytes_sent, wire_bytes_sent));
      EXPECT_TRUE(integer_near(h2_wire_bytes_received, wire_bytes_received));
    }
  }

  // Allow exceptions to the rule. There are some tests which will do upstream
  // calls for some downstream protocols and not for others, and those still
  // need the full mesh.
  bool testing_upstream_intentionally_{};
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
    if (downstreamProtocol() >= Http::CodecType::HTTP2) {
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
