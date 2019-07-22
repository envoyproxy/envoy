#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

/**
 * Class including extended methods.
 */
class ExtendedHeaderValues {
public:
  struct {
    const std::string Patch{"PATCH"};
  } MethodValues;
};

using ExtendedHeader = ConstSingleton<ExtendedHeaderValues>;

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
