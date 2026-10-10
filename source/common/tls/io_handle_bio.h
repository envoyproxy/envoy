#pragma once

#include <cstdint>

#include "envoy/network/io_handle.h"

#include "openssl/bio.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * Creates a custom BIO that can read from/write to an IoHandle. It's equivalent to a socket BIO
 * but instead of relying on access to an fd, it relies on IoHandle APIs for all interactions. The
 * IoHandle must remain valid for the lifetime of the BIO.
 */
// NOLINTNEXTLINE(readability-identifier-naming)
BIO* BIO_new_io_handle(Envoy::Network::IoHandle* io_handle);

/**
 * Sets the ciphertext read-ahead target on a BIO created by BIO_new_io_handle(). Call only after
 * the TLS handshake completes. Buffered input is preserved when the target changes. Zero disables
 * further read-ahead after buffered input drains. Allocation is deferred until a socket read and
 * released when the socket is exhausted.
 *
 * Returns true for an IoHandle BIO. Returns false without changes for null or other BIO types.
 */
bool setIoHandleBioReadAheadSize(BIO* bio, uint64_t size);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
