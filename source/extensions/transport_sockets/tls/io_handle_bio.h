#pragma once

#include "envoy/network/io_handle.h"

#include "openssl/bio.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// NOLINTNEXTLINE(readability-identifier-naming)
const BIO_METHOD* BIO_s_io_handle(void);

// NOLINTNEXTLINE(readability-identifier-naming)
BIO* BIO_new_io_handle(Envoy::Network::IoHandle* io_handle);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy