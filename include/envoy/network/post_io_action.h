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
  KeepOpen,
  // Close the connection because of an error.
  CloseError,
};

} // namespace Network
} // namespace Envoy
