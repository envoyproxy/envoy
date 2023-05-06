#include "library/common/common/system_helper.h"
#include "library/common/common/default_system_helper.h"

namespace Envoy {

std::unique_ptr<SystemHelper> SystemHelper::instance_ = std::make_unique<DefaultSystemHelper>();

SystemHelper& SystemHelper::getInstance() {
  return *instance_;
}

}  // namespace Envoy
