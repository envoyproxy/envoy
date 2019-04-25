#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Secret {

/**
 * Callbacks invoked by a dynamic secret provider.
 */
class SecretCallbacks {
public:
  virtual ~SecretCallbacks() {}

  virtual void onAddOrUpdateSecret() PURE;
};

} // namespace Secret
} // namespace Envoy
