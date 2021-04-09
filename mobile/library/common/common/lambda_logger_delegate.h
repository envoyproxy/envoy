#pragma once

#include <string>

#include "common/common/logger.h"

#include "absl/strings/string_view.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Logger {

class LambdaDelegate : public SinkDelegate {
public:
  LambdaDelegate(envoy_logger logger, DelegatingLogSinkSharedPtr log_sink);
  ~LambdaDelegate() override;

  // SinkDelegate
  void log(absl::string_view msg) override;
  // Currently unexposed. May be desired in the future.
  void flush() override{};

private:
  envoy_logger logger_;
};

using LambdaDelegatePtr = std::unique_ptr<LambdaDelegate>;

} // namespace Logger
} // namespace Envoy
