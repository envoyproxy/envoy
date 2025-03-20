#pragma once

#include <list>
#include <memory>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

#include "source/common/common/linked_object.h"

namespace Envoy {
namespace Network {

struct StreamBuffer {
  Buffer::Instance& buffer;
  const bool end_stream;
};

/**
 * Interface used to obtain read buffers.
 */
class ReadBufferSource {
public:
  virtual ~ReadBufferSource() = default;

  /**
   * Fetch the read buffer for the source.
   */
  virtual StreamBuffer getReadBuffer() PURE;
};

/**
 * Interface used to obtain write buffers.
 */
class WriteBufferSource {
public:
  virtual ~WriteBufferSource() = default;

  /**
   * Fetch the write buffer for the source.
   */
  virtual StreamBuffer getWriteBuffer() PURE;
};

/**
 * Adapter that masquerades a given buffer instance as a ReadBufferSource.
 */
class FixedReadBufferSource : public ReadBufferSource {
public:
  FixedReadBufferSource(Buffer::Instance& data, bool end_stream)
      : data_(data), end_stream_(end_stream) {}

  StreamBuffer getReadBuffer() override { return {data_, end_stream_}; }

private:
  Buffer::Instance& data_;
  const bool end_stream_;
};

/**
 * Adapter that masquerades a given buffer instance as a WriteBufferSource.
 */
class FixedWriteBufferSource : public WriteBufferSource {
public:
  FixedWriteBufferSource(Buffer::Instance& data, bool end_stream)
      : data_(data), end_stream_(end_stream) {}

  StreamBuffer getWriteBuffer() override { return {data_, end_stream_}; }

private:
  Buffer::Instance& data_;
  const bool end_stream_;
};

/**
 * Connection enriched with methods for advanced cases, i.e. write data bypassing filter chain.
 *
 * Since FilterManager is only user of those methods for now, the class is named after it.
 */
class FilterManagerConnection : public virtual Connection,
                                public ReadBufferSource,
                                public WriteBufferSource {
public:
  ~FilterManagerConnection() override = default;

  /**
   * Write data to the connection bypassing filter chain.
   *
   * I.e., consider a scenario where iteration over the filter chain is stopped at some point
   * and later is resumed via a call to WriteFilterCallbacks::injectWriteDataToFilterChain().
   *
   * @param data supplies the data to write to the connection.
   * @param end_stream supplies whether this is the last byte to write on the connection.
   */
  virtual void rawWrite(Buffer::Instance& data, bool end_stream) PURE;

  virtual void closeConnection(ConnectionCloseAction action) PURE;
};

/**
 * This is a filter manager for TCP (L4) filters. It is split out for ease of testing.
 */
class FilterManagerImpl : protected Logger::Loggable<Logger::Id::connection> {
public:
  FilterManagerImpl(FilterManagerConnection& connection, const Socket& socket)
      : connection_(connection), socket_(socket) {}

  void addWriteFilter(WriteFilterSharedPtr filter);
  void addFilter(FilterSharedPtr filter);
  void addReadFilter(ReadFilterSharedPtr filter);
  void removeReadFilter(ReadFilterSharedPtr filter);
  bool initializeReadFilters();
  void onRead();
  FilterStatus onWrite();
  bool startUpstreamSecureTransport();
  void maybeClose();
  void onConnectionClose(ConnectionCloseAction close_action);

  void finalizeClose(ConnectionCloseAction close_action) {
    state_.pending_local_close_ = false;
    state_.pending_remote_close_ = false;
    connection_.closeConnection(close_action);
  }

  bool pendingClose() { return state_.pending_local_close_ || state_.pending_remote_close_; }

protected:
  struct State {
    uint32_t pending_close_write_filter_{0};
    uint32_t pending_close_read_filter_{0};
    bool pending_remote_close_{false};
    bool pending_local_close_{false};
  };

private:
  struct ActiveReadFilter : public ReadFilterCallbacks, LinkedObject<ActiveReadFilter> {
    ActiveReadFilter(FilterManagerImpl& parent, ReadFilterSharedPtr filter)
        : parent_(parent), filter_(filter) {}

    Connection& connection() override { return parent_.connection_; }
    const Socket& socket() override { return parent_.socket_; }
    void continueReading() override { parent_.onContinueReading(this, parent_.connection_); }
    void injectReadDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
      FixedReadBufferSource buffer_source{data, end_stream};
      parent_.onContinueReading(this, buffer_source);
    }

    void continueClosing() override {
      if (pending_close_) {
        pending_close_ = false;
        parent_.state_.pending_close_read_filter_ -= 1;
      }

      if (parent_.state_.pending_close_read_filter_ == 0) {
        parent_.maybeClose();
      }
    }

    void handleStopIterationAndDontClose() {
      if (!pending_close_) {
        pending_close_ = true;
        parent_.state_.pending_close_read_filter_ += 1;
      }
    }

    Upstream::HostDescriptionConstSharedPtr upstreamHost() override {
      return parent_.host_description_;
    }
    void upstreamHost(Upstream::HostDescriptionConstSharedPtr host) override {
      parent_.host_description_ = host;
    }
    bool startUpstreamSecureTransport() override { return parent_.startUpstreamSecureTransport(); }

    FilterManagerImpl& parent_;
    ReadFilterSharedPtr filter_;
    bool initialized_{};
    bool pending_close_{false};
  };

  using ActiveReadFilterPtr = std::unique_ptr<ActiveReadFilter>;

  struct ActiveWriteFilter : public WriteFilterCallbacks, LinkedObject<ActiveWriteFilter> {
    ActiveWriteFilter(FilterManagerImpl& parent, WriteFilterSharedPtr filter)
        : parent_(parent), filter_(std::move(filter)) {}

    Connection& connection() override { return parent_.connection_; }
    const Socket& socket() override { return parent_.socket_; }
    void injectWriteDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
      FixedWriteBufferSource buffer_source{data, end_stream};
      parent_.onResumeWriting(this, buffer_source);
    }

    void continueClosing() override {
      if (pending_close_) {
        pending_close_ = false;
        parent_.state_.pending_close_write_filter_ -= 1;
      }

      if (parent_.state_.pending_close_write_filter_ == 0) {
        parent_.maybeClose();
      }
    }

    void handleStopIterationAndDontClose() {
      if (!pending_close_) {
        pending_close_ = true;
        parent_.state_.pending_close_write_filter_ += 1;
      }
    }

    FilterManagerImpl& parent_;
    WriteFilterSharedPtr filter_;
    bool pending_close_{false};
  };

  using ActiveWriteFilterPtr = std::unique_ptr<ActiveWriteFilter>;

  void onContinueReading(ActiveReadFilter* filter, ReadBufferSource& buffer_source);

  FilterStatus onWrite(ActiveWriteFilter* filter, WriteBufferSource& buffer_source);
  void onResumeWriting(ActiveWriteFilter* filter, WriteBufferSource& buffer_source);

  FilterManagerConnection& connection_;
  const Socket& socket_;
  Upstream::HostDescriptionConstSharedPtr host_description_;
  std::list<ActiveReadFilterPtr> upstream_filters_;
  std::list<ActiveWriteFilterPtr> downstream_filters_;
  State state_;
  absl::optional<ConnectionCloseAction> latched_close_action_;
};

} // namespace Network
} // namespace Envoy
