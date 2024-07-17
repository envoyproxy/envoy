#pragma once

#include <functional>

#include "envoy/http/conn_pool.h"

#include "source/common/http/codec_client.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"

namespace Envoy {
/**
 * A fake CodecClient that 1) allows a mock codec to be passed in and 2) Allows for a destroy
 * callback.
 */
class CodecClientForTest : public Http::CodecClient {
public:
  using DestroyCb = std::function<void(CodecClient*)>;
  CodecClientForTest(Http::CodecType type, Network::ClientConnectionPtr&& connection,
                     Http::ClientConnection* codec, DestroyCb destroy_cb,
                     Upstream::HostDescriptionConstSharedPtr host, Event::Dispatcher& dispatcher)
      : CodecClient(type, std::move(connection), host, dispatcher), destroy_cb_(destroy_cb) {
    codec_.reset(codec);
    connect();
  }
  ~CodecClientForTest() override {
    if (destroy_cb_) {
      destroy_cb_(this);
    }
  }
  void raiseGoAway(Http::GoAwayErrorCode error_code) { onGoAway(error_code); }
  Event::Timer* idleTimer() { return idle_timer_.get(); }
  using Http::CodecClient::onSettings;

  DestroyCb destroy_cb_;
};

/**
 * Mock callbacks used for conn pool testing.
 */
struct ConnPoolCallbacks : public Http::ConnectionPool::Callbacks {
  void onPoolReady(Http::RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
                   StreamInfo::StreamInfo&, absl::optional<Http::Protocol>) override {
    outer_encoder_ = &encoder;
    host_ = host;
    pool_ready_.ready();
  }

  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    host_ = host;
    reason_ = reason;
    transport_failure_reason_ = transport_failure_reason;
    pool_failure_.ready();
  }

  ConnectionPool::PoolFailureReason reason_;
  std::string transport_failure_reason_;
  testing::NiceMock<ReadyWatcher> pool_failure_;
  testing::NiceMock<ReadyWatcher> pool_ready_;
  Http::RequestEncoder* outer_encoder_{};
  Upstream::HostDescriptionConstSharedPtr host_;
};

/**
 * Common utility functions for HTTP tests.
 */
class HttpTestUtility {
public:
  static void addDefaultHeaders(Http::RequestHeaderMap& headers, bool overwrite = true);
};
} // namespace Envoy
