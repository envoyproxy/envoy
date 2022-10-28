#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
/**
 * Test listener filter which sets the ALPN to a manually configured string.
 */
class TestListenerFilter : public Network::ListenerFilter {
public:
  TestListenerFilter() = default;

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    absl::MutexLock m(&alpn_lock_);
    ASSERT(!alpn_.empty());
    cb.socket().setRequestedApplicationProtocols({alpn_});
    alpn_.clear();
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  }
  size_t maxReadBytes() const override { return 0; }

  static void setAlpn(std::string alpn) {
    absl::MutexLock m(&alpn_lock_);
    alpn_ = alpn;
  }

private:
  static absl::Mutex alpn_lock_;
  static std::string alpn_;
};

/**
 * Test TCP listener filter.
 */
class TestTcpListenerFilter : public Network::ListenerFilter {
public:
  TestTcpListenerFilter(const uint32_t drain_bytes) : drain_bytes_(drain_bytes) {}

  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks&) override {
    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override {
    // Drain some bytes when onData.
    if (drain_bytes_ && drain_bytes_ <= buffer.rawSlice().len_) {
      buffer.drain(drain_bytes_);
    }
    return Network::FilterStatus::Continue;
  }

  // Returning a non-zero number.
  size_t maxReadBytes() const override { return 1024; }

private:
  const uint32_t drain_bytes_;
};

/**
 * Test UDP listener filter.
 */
class TestUdpListenerFilter : public Network::UdpListenerReadFilter {
public:
  TestUdpListenerFilter(Network::UdpReadFilterCallbacks& callbacks)
      : UdpListenerReadFilter(callbacks) {}

  // Network::UdpListenerReadFilter callbacks
  Network::FilterStatus onData(Network::UdpRecvData&) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    return Network::FilterStatus::Continue;
  }
};

} // namespace Envoy
