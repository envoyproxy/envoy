#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

void ExtProcHttpClient::cancel() {
  // TBD
  ;
}

void ExtProcHttpClient::onComplete() {
  // TBD
  ;
}

void ExtProcHttpClient::onSuccess(const Http::AsyncClient::Request& request,
                                  Http::ResponseMessagePtr&&/* response*/) {
  // TBD
  ;
}

void ExtProcHttpClient::onFailure(const Http::AsyncClient::Request& request,
                                  Http::AsyncClient::FailureReason/* reason*/) {
  // TBD
  ;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
