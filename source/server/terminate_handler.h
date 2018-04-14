#pragma once

namespace Envoy {
namespace Server {

// This function sets the std::terminate to a function which will log as much of a backtrace as
// possible, then call abort.
void logOnTerminate();

} // namespace Server
} // namespace Envoy
