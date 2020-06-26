#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Implementation of a "connection pool" for HTTP/2. This mainly handles stats as well as
 * shifting to a new connection if we reach max streams on the primary. This is a base class
 * used for both the prod implementation as well as the testing one.
 */
class ConnPoolImpl : public Envoy::Http::HttpConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

  ~ConnPoolImpl() override;

  // Http::ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http2; }

  // ConnPoolImplBase
  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override;

protected:
  class ActiveClient : public CodecClientCallbacks,
                       public Http::ConnectionCallbacks,
                       public Envoy::Http::ActiveClient {
  public:
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient() override = default;

    ConnPoolImpl& parent() { return static_cast<ConnPoolImpl&>(parent_); }

    // ConnPoolImpl::ActiveClient
    bool closingWithIncompleteRequest() const override;
    RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;

    // CodecClientCallbacks
    void onStreamDestroy() override { parent().onStreamDestroy(*this); }
    void onStreamReset(Http::StreamResetReason reason) override {
      parent().onStreamReset(*this, reason);
    }

    // Http::ConnectionCallbacks
    void onGoAway(Http::GoAwayErrorCode error_code) override {
      parent().onGoAway(*this, error_code);
    }

    bool closed_with_active_rq_{};
  };

  uint64_t maxRequestsPerConnection();
  void movePrimaryClientToDraining();
  void onGoAway(ActiveClient& client, Http::GoAwayErrorCode error_code);
  void onStreamDestroy(ActiveClient& client);
  void onStreamReset(ActiveClient& client, Http::StreamResetReason reason);

  // All streams are 2^31. Client streams are half that, minus stream 0. Just to be on the safe
  // side we do 2^29.
  static const uint64_t DEFAULT_MAX_STREAMS = (1 << 29);
};

/**
 * Production implementation of the HTTP/2 connection pool.
 */
class ProdConnPoolImpl : public ConnPoolImpl {
public:
  using ConnPoolImpl::ConnPoolImpl;

private:
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                 Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

} // namespace Http2
} // namespace Http
} // namespace Envoy
