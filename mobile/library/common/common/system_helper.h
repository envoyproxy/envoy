#pragma once

#include <memory>

#include "absl/strings/string_view.h"
#include "envoy/common/pure.h"

namespace Envoy {

namespace test {
class SystemHelperPeer;
}  // namespace test

/**
 * SystemHelper provided a platform-agnostic API for interacting with platform-specific
 * system APIs. The singleton helper is accessible via the `getInstance()` static
 * method. Tests may override this singleton via `test::SystemHelperPeer` class.
 */
class SystemHelper {
public:
  virtual ~SystemHelper() = default;

  /**
   * @return true if the system permits cleartext requests.
   */
  virtual bool isCleartextPermitted(absl::string_view hostname) PURE;

  /**
   * @return a reference to the current SystemHelper instance.
   */
  static SystemHelper& getInstance();

private:
  friend class test::SystemHelperPeer;

  static std::unique_ptr<SystemHelper> instance_;
};

} // namespace Envoy
