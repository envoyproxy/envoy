#include "filter.h"
#include "source/extensions/filters/network/mtls_failure_response/filter.h"

#include "envoy/network/connection.h"

#include "source/extensions/filters/network/well_known_names.h"

#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

MtlsFailureResponseFilter::MtlsFailureResponseFilter(
    const envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse&
        config,
    Server::Configuration::FactoryContext&, std::shared_ptr<SharedTokenBucketImpl> token_bucket)
    : config_(config), token_bucket_(token_bucket) {}

Network::FilterStatus MtlsFailureResponseFilter::onNewConnection() {
  // Continue if connection is not using TLS
  if (!callbacks_->connection().ssl()) {
    return Network::FilterStatus::Continue;
  } else {
    // Wait for handshake to be completed if connection is using TLS
    return Network::FilterStatus::StopIteration;
  }
}

void MtlsFailureResponseFilter::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected) {
    return;
  }
  bool cert_valid = false;
  auto ssl = callbacks_->connection().ssl();

  if (!ssl) {
    return;
  }

  // Based on validation mode config, check if mTLS is valid
  if (config_.validation_mode() == envoy::extensions::filters::network::mtls_failure_response::v3::
                                       MtlsFailureResponse::PRESENTED) {
    cert_valid = ssl->peerCertificatePresented();
  } else if (config_.validation_mode() ==
             envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                 VALIDATED) {
    cert_valid = ssl->peerCertificateValidated();
  }

  // Based on failure mode config, respond back to client
  if (!cert_valid) {
    if (config_.failure_mode() == envoy::extensions::filters::network::mtls_failure_response::v3::
                                      MtlsFailureResponse::CLOSE_CONNECTION) {
      callbacks_->connection().close(Network::ConnectionCloseType::Abort,
                                     "client_cert_validation_failure");
    } else if (config_.failure_mode() ==
               envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse::
                   KEEP_CONNECTION_OPEN) {
      if (token_bucket_ && !token_bucket_->consume(1, false)) {
        callbacks_->connection().close(Network::ConnectionCloseType::Abort,
                                       "client_cert_validation_failure_no_token");
      } else {
        stop_iteration_ = true;
      }
    }
  }
  return;
}

Network::FilterStatus MtlsFailureResponseFilter::onData(Buffer::Instance&, bool) {
  return stop_iteration_ ? Network::FilterStatus::StopIteration : Network::FilterStatus::Continue;
}

} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
