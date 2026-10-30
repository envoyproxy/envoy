#pragma once

#include "envoy/stream_info/filter_state.h"
#include "envoy/type/ai/v3/routes.pb.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class LlmRoutesFilterStateObject : public StreamInfo::FilterState::Object {
public:
  using LlmRoutes = envoy::type::ai::v3::LlmRoutes;

  LlmRoutesFilterStateObject(const LlmRoutes& routes) : routes_(routes) {}

  const LlmRoutes& proto() const { return routes_; }

private:
  const LlmRoutes routes_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
