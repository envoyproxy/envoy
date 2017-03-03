#include "server/config_validation/async_client.h"

namespace Http {

AsyncClient::Request* ValidationAsyncClient::send(MessagePtr&&, Callbacks&,
                                                  const Optional<std::chrono::milliseconds>&) {
  return nullptr;
}

AsyncClient::Stream* ValidationAsyncClient::start(StreamCallbacks&,
                                                  const Optional<std::chrono::milliseconds>&) {
  return nullptr;
}

} // Http
