#include "source/common/tracing/trace_context_impl.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Tracing {

TraceContextHandler::TraceContextHandler(absl::string_view key) : key_(key) {
  // This will force the header map to be finalized in unit tests and do nothing in prod (
  // where the header map is already finalized when the server is initializing).
  Http::TypedHeaderMapImpl<Http::RequestHeaderMap>::inlineHeadersSize();

  handle_ = Http::CustomInlineHeaderRegistry::getInlineHeader<
      Http::CustomInlineHeaderRegistry::Type::RequestHeaders>(key_);
}

void TraceContextHandler::set(TraceContext& trace_context, absl::string_view value) const {
  // Will dynamic_cast be better?
  auto header_map = trace_context.requestHeaders();
  if (!header_map.has_value()) {
    trace_context.set(key_, value);
    return;
  }

  if (handle_.has_value()) {
    header_map->setInline(handle_.value(), value);
  } else {
    header_map->setCopy(key_, value);
  }
}

void TraceContextHandler::setRefKey(TraceContext& trace_context, absl::string_view value) const {
  auto header_map = trace_context.requestHeaders();
  if (!header_map.has_value()) {
    trace_context.set(key_, value);
    return;
  }

  if (handle_.has_value()) {
    header_map->setInline(handle_.value(), value);
  } else {
    header_map->setReferenceKey(key_, value);
  }
}

void TraceContextHandler::setRef(TraceContext& trace_context, absl::string_view value) const {
  auto header_map = trace_context.requestHeaders();
  if (!header_map.has_value()) {
    trace_context.set(key_, value);
    return;
  }

  if (handle_.has_value()) {
    header_map->setReferenceInline(handle_.value(), value);
  } else {
    header_map->setReference(key_, value);
  }
}

absl::optional<absl::string_view>
TraceContextHandler::get(const TraceContext& trace_context) const {
  auto header_map = trace_context.requestHeaders();
  if (!header_map.has_value()) {
    return trace_context.get(key_);
  }

  if (handle_.has_value()) {
    auto* entry = header_map->getInline(handle_.value());
    if (entry == nullptr) {
      return absl::nullopt;
    }
    return entry->value().getStringView();
  } else {
    auto results = header_map->get(key_);
    if (results.empty()) {
      return absl::nullopt;
    }
    return results[0]->value().getStringView();
  }
}

void TraceContextHandler::remove(TraceContext& trace_context) const {
  auto header_map = trace_context.requestHeaders();
  if (!header_map.has_value()) {
    trace_context.remove(key_);
    return;
  }

  if (handle_.has_value()) {
    header_map->removeInline(handle_.value());
  } else {
    header_map->remove(key_);
  }
}

} // namespace Tracing
} // namespace Envoy
