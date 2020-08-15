#pragma once

namespace Envoy {
namespace Network {

/**
 * Action that should occur on a connection after I/O.
 */
enum class PostIoAction {
  // Close the connection.
  Close,
  // Keep the connection open.
  KeepOpen
};

} // namespace Network
} // namespace Envoy
