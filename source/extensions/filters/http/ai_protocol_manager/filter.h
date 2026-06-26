#pragma once

#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class AiProtocolManagerFilter : public Http::PassThroughFilter {
public:
  AiProtocolManagerFilter() = default;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
