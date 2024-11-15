#include "mocks.h"

namespace Envoy {
namespace Dso {

MockHttpFilterDsoImpl::MockHttpFilterDsoImpl() : HttpFilterDso("mock") {}
MockHttpFilterDsoImpl::~MockHttpFilterDsoImpl() = default;

} // namespace Dso
} // namespace Envoy
