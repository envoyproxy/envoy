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
 * Enables bounded ciphertext read-ahead on a BIO created by BIO_new_io_handle(). Call only after
 * the TLS handshake completes. Zero preserves direct reads without allocating a buffer. Once
 * enabled, subsequent calls must specify the same size.
 *
 * Returns true for an IoHandle BIO. Returns false without changes for null or other BIO types.
 */
bool enableIoHandleBioReadAhead(BIO* bio, uint32_t size);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
