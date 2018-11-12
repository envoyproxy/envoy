#include "server/config_validation/async_client.h"

namespace Envoy {
namespace Http {

ValidationAsyncClient::ValidationAsyncClient(Event::TimeSystem& time_system)
    : dispatcher_(time_system) {}

AsyncClient::Request* ValidationAsyncClient::send(MessagePtr&&, Callbacks&, const RequestOptions&) {
  return nullptr;
}

AsyncClient::Stream* ValidationAsyncClient::start(StreamCallbacks&, const StreamOptions&) {
  return nullptr;
}

} // namespace Http
} // namespace Envoy
