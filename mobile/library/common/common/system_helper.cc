#include "library/common/common/system_helper.h"

#include "library/common/common/default_system_helper.h"

#if defined(USE_ANDROID_SYSTEM_HELPER)
#include "library/common/common/default_system_helper_android.cc"
#elif defined(__APPLE__)
#include "library/common/common/default_system_helper_apple.cc"
#else
#include "library/common/common/default_system_helper.cc"
#endif

namespace Envoy {

std::unique_ptr<SystemHelper> SystemHelper::instance_ = std::make_unique<DefaultSystemHelper>();

SystemHelper& SystemHelper::getInstance() { return *instance_; }

} // namespace Envoy
