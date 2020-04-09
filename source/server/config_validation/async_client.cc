#include "server/config_validation/async_client.h"

namespace Envoy {
namespace Http {

ValidationAsyncClient::ValidationAsyncClient(Api::Api& api, Event::TimeSystem& time_system)
    : dispatcher_(api, time_system) {}

AsyncClient::Request* ValidationAsyncClient::send(RequestMessagePtr&&, Callbacks&,
                                                  const RequestOptions&) {
  return nullptr;
}

AsyncClient::Stream* ValidationAsyncClient::start(StreamCallbacks&, const StreamOptions&) {
  return nullptr;
}

} // namespace Http
} // namespace Envoy
