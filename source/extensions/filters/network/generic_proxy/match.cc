#include "source/extensions/filters/network/generic_proxy/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

REGISTER_FACTORY(ServiceMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(MethodMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(PropertyMatchDataInputFactory, Matcher::DataInputFactory<Request>);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
