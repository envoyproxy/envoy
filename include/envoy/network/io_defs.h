#pragma once

#include "envoy/common/pure.h"

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

/**
 * Result of each I/O event.
 */
struct IoResult {
  PostIoAction action_;

  /**
   * Number of bytes processed by the I/O event.
   */
  uint64_t bytes_processed_;

  /**
   * True if an end-of-stream was read from a connection. This
   * can only be true for read operations.
   */
  bool end_stream_read_;
};

/**
 * Types of I/O handles
 */
enum class IoHandleType {
  // Normal socket fd
  SocketFd,

  // Generic transport session
  SessionId,

  // Enable multiplexing over a connection
  StreamId
};

class IoHandle {
public:
  IoHandle(int hdl = -1, IoHandleType type = IoHandleType::SocketFd)
    : handle_(hdl), type_(type) {}
    
  int handle_;
  IoHandleType type_;  // qualifier for handle_
};

} // namespace Network
} // namespace Envoy
