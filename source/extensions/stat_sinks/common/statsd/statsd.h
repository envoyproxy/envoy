#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/macros.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/stat_sinks/common/statsd/tag_formats.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

static const std::string& getDefaultPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "envoy"); }

/**
 * Implementation of Sink that writes to a UDP statsd address.
 */
class UdpStatsdSink : public Stats::Sink {
public:
  /**
   * Base interface for writing UDP datagrams.
   */
  class Writer : public ThreadLocal::ThreadLocalObject {
  public:
    virtual void write(const std::string& message) PURE;
    virtual void writeBuffer(Buffer::Instance& data) PURE;
  };

  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, Network::Address::InstanceConstSharedPtr address,
                const bool use_tag, const std::string& prefix = getDefaultPrefix(),
                absl::optional<uint64_t> buffer_size = absl::nullopt,
                const Statsd::TagFormat& tag_format = Statsd::getDefaultTagFormat());
  // For testing.
  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, const std::shared_ptr<Writer>& writer,
                const bool use_tag, const std::string& prefix = getDefaultPrefix(),
                absl::optional<uint64_t> buffer_size = absl::nullopt,
                const Statsd::TagFormat& tag_format = Statsd::getDefaultTagFormat())
      : tls_(tls.allocateSlot()), use_tag_(use_tag),
        prefix_(prefix.empty() ? getDefaultPrefix() : prefix),
        buffer_size_(buffer_size.value_or(0)), tag_format_(tag_format) {
    tls_->set(
        [writer](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return writer; });
  }

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override;

  bool getUseTagForTest() { return use_tag_; }
  uint64_t getBufferSizeForTest() { return buffer_size_; }
  const std::string& getPrefix() { return prefix_; }

private:
  /**
   * This is a simple UDP localhost writer for statsd messages.
   */
  class WriterImpl : public Writer {
  public:
    WriterImpl(UdpStatsdSink& parent);

    // Writer
    void write(const std::string& message) override;
    void writeBuffer(Buffer::Instance& data) override;

  private:
    UdpStatsdSink& parent_;
    const Network::IoHandlePtr io_handle_;
  };

  void flushBuffer(Buffer::OwnedImpl& buffer, Writer& writer) const;
  void writeBuffer(Buffer::OwnedImpl& buffer, Writer& writer, const std::string& data) const;

  template <class StatType, typename ValueType>
  const std::string buildMessage(const StatType& metric, ValueType value,
                                 const std::string& type) const;
  template <class StatType> const std::string getName(const StatType& metric) const;
  const std::string buildTagStr(const std::vector<Stats::Tag>& tags) const;

  const ThreadLocal::SlotPtr tls_;
  const Network::Address::InstanceConstSharedPtr server_address_;
  const bool use_tag_;
  // Prefix for all flushed stats.
  const std::string prefix_;
  const uint64_t buffer_size_;
  const Statsd::TagFormat tag_format_;
};

/**
 * Per thread implementation of a TCP stats flusher for statsd.
 */
class TcpStatsdSink : public Stats::Sink {
public:
  TcpStatsdSink(const LocalInfo::LocalInfo& local_info, const std::string& cluster_name,
                ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cluster_manager,
                Stats::Scope& scope, const std::string& prefix = getDefaultPrefix());

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override;

  const std::string& getPrefix() { return prefix_; }

private:
  struct TlsSink : public ThreadLocal::ThreadLocalObject, public Network::ConnectionCallbacks {
    TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher);
    ~TlsSink() override;

    void beginFlush(bool expect_empty_buffer);
    void commonFlush(const std::string& name, uint64_t value, char stat_type);
    void flushCounter(const std::string& name, uint64_t delta);
    void flushGauge(const std::string& name, uint64_t value);
    void endFlush(bool do_write);
    void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms);
    void onPercentHistogramComplete(const std::string& name, float value);
    uint64_t usedBuffer() const;
    void write(Buffer::Instance& buffer);

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    TcpStatsdSink& parent_;
    Event::Dispatcher& dispatcher_;
    Network::ClientConnectionPtr connection_;
    Buffer::OwnedImpl buffer_;
    absl::optional<Buffer::ReservationSingleSlice> current_buffer_reservation_;
    char* current_slice_mem_{};
  };

  // Somewhat arbitrary 16MiB limit for buffered stats.
  static constexpr uint32_t MAX_BUFFERED_STATS_BYTES = (1024 * 1024 * 16);

  // 16KiB intermediate buffer for flushing.
  static constexpr uint32_t FLUSH_SLICE_SIZE_BYTES = (1024 * 16);

  // Prefix for all flushed stats.
  const std::string prefix_;

  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  ThreadLocal::SlotPtr tls_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::Counter& cx_overflow_stat_;
};

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
