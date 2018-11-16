#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "openssl/ssl.h"
#include "openssl/hmac.h"
#include "openssl/rand.h"
#include "openssl/x509v3.h"

#include "common/ssl/ssl_impl.h"
#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

namespace Envoy {
namespace Ssl {

const SSL_METHOD *TLS_with_buffers_method(void) {
  return TLS_method();
}

void set_certificate_cb(SSL_CTX *ctx ){
  SSL_CTX_set_select_certificate_cb(
      ctx, [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
        const uint8_t* data;
        size_t len;
        if (SSL_early_callback_ctx_extension_get(
                client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &len)) {
          Envoy::Extensions::ListenerFilters::TlsInspector::Filter* filter = static_cast<Envoy::Extensions::ListenerFilters::TlsInspector::Filter*>(SSL_get_app_data(client_hello->ssl));
          filter->onALPN(data, len);
        }
        return ssl_select_cert_success;
      });
}

int getServernameCallbackReturn(int *out_alert) {
  *out_alert = SSL_AD_USER_CANCELLED;
   return SSL_TLSEXT_ERR_ALERT_FATAL;
}
 
std::vector<absl::string_view> getAlpnProtocols(const unsigned char* data, unsigned int len) {
  std::vector<absl::string_view> protocols;

  CBS wire, list;
  CBS_init(&wire, reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(len));
  if (!CBS_get_u16_length_prefixed(&wire, &list) || CBS_len(&wire) != 0 || CBS_len(&list) < 2) {
    // Don't produce errors, let the real TLS stack do it.
    return protocols;
  }
  CBS name;
  while (CBS_len(&list) > 0) {
    if (!CBS_get_u8_length_prefixed(&list, &name) || CBS_len(&name) == 0) {
      // Don't produce errors, let the real TLS stack do it.
      return protocols;
    }
    protocols.emplace_back(reinterpret_cast<const char*>(CBS_data(&name)), CBS_len(&name));
  }

  return protocols;
}

} // namespace Ssl
} // namespace Envoy
