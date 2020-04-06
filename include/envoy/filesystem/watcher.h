#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Filesystem {

/**
 * Abstraction for a file watcher.
 */
class Watcher {
public:
  using OnChangedCb = std::function<void(uint32_t events)>;

  struct Events {
    static const uint32_t MovedTo = 0x1;
    static const uint32_t Modified = 0x2;
  };

  virtual ~Watcher() = default;

  /**
   * Add a file watch.
   * @param path supplies the path to watch.
   *        If path is a file, callback is called on events for the given file.
   *        If path is a directory (ends with "/"), callback is called on events
   *        for the given directory.
   * @param events supplies the events to watch.
   * @param cb supplies the callback to invoke when a change occurs.
   */
  virtual void addWatch(absl::string_view path, uint32_t events, OnChangedCb cb) PURE;
};

using WatcherPtr = std::unique_ptr<Watcher>;

} // namespace Filesystem
} // namespace Envoy
