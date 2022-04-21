#include "source/extensions/filters/network/meta_protocol_proxy/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

REGISTER_FACTORY(ServiceMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(MethodMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(PropertyMatchDataInputFactory, Matcher::DataInputFactory<Request>);

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
