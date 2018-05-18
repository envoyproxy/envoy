#pragma once

#include "envoy/config/health_checker/mysql/v2/mysql.pb.validate.h"
#include "envoy/network/filter.h"

#include "common/network/filter_impl.h"
#include "common/upstream/health_checker_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace MySQLHealthChecker {

/**
 * MySQL health checker implementation. Connects, waits for the server greeting then sends a login
 * request followed by a QUIT packet to properly close the session.
 */
class MySQLHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  MySQLHealthChecker(const Upstream::Cluster& cluster,
                     const envoy::api::v2::core::HealthCheck& config,
                     const envoy::config::health_checker::mysql::v2::MySQL& mysql_config,
                     Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random,
                     Upstream::HealthCheckEventLoggerPtr&& event_logger);

protected:
  envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v2alpha::HealthCheckerType::MYSQL;
  }

private:
  typedef std::vector<uint8_t> Packet;

  struct MySQLActiveHealthCheckSession;

  struct MySQLSessionCallbacks : public Network::ConnectionCallbacks,
                                 public Network::ReadFilterBaseImpl {
    MySQLSessionCallbacks(MySQLActiveHealthCheckSession& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    MySQLActiveHealthCheckSession& parent_;
  };

  struct MySQLActiveHealthCheckSession : public ActiveHealthCheckSession {
    enum class Phase { Greeting, Reply, Over };

    MySQLActiveHealthCheckSession(MySQLHealthChecker& parent, const Upstream::HostSharedPtr& host)
        : ActiveHealthCheckSession(parent, host), parent_(parent) {}
    ~MySQLActiveHealthCheckSession();

    void onData(Buffer::Instance& data);
    void onEvent(Network::ConnectionEvent event);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    bool parseMySqlServerGreetingPacket(Buffer::Instance& data);
    void writeMySqlLoginRequestPacket(Buffer::Instance& buffer);
    void writeMySqlQuitPacket(Buffer::Instance& buffer);
    bool parseMySqlOkPacket(Buffer::Instance& data);

    MySQLHealthChecker& parent_;
    Network::ClientConnectionPtr client_;
    std::shared_ptr<MySQLSessionCallbacks> session_callbacks_;
    Phase phase_ = Phase::Over;
  };

  typedef std::unique_ptr<MySQLActiveHealthCheckSession> MySQLActiveHealthCheckSessionPtr;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<MySQLActiveHealthCheckSession>(*this, host);
  }

  const std::string user_;
};

} // namespace MySQLHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
