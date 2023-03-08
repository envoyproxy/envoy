#include "mocks.h"

namespace Envoy {
namespace Dso {

MockHttpFilterDsoInstance::MockHttpFilterDsoInstance() : HttpFilterDso("mock") {}
MockHttpFilterDsoInstance::~MockHttpFilterDsoInstance() {}

} // namespace Dso
} // namespace Envoy
