#pragma once

#include "envoy/http/conn_pool.h"

#include "common/http/codec_client.h"

#include "test/mocks/common.h"

/**
 * A fake CodecClient that 1) allows a mock codec to be passed in and 2) Allows or a destroy
 * callback.
 */
class CodecClientForTest : public Http::CodecClient {
public:
  typedef std::function<void(CodecClient*)> DestroyCb;

  CodecClientForTest(Network::ClientConnectionPtr&& connection, Http::ClientConnection* codec,
                     DestroyCb destroy_cb, Upstream::HostDescriptionPtr host)
      : CodecClient(CodecClient::Type::HTTP1, std::move(connection), host),
        destroy_cb_(destroy_cb) {
    codec_.reset(codec);
  }

  ~CodecClientForTest() {
    if (destroy_cb_) {
      destroy_cb_(this);
    }
  }

  void raiseGoAway() { onGoAway(); }

  DestroyCb destroy_cb_;
};

/**
 * Mock callbacks used for conn pool testing.
 */
struct ConnPoolCallbacks : public Http::ConnectionPool::Callbacks {
  void onPoolReady(Http::StreamEncoder& encoder, Upstream::HostDescriptionPtr host) override {
    outer_encoder_ = &encoder;
    host_ = host;
    pool_ready_.ready();
  }

  void onPoolFailure(Http::ConnectionPool::PoolFailureReason,
                     Upstream::HostDescriptionPtr host) override {
    host_ = host;
    pool_failure_.ready();
  }

  ReadyWatcher pool_failure_;
  ReadyWatcher pool_ready_;
  Http::StreamEncoder* outer_encoder_{};
  Upstream::HostDescriptionPtr host_;
};

/**
 * Common utility functions for HTTP tests.
 */
class HttpTestUtility {
public:
  static void addDefaultHeaders(Http::HeaderMap& headers);
};
