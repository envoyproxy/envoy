#include "library/common/mobile_process_wide.h"

namespace Envoy {

MobileProcessWide::MobileProcessWide(const OptionsImplBase& options) {
  logging_context_ = std::make_unique<Logger::Context>(options.logLevel(), options.logFormat(),
                                                       log_lock_, options.logFormatEscaped(),
                                                       options.enableFineGrainLogging());
}

MobileProcessWide::~MobileProcessWide() { logging_context_.reset(nullptr); }

} // namespace Envoy
