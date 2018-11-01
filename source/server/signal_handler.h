#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class SignalHandler {
public:
  SignalHandler();

  SignalHandler(absl::string_view input);

  bool allows(absl::string_view signal_name) const;

private:
  // List of allowed signals.
  std::vector<std::string> signals_;
};

} // namespace Server
} // namespace Envoy
