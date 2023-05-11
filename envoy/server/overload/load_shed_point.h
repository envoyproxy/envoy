#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/**
 * A point within the connection or request lifecycle that provides context on
 * whether to shed load at that given stage for the current entity at the point.
 */
class LoadShedPoint {
public:
  virtual ~LoadShedPoint() = default;

  // Whether to shed the load.
  virtual bool shouldShedLoad() PURE;
};

using LoadShedPointPtr = std::unique_ptr<LoadShedPoint>;

/**
 * Provides configured LoadShedPoints.
 */
class LoadShedPointProvider {
public:
  virtual ~LoadShedPointProvider() = default;

  /**
   * Get the load shed point identified by the following string. Returns nullptr
   * for non-configured points.
   */
  virtual LoadShedPoint* getLoadShedPoint(absl::string_view point_name) PURE;
};

} // namespace Server
} // namespace Envoy
