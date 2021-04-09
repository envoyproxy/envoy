#include "library/common/common/lambda_logger_delegate.h"

#include <iostream>

#include "library/common/data/utility.h"

namespace Envoy {
namespace Logger {

LambdaDelegate::LambdaDelegate(envoy_logger logger, DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(log_sink), logger_(logger) {
  setDelegate();
}

LambdaDelegate::~LambdaDelegate() { restoreDelegate(); }

void LambdaDelegate::log(absl::string_view msg) {
  logger_.log(Data::Utility::copyToBridgeData(msg), logger_.context);
}

} // namespace Logger
} // namespace Envoy
