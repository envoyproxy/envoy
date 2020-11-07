#pragma once

#include <functional>

#include "envoy/http/conn_pool.h"

#include "common/http/codec_client.h"

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
  CodecClientForTest(CodecClient::Type type, Network::ClientConnectionPtr&& connection,
                     Http::ClientConnection* codec, DestroyCb destroy_cb,
                     Upstream::HostDescriptionConstSharedPtr host, Event::Dispatcher& dispatcher)
      : CodecClient(type, std::move(connection), host, dispatcher), destroy_cb_(destroy_cb) {
    codec_.reset(codec);
  }
  ~CodecClientForTest() override {
    if (destroy_cb_) {
      destroy_cb_(this);
    }
  }
  void raiseGoAway(Http::GoAwayErrorCode error_code) { onGoAway(error_code); }
  Event::Timer* idleTimer() { return idle_timer_.get(); }

  DestroyCb destroy_cb_;
};

/**
 * Mock callbacks used for conn pool testing.
 */
struct ConnPoolCallbacks : public Http::ConnectionPool::Callbacks {
  void onPoolReady(Http::RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
                   const StreamInfo::StreamInfo&) override {
    outer_encoder_ = &encoder;
    host_ = host;
    pool_ready_.ready();
  }

  void onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    host_ = host;
    reason_ = reason;
    pool_failure_.ready();
  }

  ConnectionPool::PoolFailureReason reason_;
  ReadyWatcher pool_failure_;
  ReadyWatcher pool_ready_;
  Http::RequestEncoder* outer_encoder_{};
  Upstream::HostDescriptionConstSharedPtr host_;
};

/**
 * Common utility functions for HTTP tests.
 */
class HttpTestUtility {
public:
  static void addDefaultHeaders(Http::RequestHeaderMap& headers,
                                const std::string default_method = "GET");
};
} // namespace Envoy
