#pragma once

#include "common/common/base_logger.h"

namespace Envoy {
namespace Logger {

class AndroidLogger : public Logger {
private:
AndroidLogger(const std:string& name);

friend class Registry;
};

} // namespace Logger
} // namespace Envoy