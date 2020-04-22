#pragma once

#include <list>
#include <memory>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "common/common/linked_object.h"

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
};

/**
 * This is a filter manager for TCP (L4) filters. It is split out for ease of testing.
 */
class FilterManagerImpl {
public:
  FilterManagerImpl(FilterManagerConnection& connection) : connection_(connection) {}

  void addWriteFilter(WriteFilterSharedPtr filter);
  void addFilter(FilterSharedPtr filter);
  void addReadFilter(ReadFilterSharedPtr filter);
  bool initializeReadFilters();
  void onRead();
  FilterStatus onWrite();

private:
  struct ActiveReadFilter : public ReadFilterCallbacks, LinkedObject<ActiveReadFilter> {
    ActiveReadFilter(FilterManagerImpl& parent, ReadFilterSharedPtr filter)
        : parent_(parent), filter_(filter) {}

    Connection& connection() override { return parent_.connection_; }
    void continueReading() override { parent_.onContinueReading(this, parent_.connection_); }
    void injectReadDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
      FixedReadBufferSource buffer_source{data, end_stream};
      parent_.onContinueReading(this, buffer_source);
    }
    Upstream::HostDescriptionConstSharedPtr upstreamHost() override {
      return parent_.host_description_;
    }
    void upstreamHost(Upstream::HostDescriptionConstSharedPtr host) override {
      parent_.host_description_ = host;
    }

    FilterManagerImpl& parent_;
    ReadFilterSharedPtr filter_;
    bool initialized_{};
  };

  using ActiveReadFilterPtr = std::unique_ptr<ActiveReadFilter>;

  struct ActiveWriteFilter : public WriteFilterCallbacks, LinkedObject<ActiveWriteFilter> {
    ActiveWriteFilter(FilterManagerImpl& parent, WriteFilterSharedPtr filter)
        : parent_(parent), filter_(std::move(filter)) {}

    Connection& connection() override { return parent_.connection_; }
    void injectWriteDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
      FixedWriteBufferSource buffer_source{data, end_stream};
      parent_.onResumeWriting(this, buffer_source);
    }

    FilterManagerImpl& parent_;
    WriteFilterSharedPtr filter_;
  };

  using ActiveWriteFilterPtr = std::unique_ptr<ActiveWriteFilter>;

  void onContinueReading(ActiveReadFilter* filter, ReadBufferSource& buffer_source);

  FilterStatus onWrite(ActiveWriteFilter* filter, WriteBufferSource& buffer_source);
  void onResumeWriting(ActiveWriteFilter* filter, WriteBufferSource& buffer_source);

  FilterManagerConnection& connection_;
  Upstream::HostDescriptionConstSharedPtr host_description_;
  std::list<ActiveReadFilterPtr> upstream_filters_;
  std::list<ActiveWriteFilterPtr> downstream_filters_;
};

} // namespace Network
} // namespace Envoy
