#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {

/**
 * Active client base for HTTP/2 and HTTP/3
 */
class MultiplexedActiveClientBase : public CodecClientCallbacks,
                                    public Http::ConnectionCallbacks,
                                    public Envoy::Http::ActiveClient {
public:
  MultiplexedActiveClientBase(HttpConnPoolImplBase& parent, Stats::Counter& cx_total);
  ~MultiplexedActiveClientBase() override = default;

  // ConnPoolImpl::ActiveClient
  bool closingWithIncompleteStream() const override;
  RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;

  // CodecClientCallbacks
  void onStreamDestroy() override;
  void onStreamReset(Http::StreamResetReason reason) override;

  // Http::ConnectionCallbacks
  void onGoAway(Http::GoAwayErrorCode error_code) override;

protected:
  MultiplexedActiveClientBase(Envoy::Http::HttpConnPoolImplBase& parent,
                              Upstream::Host::CreateConnectionData& data, Stats::Counter& cx_total);

private:
  bool closed_with_active_rq_{};
};

namespace Http2 {

/**
 * Implementation of an active client for HTTP/2
 */
class ActiveClient : public MultiplexedActiveClientBase {
public:
  ActiveClient(HttpConnPoolImplBase& parent);
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               Upstream::Host::CreateConnectionData& data);
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state);

} // namespace Http2
} // namespace Http
} // namespace Envoy
