#include "source/common/common/assert.h"

#include "test_extensions.h"

namespace Envoy {

class Autoregister {
public:
  Autoregister() { register_test_extensions(); }
};

} // namespace Envoy

static Envoy::Autoregister auto_;
