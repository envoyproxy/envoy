#include "common/http/context_impl.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

ContextImpl::ContextImpl() : tracer_(std::make_unique<Tracing::HttpNullTracer>()) {}

ContextImpl::ContextImpl(Tracing::HttpTracerPtr tracer) : tracer_(std::move(tracer)) {}

ContextImpl::~ContextImpl() {}

} // namespace Http
} // namespace Envoy
