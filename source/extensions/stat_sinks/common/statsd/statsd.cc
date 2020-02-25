#include "extensions/stat_sinks/common/statsd/statsd.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/network/utility.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

UdpStatsdSink::WriterImpl::WriterImpl(UdpStatsdSink& parent)
    : parent_(parent),
      io_handle_(parent_.server_address_->socket(Network::Address::SocketType::Datagram)) {}

void UdpStatsdSink::WriterImpl::write(const std::string& message) {
  // TODO(mattklein123): We can avoid this const_cast pattern by having a constant variant of
  // RawSlice. This can be fixed elsewhere as well.
  Buffer::RawSlice slice{const_cast<char*>(message.c_str()), message.size()};
  Network::Utility::writeToSocket(*io_handle_, &slice, 1, nullptr, *parent_.server_address_);
}

UdpStatsdSink::UdpStatsdSink(ThreadLocal::SlotAllocator& tls,
                             Network::Address::InstanceConstSharedPtr address, const bool use_tag,
                             const std::string& prefix)
    : tls_(tls.allocateSlot()), server_address_(std::move(address)), use_tag_(use_tag),
      prefix_(prefix.empty() ? Statsd::getDefaultPrefix() : prefix) {
  tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<WriterImpl>(*this);
  });
}

void UdpStatsdSink::flush(Stats::MetricSnapshot& snapshot) {
  Writer& writer = tls_->getTyped<Writer>();
  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      writer.write(absl::StrCat(prefix_, ".", getName(counter.counter_.get()), ":", counter.delta_,
                                "|c", buildTagStr(counter.counter_.get().tags())));
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      writer.write(absl::StrCat(prefix_, ".", getName(gauge.get()), ":", gauge.get().value(), "|g",
                                buildTagStr(gauge.get().tags())));
    }
  }
}

void UdpStatsdSink::onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) {
  // For statsd histograms are all timers in milliseconds, Envoy histograms are however
  // not necessarily timers in milliseconds, for Envoy histograms suffixed with their corresponding
  // SI unit symbol this is acceptable, but for histograms without a suffix, especially those which
  // are timers but record in units other than milliseconds, it may make sense to scale the value to
  // milliseconds here and potentially suffix the names accordingly (minus the pre-existing ones for
  // backwards compatibility).
  const std::string message(absl::StrCat(prefix_, ".", getName(histogram), ":",
                                         std::chrono::milliseconds(value).count(), "|ms",
                                         buildTagStr(histogram.tags())));
  tls_->getTyped<Writer>().write(message);
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
    tag_strings.emplace_back(tag.name_ + ":" + tag.value_);
  }
  return "|#" + absl::StrJoin(tag_strings, ",");
}

TcpStatsdSink::TcpStatsdSink(const LocalInfo::LocalInfo& local_info,
                             const std::string& cluster_name, ThreadLocal::SlotAllocator& tls,
                             Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                             const std::string& prefix)
    : prefix_(prefix.empty() ? Statsd::getDefaultPrefix() : prefix), tls_(tls.allocateSlot()),
      cluster_manager_(cluster_manager),
      cx_overflow_stat_(scope.counterFromStatName(
          Stats::StatNameManagedStorage("statsd.cx_overflow", scope.symbolTable()).statName())) {
  Config::Utility::checkClusterAndLocalInfo("tcp statsd", cluster_name, cluster_manager,
                                            local_info);
  cluster_info_ = cluster_manager.get(cluster_name)->info();
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
  tls_sink.endFlush(true);
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

  uint64_t num_iovecs = buffer_.reserve(FLUSH_SLICE_SIZE_BYTES, &current_buffer_slice_, 1);
  ASSERT(num_iovecs == 1);

  ASSERT(current_buffer_slice_.len_ >= FLUSH_SLICE_SIZE_BYTES);
  current_slice_mem_ = reinterpret_cast<char*>(current_buffer_slice_.mem_);
}

void TcpStatsdSink::TlsSink::commonFlush(const std::string& name, uint64_t value, char stat_type) {
  ASSERT(current_slice_mem_ != nullptr);
  // 36 > 1 ("." after prefix) + 1 (":" after name) + 4 (postfix chars, e.g., "|ms\n") + 30 for
  // number (bigger than it will ever be)
  const uint32_t max_size = name.size() + parent_.getPrefix().size() + 36;
  if (current_buffer_slice_.len_ - usedBuffer() < max_size) {
    endFlush(false);
    beginFlush(false);
  }

  // Produces something like "envoy.{}:{}|c\n"
  // This written this way for maximum perf since with a large number of stats and at a high flush
  // rate this can become expensive.
  const char* snapped_current = current_slice_mem_;
  memcpy(current_slice_mem_, parent_.getPrefix().c_str(), parent_.getPrefix().size());
  current_slice_mem_ += parent_.getPrefix().size();
  *current_slice_mem_++ = '.';
  memcpy(current_slice_mem_, name.c_str(), name.size());
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
  current_buffer_slice_.len_ = usedBuffer();
  buffer_.commit(&current_buffer_slice_, 1);
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
  if (parent_.cluster_info_->stats().upstream_cx_tx_bytes_buffered_.value() >
      MAX_BUFFERED_STATS_BYTES) {
    if (connection_) {
      connection_->close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.cx_overflow_stat_.inc();
    buffer.drain(buffer.length());
    return;
  }

  if (!connection_) {
    Upstream::Host::CreateConnectionData info =
        parent_.cluster_manager_.tcpConnForCluster(parent_.cluster_info_->name(), nullptr);
    if (!info.connection_) {
      buffer.drain(buffer.length());
      return;
    }

    connection_ = std::move(info.connection_);
    connection_->addConnectionCallbacks(*this);
    connection_->setConnectionStats({parent_.cluster_info_->stats().upstream_cx_rx_bytes_total_,
                                     parent_.cluster_info_->stats().upstream_cx_rx_bytes_buffered_,
                                     parent_.cluster_info_->stats().upstream_cx_tx_bytes_total_,
                                     parent_.cluster_info_->stats().upstream_cx_tx_bytes_buffered_,
                                     &parent_.cluster_info_->stats().bind_errors_, nullptr});
    connection_->connect();
  }

  connection_->write(buffer, false);
}

uint64_t TcpStatsdSink::TlsSink::usedBuffer() const {
  ASSERT(current_slice_mem_ != nullptr);
  return current_slice_mem_ - reinterpret_cast<char*>(current_buffer_slice_.mem_);
}

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
