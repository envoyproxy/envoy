#include "common/http/context_impl.h"

#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

ContextImpl::ContextImpl()
    : tracer_storage_(std::make_unique<Tracing::HttpNullTracer>()), tracer_(tracer_storage_.get()) {
}

ContextImpl::~ContextImpl() {}

} // namespace Http
} // namespace Envoy
