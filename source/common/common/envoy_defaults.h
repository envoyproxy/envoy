#pragma once

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {

struct DefaultsProfile {
  uint32_t some_default_val = 1024 * 1024;
};

using DefaultsProfileSingleton = InjectableSingleton<DefaultsProfile>;

} // namespace Envoy
