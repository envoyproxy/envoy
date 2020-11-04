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
  ConnPoolImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
               Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

  ~ConnPoolImpl() override;

  // ConnPoolImplBase
  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override;

  class ActiveClient : public CodecClientCallbacks,
                       public Http::ConnectionCallbacks,
                       public Envoy::Http::ActiveClient {
  public:
    ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent);
    ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
                 Upstream::Host::CreateConnectionData& data);
    ~ActiveClient() override = default;

    ConnPoolImpl& parent() { return static_cast<ConnPoolImpl&>(parent_); }

    // ConnPoolImpl::ActiveClient
    bool closingWithIncompleteStream() const override;
    RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;

    // CodecClientCallbacks
    void onStreamDestroy() override;
    void onStreamReset(Http::StreamResetReason reason) override;

    // Http::ConnectionCallbacks
    void onGoAway(Http::GoAwayErrorCode error_code) override;

    bool closed_with_active_rq_{};
  };
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
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

} // namespace Http2
} // namespace Http
} // namespace Envoy
