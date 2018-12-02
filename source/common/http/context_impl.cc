#include "common/http/context_impl.h"

namespace Envoy {
namespace Http {

ContextImpl::ContextImpl() : tracer_(&null_tracer_) {}

} // namespace Http
} // namespace Envoy
