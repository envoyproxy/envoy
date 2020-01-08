#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/common/factory_base.h"

#include "test/integration/filter_manager_integration_test.pb.h"
#include "test/integration/filter_manager_integration_test.pb.validate.h"

namespace Envoy {
namespace {

/**
 * Basic traffic throttler that emits a next chunk of the original request/response data
 * on timer tick.
 */
class Throttler {
public:
  Throttler(Event::Dispatcher& dispatcher, std::chrono::milliseconds tick_interval,
            uint64_t max_chunk_length, std::function<void(Buffer::Instance&, bool)> next_chunk_cb)
      : timer_(dispatcher.createTimer([this] { onTimerTick(); })), tick_interval_(tick_interval),
        max_chunk_length_(max_chunk_length), next_chunk_cb_(next_chunk_cb) {}

  /**
   * Throttle given given request/response data.
   */
  void throttle(Buffer::Instance& data, bool end_stream);
  /**
   * Cancel any scheduled activities (on connection close).
   */
  void reset();

private:
  void onTimerTick();

  Buffer::OwnedImpl buffer_{};
  bool end_stream_{};

  const Event::TimerPtr timer_;
  const std::chrono::milliseconds tick_interval_;
  const uint64_t max_chunk_length_;
  const std::function<void(Buffer::Instance&, bool)> next_chunk_cb_;
};

void Throttler::throttle(Buffer::Instance& data, bool end_stream) {
  buffer_.move(data);
  end_stream_ |= end_stream;
  if (!timer_->enabled()) {
    timer_->enableTimer(tick_interval_);
  }
}

void Throttler::reset() { timer_->disableTimer(); }

void Throttler::onTimerTick() {
  Buffer::OwnedImpl next_chunk{};
  if (0 < buffer_.length()) {
    auto chunk_length = max_chunk_length_ < buffer_.length() ? max_chunk_length_ : buffer_.length();
    next_chunk.move(buffer_, chunk_length);
  }
  bool end_stream = end_stream_ && 0 == buffer_.length();
  if (0 < buffer_.length()) {
    timer_->enableTimer(tick_interval_);
  }
  next_chunk_cb_(next_chunk, end_stream);
}

/**
 * Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
 * and WriteFilterCallbacks::injectWriteDataToFilterChain() methods in the context of a timer
 * callback.
 *
 * Emits a next chunk of the original request/response data on timer tick.
 */
class ThrottlerFilter : public Network::Filter, public Network::ConnectionCallbacks {
public:
  ThrottlerFilter(std::chrono::milliseconds tick_interval, uint64_t max_chunk_length)
      : tick_interval_(tick_interval), max_chunk_length_(max_chunk_length) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);

    read_throttler_ = std::make_unique<Throttler>(
        read_callbacks_->connection().dispatcher(), tick_interval_, max_chunk_length_,
        [this](Buffer::Instance& data, bool end_stream) {
          read_callbacks_->injectReadDataToFilterChain(data, end_stream);
        });
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;

    write_throttler_ = std::make_unique<Throttler>(
        write_callbacks_->connection().dispatcher(), tick_interval_, max_chunk_length_,
        [this](Buffer::Instance& data, bool end_stream) {
          write_callbacks_->injectWriteDataToFilterChain(data, end_stream);
        });
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  std::unique_ptr<Throttler> read_throttler_;
  std::unique_ptr<Throttler> write_throttler_;

  const std::chrono::milliseconds tick_interval_;
  const uint64_t max_chunk_length_;
};

// Network::ReadFilter
Network::FilterStatus ThrottlerFilter::onNewConnection() { return Network::FilterStatus::Continue; }

Network::FilterStatus ThrottlerFilter::onData(Buffer::Instance& data, bool end_stream) {
  read_throttler_->throttle(data, end_stream);
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Network::WriteFilter
Network::FilterStatus ThrottlerFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  write_throttler_->throttle(data, end_stream);
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Network::ConnectionCallbacks
void ThrottlerFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    read_throttler_->reset();
    write_throttler_->reset();
  }
}

/**
 * Config factory for ThrottlerFilter.
 */
class ThrottlerFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                         test::integration::filter_manager::Throttler> {
public:
  explicit ThrottlerFilterConfigFactory(const std::string& name) : FactoryBase(name) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filter_manager::Throttler& proto_config,
      Server::Configuration::FactoryContext&) override {
    return [proto_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<ThrottlerFilter>(
          std::chrono::milliseconds(proto_config.tick_interval_ms()),
          proto_config.max_chunk_length()));
    };
  }
};

/**
 * Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
 * and WriteFilterCallbacks::injectWriteDataToFilterChain() methods in the context of
 * ReadFilter::onData() and WriteFilter::onWrite().
 *
 * Calls ReadFilterCallbacks::injectReadDataToFilterChain() /
 * WriteFilterCallbacks::injectWriteDataToFilterChain() to pass data to the next filter
 * byte-by-byte.
 */
class DispenserFilter : public Network::Filter {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  // Pass data to the next filter byte-by-byte.
  void dispense(Buffer::Instance& data, bool end_stream,
                std::function<void(Buffer::Instance&, bool)> next_chunk_cb);

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
};

// Network::ReadFilter
Network::FilterStatus DispenserFilter::onNewConnection() { return Network::FilterStatus::Continue; }

Network::FilterStatus DispenserFilter::onData(Buffer::Instance& data, bool end_stream) {
  dispense(data, end_stream, [this](Buffer::Instance& data, bool end_stream) {
    read_callbacks_->injectReadDataToFilterChain(data, end_stream);
  });
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Network::WriteFilter
Network::FilterStatus DispenserFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  dispense(data, end_stream, [this](Buffer::Instance& data, bool end_stream) {
    write_callbacks_->injectWriteDataToFilterChain(data, end_stream);
  });
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Pass data to the next filter byte-by-byte.
void DispenserFilter::dispense(Buffer::Instance& data, bool end_stream,
                               std::function<void(Buffer::Instance&, bool)> next_chunk_cb) {
  Buffer::OwnedImpl next_chunk{};
  do {
    if (0 < data.length()) {
      next_chunk.move(data, 1);
    }
    next_chunk_cb(next_chunk, end_stream && 0 == data.length());
    next_chunk.drain(next_chunk.length());
  } while (0 < data.length());
}

/**
 * Config factory for DispenserFilter.
 */
class DispenserFilterConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  explicit DispenserFilterConfigFactory(const std::string& name) : name_(name) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<DispenserFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return name_; }

private:
  const std::string name_;
};

} // namespace
} // namespace Envoy
