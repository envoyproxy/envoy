#include "mocks.h"

namespace Envoy {
namespace Dso {

MockHttpFilterDsoInstance::MockHttpFilterDsoInstance() : HttpFilterDso("mock") {}
MockHttpFilterDsoInstance::~MockHttpFilterDsoInstance() = default;

} // namespace Dso
} // namespace Envoy
