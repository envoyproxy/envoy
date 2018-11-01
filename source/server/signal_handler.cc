#include "server/signal_handler.h"

#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

// Canonical list of all signals which Envoy knows how to handle.
//
// *NB*: If this list is changed, please also update `cli.rst` under `--listen_for_signals`.
std::vector<std::string> getAllSignals() { return {"SIGTERM", "SIGUSR1", "SIGHUP"}; }

SignalHandler::SignalHandler() { signals_ = getAllSignals(); }

SignalHandler::SignalHandler(absl::string_view input) {
  // Special case to handle default options argument.
  if (input == "all") {
    signals_ = getAllSignals();
  } else if (input != "none") {
    signals_ = absl::StrSplit(input, ',');
  }
}

bool SignalHandler::allows(absl::string_view signal) const {
  return std::find(signals_.begin(), signals_.end(), signal) != signals_.end();
}

} // namespace Server
} // namespace Envoy
