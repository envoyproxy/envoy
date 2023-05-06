#include "library/common/common/default_system_helper.h"

#include "library/common/jni/android_jni_utility.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view hostname) {
  return is_cleartext_permitted(hostname);
}

}  // namespace Envoy
