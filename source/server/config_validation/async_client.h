#pragma once

#include <chrono>

#include "envoy/http/async_client.h"
#include "envoy/http/message.h"

#include "common/common/assert.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Http {

/**
 * Config-validation-only implementation of AsyncClient. Both methods on AsyncClient are allowed to
 * return nullptr if the request can't be created, so that's what the ValidationAsyncClient does in
 * all cases.
 */
class ValidationAsyncClient : public AsyncClient {
public:
  ValidationAsyncClient(Event::TimeSystem& time_system);

  // Http::AsyncClient
  AsyncClient::Request* send(MessagePtr&& request, Callbacks& callbacks,
                             const absl::optional<std::chrono::milliseconds>& timeout) override;
  AsyncClient::Stream* start(StreamCallbacks& callbacks,
                             const absl::optional<std::chrono::milliseconds>& timeout,
                             bool buffer_body_for_retry) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }

private:
  Event::ValidationDispatcher dispatcher_;
};

} // namespace Http
} // namespace Envoy
