#pragma once

#include <list>
#include <memory>

#include "envoy/network/filter.h"

#include "common/common/linked_object.h"

namespace Envoy {
namespace Network {

/**
 * Interface used to obtain read/write buffers.
 */
class BufferSource {
public:
  virtual ~BufferSource() {}

  struct StreamBuffer {
    Buffer::Instance& buffer;
    bool end_stream;
  };

  /**
   * Fetch the read buffer for the source.
   */
  virtual StreamBuffer getReadBuffer() PURE;

  /**
   * Fetch the write buffer for the source.
   */
  virtual StreamBuffer getWriteBuffer() PURE;
};

/**
 * This is a filter manager for TCP (L4) filters. It is split out for ease of testing.
 */
class FilterManagerImpl {
public:
  FilterManagerImpl(Connection& connection, BufferSource& buffer_source)
      : connection_(connection), buffer_source_(buffer_source) {}

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
    void continueReading() override { parent_.onContinueReading(this); }
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

  typedef std::unique_ptr<ActiveReadFilter> ActiveReadFilterPtr;

  void onContinueReading(ActiveReadFilter* filter);

  Connection& connection_;
  BufferSource& buffer_source_;
  Upstream::HostDescriptionConstSharedPtr host_description_;
  std::list<ActiveReadFilterPtr> upstream_filters_;
  std::list<WriteFilterSharedPtr> downstream_filters_;
};

} // namespace Network
} // namespace Envoy
