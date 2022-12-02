#include "source/extensions/stat_sinks/common/statsd/statsd.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/stats/symbol_table.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

UdpStatsdSink::WriterImpl::WriterImpl(UdpStatsdSink& parent)
    : parent_(parent), io_handle_(Network::ioHandleForAddr(Network::Socket::Type::Datagram,
                                                           parent_.server_address_, {})) {}

void UdpStatsdSink::WriterImpl::write(const std::string& message) {
  // TODO(mattklein123): We can avoid this const_cast pattern by having a constant variant of
  // RawSlice. This can be fixed elsewhere as well.
  Buffer::RawSlice slice{const_cast<char*>(message.c_str()), message.size()};
  Network::Utility::writeToSocket(*io_handle_, &slice, 1, nullptr, *parent_.server_address_);
}

void UdpStatsdSink::WriterImpl::writeBuffer(Buffer::Instance& data) {
  Network::Utility::writeToSocket(*io_handle_, data, nullptr, *parent_.server_address_);
}

UdpStatsdSink::UdpStatsdSink(ThreadLocal::SlotAllocator& tls,
                             Network::Address::InstanceConstSharedPtr address, const bool use_tag,
                             const std::string& prefix, absl::optional<uint64_t> buffer_size,
                             const Statsd::TagFormat& tag_format)
    : tls_(tls.allocateSlot()), server_address_(std::move(address)), use_tag_(use_tag),
      prefix_(prefix.empty() ? Statsd::getDefaultPrefix() : prefix),
      buffer_size_(buffer_size.value_or(0)), tag_format_(tag_format) {
  tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<WriterImpl>(*this);
  });
}

void UdpStatsdSink::flush(Stats::MetricSnapshot& snapshot) {
  Writer& writer = tls_->getTyped<Writer>();
  Buffer::OwnedImpl buffer;

  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      const std::string counter_str = buildMessage(counter.counter_.get(), counter.delta_, "|c");
      writeBuffer(buffer, writer, counter_str);
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      const std::string gauge_str = buildMessage(gauge.get(), gauge.get().value(), "|g");
      writeBuffer(buffer, writer, gauge_str);
    }
  }

  flushBuffer(buffer, writer);
  // TODO(efimki): Add support of text readouts stats.
}

void UdpStatsdSink::writeBuffer(Buffer::OwnedImpl& buffer, Writer& writer,
                                const std::string& statsd_metric) const {
  if (statsd_metric.length() >= buffer_size_) {
    // Our statsd_metric is too large to fit into the buffer, skip buffering and write directly
    writer.write(statsd_metric);
  } else {
    if ((buffer.length() + statsd_metric.length() + 1) > buffer_size_) {
      // If we add the new statsd_metric, we'll overflow our buffer. Flush the buffer to make
      // room for the new statsd_metric.
      flushBuffer(buffer, writer);
    } else if (buffer.length() > 0) {
      // We have room and have metrics already in the buffer, add a newline to separate
      // metric entries.
      buffer.add("\n");
    }
    buffer.add(statsd_metric);
  }
}

void UdpStatsdSink::flushBuffer(Buffer::OwnedImpl& buffer, Writer& writer) const {
  if (buffer.length() == 0) {
    return;
  }
  writer.writeBuffer(buffer);
  buffer.drain(buffer.length());
}

void UdpStatsdSink::onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) {
  // For statsd histograms are all timers in milliseconds, Envoy histograms are however
  // not necessarily timers in milliseconds, for Envoy histograms suffixed with their corresponding
  // SI unit symbol this is acceptable, but for histograms without a suffix, especially those which
  // are timers but record in units other than milliseconds, it may make sense to scale the value to
  // milliseconds here and potentially suffix the names accordingly (minus the pre-existing ones for
  // backwards compatibility).
  std::string message;
  if (histogram.unit() == Stats::Histogram::Unit::Percent) {
    // 32-bit floating point values should have plenty of range for these values, and are faster to
    // operate on than 64-bit doubles.
    constexpr float divisor = Stats::Histogram::PercentScale;
    const float float_value = value;
    const float scaled = float_value / divisor;
    message = buildMessage(histogram, scaled, "|h");
  } else {
    message = buildMessage(histogram, std::chrono::milliseconds(value).count(), "|ms");
  }
  tls_->getTyped<Writer>().write(message);
}

template <typename ValueType>
const std::string UdpStatsdSink::buildMessage(const Stats::Metric& metric, ValueType value,
                                              const std::string& type) const {
  switch (tag_format_.tag_position) {
  case Statsd::TagPosition::TagAfterValue: {
    const std::string message = absl::StrCat(
        // metric name
        prefix_, ".", getName(metric),
        // value and type
        ":", value, type,
        // tags
        buildTagStr(metric.tags()));
    return message;
  }

  case Statsd::TagPosition::TagAfterName: {
    const std::string message = absl::StrCat(
        // metric name
        prefix_, ".", getName(metric),
        // tags
        buildTagStr(metric.tags()),
        // value and type
        ":", value, type);
    return message;
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

const std::string UdpStatsdSink::getName(const Stats::Metric& metric) const {
  if (use_tag_) {
    return metric.tagExtractedName();
  } else {
    return metric.name();
  }
}

const std::string UdpStatsdSink::buildTagStr(const std::vector<Stats::Tag>& tags) const {
  if (!use_tag_ || tags.empty()) {
    return "";
  }

  std::vector<std::string> tag_strings;
  tag_strings.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    tag_strings.emplace_back(tag.name_ + tag_format_.assign + tag.value_);
  }
  return tag_format_.start + absl::StrJoin(tag_strings, tag_format_.separator);
}

TcpStatsdSink::TcpStatsdSink(const LocalInfo::LocalInfo& local_info,
                             const std::string& cluster_name, ThreadLocal::SlotAllocator& tls,
                             Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                             const std::string& prefix)
    : prefix_(prefix.empty() ? Statsd::getDefaultPrefix() : prefix), tls_(tls.allocateSlot()),
      cluster_manager_(cluster_manager),
      cx_overflow_stat_(scope.counterFromStatName(
          Stats::StatNameManagedStorage("statsd.cx_overflow", scope.symbolTable()).statName())) {
  const auto cluster = Config::Utility::checkClusterAndLocalInfo("tcp statsd", cluster_name,
                                                                 cluster_manager, local_info);
  cluster_info_ = cluster->get().info();
  tls_->set([this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<TlsSink>(*this, dispatcher);
  });
}

void TcpStatsdSink::flush(Stats::MetricSnapshot& snapshot) {
  TlsSink& tls_sink = tls_->getTyped<TlsSink>();
  tls_sink.beginFlush(true);
  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      tls_sink.flushCounter(counter.counter_.get().name(), counter.delta_);
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      tls_sink.flushGauge(gauge.get().name(), gauge.get().value());
    }
  }
  // TODO(efimki): Add support of text readouts stats.
  tls_sink.endFlush(true);
}

void TcpStatsdSink::onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) {
  // For statsd histograms are all timers except percents.
  if (histogram.unit() == Stats::Histogram::Unit::Percent) {
    // 32-bit floating point values should have plenty of range for these values, and are faster to
    // operate on than 64-bit doubles.
    constexpr float divisor = Stats::Histogram::PercentScale;
    const float float_value = value;
    const float scaled = float_value / divisor;
    tls_->getTyped<TlsSink>().onPercentHistogramComplete(histogram.name(), scaled);
  } else {
    tls_->getTyped<TlsSink>().onTimespanComplete(histogram.name(),
                                                 std::chrono::milliseconds(value));
  }
}

TcpStatsdSink::TlsSink::TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher)
    : parent_(parent), dispatcher_(dispatcher) {}

TcpStatsdSink::TlsSink::~TlsSink() {
  if (connection_) {
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpStatsdSink::TlsSink::beginFlush(bool expect_empty_buffer) {
  ASSERT(!expect_empty_buffer || buffer_.length() == 0);
  ASSERT(current_slice_mem_ == nullptr);
  ASSERT(!current_buffer_reservation_.has_value());

  current_buffer_reservation_.emplace(buffer_.reserveSingleSlice(FLUSH_SLICE_SIZE_BYTES));

  ASSERT(current_buffer_reservation_->slice().len_ >= FLUSH_SLICE_SIZE_BYTES);
  current_slice_mem_ = reinterpret_cast<char*>(current_buffer_reservation_->slice().mem_);
}

void TcpStatsdSink::TlsSink::commonFlush(const std::string& name, uint64_t value, char stat_type) {
  ASSERT(current_slice_mem_ != nullptr);
  // 36 > 1 ("." after prefix) + 1 (":" after name) + 4 (postfix chars, e.g., "|ms\n") + 30 for
  // number (bigger than it will ever be)
  const uint32_t max_size = name.size() + parent_.getPrefix().size() + 36;
  if (current_buffer_reservation_->slice().len_ - usedBuffer() < max_size) {
    endFlush(false);
    beginFlush(false);
  }

  // Produces something like "envoy.{}:{}|c\n"
  // This written this way for maximum perf since with a large number of stats and at a high flush
  // rate this can become expensive.
  const char* snapped_current = current_slice_mem_;
  const std::string prefix = parent_.getPrefix();
  memcpy(current_slice_mem_, prefix.data(), prefix.size()); // NOLINT(safe-memcpy)
  current_slice_mem_ += prefix.size();
  *current_slice_mem_++ = '.';
  memcpy(current_slice_mem_, name.data(), name.size()); // NOLINT(safe-memcpy)
  current_slice_mem_ += name.size();
  *current_slice_mem_++ = ':';
  current_slice_mem_ += StringUtil::itoa(current_slice_mem_, 30, value);
  *current_slice_mem_++ = '|';
  *current_slice_mem_++ = stat_type;

  *current_slice_mem_++ = '\n';

  ASSERT(static_cast<uint64_t>(current_slice_mem_ - snapped_current) < max_size);
}

void TcpStatsdSink::TlsSink::flushCounter(const std::string& name, uint64_t delta) {
  commonFlush(name, delta, 'c');
}

void TcpStatsdSink::TlsSink::flushGauge(const std::string& name, uint64_t value) {
  commonFlush(name, value, 'g');
}

void TcpStatsdSink::TlsSink::endFlush(bool do_write) {
  ASSERT(current_slice_mem_ != nullptr);
  ASSERT(current_buffer_reservation_.has_value());
  current_buffer_reservation_->commit(usedBuffer());
  current_buffer_reservation_.reset();
  current_slice_mem_ = nullptr;
  if (do_write) {
    write(buffer_);
    ASSERT(buffer_.length() == 0);
  }
}

void TcpStatsdSink::TlsSink::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    dispatcher_.deferredDelete(std::move(connection_));
  }
}

