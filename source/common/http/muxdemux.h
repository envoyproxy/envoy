#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Http {

class MuxDemux;

// Facade that combines multiple Http::AsyncClient::Stream into one object.
// Created my the MuxDemux::multicast() method.
class MultiStream {
  // TODO(yanavlasov): This is a temporary solution to allow using
  // weak_ptr<AsyncClient::StreamCallbacks> with AsyncClient::start. It will be removed once
  // AsyncClient::start accepts weak_ptr to callbacks.
  struct CallbacksFacade : public AsyncClient::StreamCallbacks {
    explicit CallbacksFacade(MultiStream& multistream,
                             std::weak_ptr<AsyncClient::StreamCallbacks> callbacks)
        : multistream_(multistream), callbacks_(callbacks) {}

    void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void onData(Buffer::Instance& data, bool end_stream) override;
    void onTrailers(ResponseTrailerMapPtr&& trailers) override;
    void onComplete() override;
    void onReset() override;

    MultiStream& multistream_;
    std::weak_ptr<AsyncClient::StreamCallbacks> callbacks_;
    bool is_idle_{false};
    AsyncClient::Stream* stream_{nullptr};
  };

public:
  ~MultiStream();

  // Iterator over streams. Allows sending different headers, body or trailers to different streams.
  struct StreamIterator {
    using difference_type = std::ptrdiff_t;
    using element_type = AsyncClient::Stream*;
    using pointer = element_type*;
    using reference = element_type&;
    explicit StreamIterator(std::list<CallbacksFacade>::iterator it) : it(it) {}
    StreamIterator() = default;

    reference operator*() { return it->stream_; }
    reference operator*() const { return it->stream_; }
    reference operator->() { return it->stream_; }
    reference operator->() const { return it->stream_; }
    StreamIterator& operator++() {
      ++it;
      return *this;
    }
    StreamIterator operator++(int) {
      StreamIterator tmp(*this);
      ++(*this);
      return tmp;
    }
    bool operator==(const StreamIterator& other) const { return it == other.it; }

    std::list<CallbacksFacade>::iterator it;
  };

  // Iterator over underlying Http::AsyncClient::Stream objects
  static_assert(std::forward_iterator<StreamIterator>);
  StreamIterator begin() { return StreamIterator(callbacks_facades_.begin()); }
  StreamIterator end() { return StreamIterator(callbacks_facades_.end()); }

  // Send the same headers to all streams.
  void multicastHeaders(RequestHeaderMap& headers, bool end_stream);
  // Send the same data to all streams.
  void multicastData(Buffer::Instance& data, bool end_stream);
  // Send the same trailers to all streams.
  void multicastTrailers(RequestTrailerMap& trailers);
  // Reset all streams.
  void multicastReset();

  bool isIdle() const { return active_streams_ == 0; };

private:
  friend class MuxDemux;
  MultiStream(std::weak_ptr<MuxDemux> muxdemux) : muxdemux_(muxdemux) {}

  absl::Status addStream(const AsyncClient::StreamOptions& options, absl::string_view cluster_name,
                         std::weak_ptr<AsyncClient::StreamCallbacks> callbacks,
                         Server::Configuration::FactoryContext& factory_context);
  void maybeSwitchToIdle();

  uint32_t active_streams_{0};
  std::weak_ptr<MuxDemux> muxdemux_;
  std::list<CallbacksFacade> callbacks_facades_;
};

// MuxDemux allows sending the same or different requests to multiple destinations.
// The same connections are re-used when sending repeated requests.
class MuxDemux : public std::enable_shared_from_this<MuxDemux> {
public:
  struct Callbacks {
    std::string cluster_name;
    std::weak_ptr<AsyncClient::StreamCallbacks> callbacks;
    absl::optional<AsyncClient::StreamOptions> options;
  };

  static std::shared_ptr<MuxDemux> create(Server::Configuration::FactoryContext& context) {
    return std::shared_ptr<MuxDemux>(new MuxDemux(context));
  }

  ~MuxDemux();

  // Multicast a request to multiple destinations. Multiplexer must be in an idle state.
  // Return a MultiStream object if the request was successfully sent to all destinations.
  // Error if the multiplexer was not in an idle state, or all streams failed to start.
  // Note releasing MultiStream object while it is not fully closed (i.e. the end_stream
  // was observed in both directions on all streams) will result in all still active streams being
  // reset.
  absl::StatusOr<std::unique_ptr<MultiStream>> multicast(const AsyncClient::StreamOptions& options,
                                                         absl::Span<const Callbacks> callbacks);

  // Returns true if the multiplexer is in an idle state.
  // Idle state is defined as:
  // - There are no requests in progress.
  bool isIdle() const { return is_idle_; }

private:
  friend class MultiStream;
  MuxDemux(Server::Configuration::FactoryContext& context);

  void switchToIdle() { is_idle_ = true; }

  Server::Configuration::FactoryContext& factory_context_;
  bool is_idle_{true};
};

} // namespace Http
} // namespace Envoy
