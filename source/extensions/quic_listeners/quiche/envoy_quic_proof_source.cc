#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/quic_io_handle_wrapper.h"
#include "extensions/transport_sockets/well_known_names.h"
#include "extensions/quic_listeners/quiche/quic_transport_socket_factory.h"
#include "openssl/bytestring.h"


namespace Envoy {
namespace Quic {

quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  EnvoyQuicProofSource::GetCertChain(const quic::QuicSocketAddress& server_address,
               const quic::QuicSocketAddress& client_address,
               const std::string& hostname) {
  ENVOY_LOG(trace, "Getting cert chain for {}", hostname);
  Network::ConnectionSocketImpl connection_socket(
                              std::make_unique<QuicIoHandleWrapper>(listen_socket_->ioHandle()),
                              quicAddressToEnvoyAddressInstance(server_address),
                              quicAddressToEnvoyAddressInstance(client_address));
  connection_socket.setDetectedTransportProtocol(
      Extensions::TransportSockets::TransportProtocolNames::get().Quic);
  connection_socket.setRequestedServerName(hostname);
  connection_socket.setRequestedApplicationProtocols({"h2"});
   const Network::FilterChain* filter_chain = filter_chain_manager_.findFilterChain(connection_socket);
   if (filter_chain == nullptr) {
     ENVOY_LOG(warn, "No matching filter chain found for handshake.");
     return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(new quic::ProofSource::Chain({}));
   }
   const Network::TransportSocketFactory& transport_socket_factory =
       filter_chain->transportSocketFactory();
   std::cerr << "got TransportSocketFactory " << &transport_socket_factory << " from filter_chain " << filter_chain << "\n";
   std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
       tls_cert_configs =
       dynamic_cast<const QuicServerTransportSocketFactory&>(transport_socket_factory).serverContextConfig().tlsCertificates();
 std::cerr << "got QuicTransportSocketFactory\n";

   // Only return the first TLS cert config.
   auto& cert_config = tls_cert_configs[0].get();
   std::string cert_chain_file_path = cert_config.certificateChainPath();
   bssl::UniquePtr<BIO> bio(
        BIO_new_mem_buf(const_cast<char*>(cert_config.certificateChain().data()),
                             cert_config.certificateChain().size()));

    std::vector<std::string> chain;
    RELEASE_ASSERT(bio != nullptr, "");
    uint8_t* data = nullptr;
    long len;
    while (true) {
       if (!PEM_bytes_read_bio(&data, &len, nullptr, PEM_STRING_X509, bio.get(), nullptr, nullptr)) {
            break;
       }
       bssl::UniquePtr<uint8_t> der(data);
       chain.emplace_back(reinterpret_cast<char*>(data), len);
    }
    if (chain.size() == 0) {
      throw EnvoyException(
          absl::StrCat("Failed to load certificate chain from ",
                       cert_chain_file_path));
    }
  return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(new quic::ProofSource::Chain(chain));
}

} // namespace Quic
} // namespace Envoy