void TcpStatsdSink::TlsSink::onTimespanComplete(const std::string& name,
                                                std::chrono::milliseconds ms) {
  // Ultimately it would be nice to perf optimize this path also, but it's not very frequent. It's
  // also currently not possible that this interleaves with any counter/gauge flushing.
  // See the comment at UdpStatsdSink::onHistogramComplete with respect to unit suffixes.
  ASSERT(current_slice_mem_ == nullptr);
  Buffer::OwnedImpl buffer(
      fmt::format("{}.{}:{}|ms\n", parent_.getPrefix().c_str(), name, ms.count()));
  write(buffer);
}

void TcpStatsdSink::TlsSink::onPercentHistogramComplete(const std::string& name, float value) {
  ASSERT(current_slice_mem_ == nullptr);
  Buffer::OwnedImpl buffer(fmt::format("{}.{}:{}|h\n", parent_.getPrefix().c_str(), name, value));
  write(buffer);
}

void TcpStatsdSink::TlsSink::write(Buffer::Instance& buffer) {
  // Guard against the stats connection backing up. In this case we probably have no visibility
  // into what is going on externally, but we also increment a stat that should be viewable
  // locally.
  // NOTE: In the current implementation, we write most stats on the main thread, but timers
  //       get emitted on the worker threads. Since this is using global buffered data, it's
  //       possible that we are about to kill the connection that is not actually backed up.
  //       This is essentially a panic state, so it's not worth keeping per thread buffer stats,
  //       since if we stay over, the other threads will eventually kill their connections too.
  // TODO(mattklein123): The use of the stat is somewhat of a hack, and should be replaced with
  // real flow control callbacks once they are available.
  if (parent_.cluster_info_->trafficStats().upstream_cx_tx_bytes_buffered_.value() >
      MAX_BUFFERED_STATS_BYTES) {
    if (connection_) {
      connection_->close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.cx_overflow_stat_.inc();
    buffer.drain(buffer.length());
    return;
  }

  if (!connection_) {
    const auto thread_local_cluster =
        parent_.cluster_manager_.getThreadLocalCluster(parent_.cluster_info_->name());
    Upstream::Host::CreateConnectionData info;
    if (thread_local_cluster != nullptr) {
      info = thread_local_cluster->tcpConn(nullptr);
    }
    if (!info.connection_) {
      buffer.drain(buffer.length());
      return;
    }

    connection_ = std::move(info.connection_);
    connection_->addConnectionCallbacks(*this);
    connection_->setConnectionStats(
        {parent_.cluster_info_->trafficStats().upstream_cx_rx_bytes_total_,
         parent_.cluster_info_->trafficStats().upstream_cx_rx_bytes_buffered_,
         parent_.cluster_info_->trafficStats().upstream_cx_tx_bytes_total_,
         parent_.cluster_info_->trafficStats().upstream_cx_tx_bytes_buffered_,
         &parent_.cluster_info_->trafficStats().bind_errors_, nullptr});
    connection_->connect();
  }

  connection_->write(buffer, false);
}

uint64_t TcpStatsdSink::TlsSink::usedBuffer() const {
  ASSERT(current_slice_mem_ != nullptr);
  ASSERT(current_buffer_reservation_.has_value());
  return current_slice_mem_ - reinterpret_cast<char*>(current_buffer_reservation_->slice().mem_);
}

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
