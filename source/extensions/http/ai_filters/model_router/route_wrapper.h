#pragma once

#include <utility>
#include <vector>

#include "envoy/stream_info/filter_state.h"
#include "envoy/type/ai/v3/routes.pb.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class LlmRoutesFilterStateObject : public StreamInfo::FilterState::Object {
public:
  using LlmRoutes = envoy::type::ai::v3::LlmRoutes;
  using Bindings = std::vector<HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr>;

  LlmRoutesFilterStateObject(LlmRoutes routes, Bindings bindings)
      : routes_(std::move(routes)), bindings_(std::move(bindings)) {
    ASSERT(bindings_.size() == static_cast<size_t>(routes_.routes_size()));
  }

  const LlmRoutes& proto() const { return routes_; }
  const Bindings& bindings() const { return bindings_; }

private:
  const LlmRoutes routes_;
  const Bindings bindings_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
