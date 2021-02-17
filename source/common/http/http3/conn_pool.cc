#include "common/http/http3/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "common/config/utility.h"
#include "common/http/http3/quic_client_connection_factory.h"
#include "common/http/http3/well_known_names.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {
namespace Http3 {

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, TimeSource& time_source) {
  return std::make_unique<FixedHttpConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [](HttpConnPoolImplBase* pool) { return std::make_unique<ActiveClient>(*pool); },
      [&dispatcher, &time_source](Upstream::Host::CreateConnectionData& data,
                                  HttpConnPoolImplBase* pool) {
        // TODO(#14829) this is creating CreateConnectionData and overwriting. Refactor the base
        // pool class to inject the correct CreateConnectionData and avoid the wasted allocation.
        auto host_address = data.host_description_->address();
        auto source_address = data.host_description_->cluster().sourceAddress();
        if (!source_address.get()) {
          source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
        }
        Network::TransportSocketFactory& transport_socket_factory =
            data.host_description_->transportSocketFactory();
        data.connection_->close(Network::ConnectionCloseType::NoFlush);
        data.connection_ =
            Config::Utility::getAndCheckFactoryByName<Http::QuicClientConnectionFactory>(
                Http::QuicCodecNames::get().Quiche)
                .createQuicNetworkConnection(host_address, source_address, transport_socket_factory,
                                             data.host_description_->cluster().statsScope(),
                                             dispatcher, time_source);
        CodecClientPtr codec{new CodecClientProd(
            CodecClient::Type::HTTP3, std::move(data.connection_), data.host_description_,
            pool->dispatcher(), pool->randomGenerator())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http3});
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
