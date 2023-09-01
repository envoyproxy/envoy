#include "extensions/transport_sockets/tls/ssl_handshaker/custom_ssl_handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CustomSslHandshaker {

//
// DownStream Tls Handshaker
//

const int TlsHandshakerRespSync = 0;

extern "C" void envoyTlsConnectionSelectCert(unsigned long long int envoyTlsHandshaker,
                                             int respType, unsigned long long int certNameData,
                                             int certNameLen) {

  if (envoyTlsHandshaker == 0) {
    return;
  }

  auto ch = reinterpret_cast<CustomSslHandshakerImplWeakPtrHolder*>(envoyTlsHandshaker);
  auto weak_ptr = ch->get();
  if (auto shaker = weak_ptr.lock()) {
    shaker->selectCertCb(respType == TlsHandshakerRespSync, reinterpret_cast<char*>(certNameData),
                         certNameLen);
  }

  delete ch;
}

} // namespace CustomSslHandshaker
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy