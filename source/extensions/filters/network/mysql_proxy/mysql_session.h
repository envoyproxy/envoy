#pragma once
#include <cstdint>

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

class MysqlSession : Logger::Loggable<Logger::Id::filter> {
public:
  enum class State {
    MYSQL_INIT = 0,
    MYSQL_CHALLENGE_REQ = 1,
    MYSQL_CHALLENGE_RESP_41 = 2,
    MYSQL_CHALLENGE_RESP_320 = 3,
    MYSQL_SSL_PT = 4,
    MYSQL_AUTH_SWITCH_REQ = 5,
    MYSQL_AUTH_SWITCH_REQ_OLD = 6,
    MYSQL_AUTH_SWITCH_RESP = 7,
    MYSQL_AUTH_SWITCH_MORE = 8,
    MYSQL_REQ_RESP = 9,
    MYSQL_REQ = 10,
    MYSQL_NOT_HANDLED = 11,
    MYSQL_ERROR = 12,
  };

  MysqlSession() { cinfo_ = {}; };
  uint64_t GetId() { return cinfo_.id; }
  void SetId(uint64_t id) { cinfo_.id = id; }
  void SetState(MysqlSession::State state) { cinfo_.state = state; }
  MysqlSession::State GetState() { return cinfo_.state; }
  int GetExpectedSeq() { return cinfo_.expected_seq; }
  void SetExpectedSeq(int seq) { cinfo_.expected_seq = seq; }

private:
  struct ConnInfo {
    uint64_t id;
    MysqlSession::State state;
    int expected_seq;
  };
  ConnInfo cinfo_;
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
