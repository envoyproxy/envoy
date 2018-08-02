#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Secret {

/**
 * Callbacks invoked by a secret manager.
 */
class SecretCallbacks {
public:
  virtual ~SecretCallbacks() {}

  virtual void onAddOrUpdateSecret() PURE;
};

} // namespace Secret
} // namespace Envoy
