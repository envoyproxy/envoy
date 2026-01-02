#include "source/common/http/muxdemux.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/assert.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Http {

void MultiStream::multicastHeaders(RequestHeaderMap& headers, bool end_stream) {
  for (CallbacksFacade& facade : callbacks_facades_) {
    if (!facade.is_idle_) {
      facade.stream_->sendHeaders(headers, end_stream);
    }
  }
}

void MultiStream::multicastData(Buffer::Instance& data, bool end_stream) {
  // Make a copy of data to workaround the sendData call moving slices out of the buffer.
  // We only need to do it, if there is more than 1 stream.
  Buffer::OwnedImpl copy;
  if (active_streams_ > 1) {
    copy.add(data);
  }
  uint32_t streams_done = 0;
  for (CallbacksFacade& facade : callbacks_facades_) {
    if (!facade.is_idle_) {
      facade.stream_->sendData(data, end_stream);
      ++streams_done;
      if (data.length() == 0) {
        // Avoid copy on the last call to sendData
        if (streams_done < active_streams_ - 1) {
          data.add(copy);
        } else {
          data.move(copy);
        }
      }
    }
  }
}

void MultiStream::multicastTrailers(RequestTrailerMap& trailers) {
  for (CallbacksFacade& facade : callbacks_facades_) {
    if (!facade.is_idle_) {
      facade.stream_->sendTrailers(trailers);
    }
  }
}

void MultiStream::multicastReset() {
  for (CallbacksFacade& facade : callbacks_facades_) {
    if (!facade.is_idle_) {
      facade.stream_->reset();
      facade.is_idle_ = true;
    }
  }
  if (auto muxdemux = muxdemux_.lock()) {
    muxdemux->switchToIdle();
  }
}

// TODO(yavlasov): This is a temporary solution to allow using weak_ptr in AsyncClient::start.
// It will be removed once AsyncClient::start accepts weak_ptr to callbacks.
void MultiStream::CallbacksFacade::onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
  if (auto callbacks = callbacks_.lock()) {
    callbacks->onHeaders(std::move(headers), end_stream);
  }
}

void MultiStream::CallbacksFacade::onData(Buffer::Instance& data, bool end_stream) {
  if (auto callbacks = callbacks_.lock()) {
    callbacks->onData(data, end_stream);
  }
}

void MultiStream::CallbacksFacade::onTrailers(ResponseTrailerMapPtr&& trailers) {
  if (auto callbacks = callbacks_.lock()) {
    callbacks->onTrailers(std::move(trailers));
  }
}

void MultiStream::CallbacksFacade::onComplete() {
  is_idle_ = true;
  if (auto callbacks = callbacks_.lock()) {
    callbacks->onComplete();
  }
  multistream_.maybeSwitchToIdle();
}

void MultiStream::CallbacksFacade::onReset() {
  is_idle_ = true;
  if (auto callbacks = callbacks_.lock()) {
    callbacks->onReset();
  }
  multistream_.maybeSwitchToIdle();
}

MultiStream::~MultiStream() { multicastReset(); }

absl::Status MultiStream::addStream(const AsyncClient::StreamOptions& options,
                                    absl::string_view cluster_name,
                                    std::weak_ptr<AsyncClient::StreamCallbacks> callbacks,
                                    Server::Configuration::FactoryContext& factory_context) {
  Envoy::Upstream::ThreadLocalCluster* cluster =
      factory_context.serverFactoryContext().clusterManager().getThreadLocalCluster(cluster_name);
  if (cluster == nullptr) {
    // Allow missing clusters in case control plane did not converge yet.
    // TODO(yanavlasov): We can possibly fail request here as well.
    return absl::OkStatus();
  }
  callbacks_facades_.emplace_back(*this, callbacks);
  AsyncClient::Stream* stream =
      cluster->httpAsyncClient().start(callbacks_facades_.back(), options);
  if (stream == nullptr) {
    callbacks_facades_.pop_back();
    return absl::InternalError(absl::StrCat("Failed to start stream for cluster ", cluster_name));
  }
  callbacks_facades_.back().stream_ = stream;
  // If at least one stream was successfully started, multiplexer is not idle anymore.
  ++active_streams_;
  return absl::OkStatus();
}

void MultiStream::maybeSwitchToIdle() {
  ASSERT(active_streams_ > 0);
  --active_streams_;
  if (active_streams_ > 0) {
    return;
  }
  if (auto muxdemux = muxdemux_.lock()) {
    muxdemux->switchToIdle();
  }
}

MuxDemux::MuxDemux(Server::Configuration::FactoryContext& context) : factory_context_(context) {}

MuxDemux::~MuxDemux() {}

absl::StatusOr<std::unique_ptr<MultiStream>>
MuxDemux::multicast(const AsyncClient::StreamOptions& options,
                    absl::Span<const Callbacks> callbacks) {
  // Sanity checks
  if (callbacks.empty()) {
    return absl::InvalidArgumentError("No callbacks provided");
  }
  if (std::any_of(callbacks.begin(), callbacks.end(),
                  [](const Callbacks& callback) { return callback.cluster_name.empty(); })) {
    return absl::InvalidArgumentError("Cluster name is empty");
  }
  if (std::any_of(callbacks.begin(), callbacks.end(),
                  [](const Callbacks& callback) { return callback.callbacks.use_count() == 0; })) {
    return absl::InvalidArgumentError("Callbacks are null");
  }

  auto multistream = std::unique_ptr<MultiStream>(new MultiStream(shared_from_this()));
  for (const Callbacks& callback : callbacks) {
    // Use per-backend options if provided, otherwise fall back to the default options.
    const AsyncClient::StreamOptions& effective_options =
        callback.options.has_value() ? callback.options.value() : options;
    absl::Status status = multistream->addStream(effective_options, callback.cluster_name,
                                                 callback.callbacks, factory_context_);
    if (!status.ok()) {
      return status;
    }
  }
  // If no streams were actually started, return error.
  if (multistream->isIdle()) {
    return absl::InternalError("No streams were started");
  }

  is_idle_ = false;
  return multistream;
}

} // namespace Http
} // namespace Envoy
