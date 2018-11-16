#pragma once

#include <functional>
#include <string>
#include <vector>

#include "openssl/ssl.h"
#include "common/ssl/ssl_impl.h"

namespace Envoy {
namespace Ssl {

const SSL_METHOD *TLS_with_buffers_method();

void set_certificate_cb(SSL_CTX *ctx);

std::vector<absl::string_view> getAlpnProtocols(const unsigned char* data, unsigned int len);
int getServernameCallbackReturn(int *out_alert);

} // namespace Ssl
} // namespace Envoy
