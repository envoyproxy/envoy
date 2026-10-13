#include "source/extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class UnframedTransportConfigFactory : public TransportFactoryBase<UnframedTransportImpl> {
public:
  UnframedTransportConfigFactory() : TransportFactoryBase(TransportNames::get().UNFRAMED) {}
};

/**
 * Static registration for the unframed transport. @see RegisterFactory.
 */
REGISTER_FACTORY(UnframedTransportConfigFactory, NamedTransportConfigFactory);

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
