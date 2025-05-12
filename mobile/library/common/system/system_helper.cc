#include "library/common/system/system_helper.h"

#include "library/common/system/default_system_helper.h"

#if defined(USE_ANDROID_SYSTEM_HELPER)
#include "library/common/system/default_system_helper_android.cc"
#elif defined(__APPLE__)
#include "library/common/system/default_system_helper_apple.cc"
#else
#include "library/common/system/default_system_helper.cc"
#endif

namespace Envoy {

SystemHelper* SystemHelper::instance_ = new DefaultSystemHelper();

SystemHelper& SystemHelper::getInstance() { return *instance_; }

} // namespace Envoy
