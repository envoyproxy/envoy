#pragma once

#include "envoy/network/filter.h"

#include "common/common/linked_object.h"

namespace Network {

/**
 * Interface used to obtain read/write buffers.
 */
class BufferSource {
public:
  virtual ~BufferSource() {}

  /**
   * Fetch the read buffer for the source.
   */
  virtual Buffer::Instance& getReadBuffer() PURE;

  /**
   * Fetch the write buffer for the source.
   */
  virtual Buffer::Instance& getWriteBuffer() PURE;
};

/**
 * This is a filter manager for TCP (L4) filters. It is split out for ease of testing.
 */
class FilterManager {
public:
  FilterManager(Connection& connection, BufferSource& buffer_source)
      : connection_(connection), buffer_source_(buffer_source) {}

  void addWriteFilter(WriteFilterPtr filter);
  void addFilter(FilterPtr filter);
  void addReadFilter(ReadFilterPtr filter);
  void destroyFilters();
  void onRead();
  FilterStatus onWrite();

private:
  struct ActiveReadFilter : public ReadFilterCallbacks, LinkedObject<ActiveReadFilter> {
    ActiveReadFilter(FilterManager& parent, ReadFilterPtr filter)
        : parent_(parent), filter_(filter) {}

    Connection& connection() override { return parent_.connection_; }
    void continueReading() override { parent_.onContinueReading(this); }
    Upstream::HostDescriptionPtr upstreamHost() override { return parent_.host_description_; }
    void upstreamHost(Upstream::HostDescriptionPtr host) override {
      parent_.host_description_ = host;
    }

    FilterManager& parent_;
    ReadFilterPtr filter_;
  };

  typedef std::unique_ptr<ActiveReadFilter> ActiveReadFilterPtr;

  void onContinueReading(ActiveReadFilter* filter);

  Connection& connection_;
  BufferSource& buffer_source_;
  Upstream::HostDescriptionPtr host_description_;
  std::list<ActiveReadFilterPtr> upstream_filters_;
  std::list<WriteFilterPtr> downstream_filters_;
};

} // Network
