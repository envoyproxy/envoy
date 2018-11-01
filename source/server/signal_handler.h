#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Server {

class SignalHandler {
public:
  SignalHandler();

  SignalHandler(std::string input);

  bool allows(std::string signal_name) const;

private:
  // List of allowed signals.
  std::vector<std::string> signals_;
};

} // namespace Server
} // namespace Envoy
