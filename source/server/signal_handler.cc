#include "server/signal_handler.h"

#include <string>
#include <vector>

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Server {

// Canonical list of all signals which Envoy knows how to handle.
std::vector<std::string> all_signals = {"SIGTERM", "SIGUSR1", "SIGHUP"};

SignalHandler::SignalHandler() { signals_ = all_signals; }

SignalHandler::SignalHandler(std::string input) {
  // Special case to handle default options argument.
  if (input == "ALL") {
    signals_ = all_signals;
  } else if (input != "NONE") {
    signals_ = absl::StrSplit(input, ',');
  }
}

bool SignalHandler::allows(std::string signal) const {
  return std::find(signals_.begin(), signals_.end(), signal) != signals_.end();
}

} // namespace Server
} // namespace Envoy
