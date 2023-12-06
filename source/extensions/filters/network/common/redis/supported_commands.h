#pragma once

#include <set>
#include <string>
#include <vector>

#include "source/common/common/macros.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

struct SupportedCommands {
  /**
   * @return commands which hash to a single server
   */
  static const absl::flat_hash_set<std::string>& simpleCommands() {
    CONSTRUCT_ON_FIRST_USE(
        absl::flat_hash_set<std::string>, "append", "bitcount", "bitfield", "bitpos", "decr",
        "decrby", "dump", "expire", "expireat", "geoadd", "geodist", "geohash", "geopos",
        "georadius_ro", "georadiusbymember_ro", "get", "getbit", "getrange", "getset", "hdel",
        "hexists", "hget", "hgetall", "hincrby", "hincrbyfloat", "hkeys", "hlen", "hmget", "hmset",
        "hscan", "hset", "hsetnx", "hstrlen", "hvals", "incr", "incrby", "incrbyfloat", "lindex",
        "linsert", "llen", "lmove", "lpop", "lpush", "lpushx", "lrange", "lrem", "lset", "ltrim",
        "persist", "pexpire", "pexpireat", "pfadd", "pfcount", "psetex", "pttl", "restore", "rpop",
        "rpush", "rpushx", "sadd", "scard", "set", "setbit", "setex", "setnx", "setrange",
        "sismember", "smembers", "spop", "srandmember", "srem", "sscan", "strlen", "ttl", "type",
        "watch", "zadd", "zcard", "zcount", "zincrby", "zlexcount", "zpopmin", "zpopmax", "zrange",
        "zrangebylex", "zrangebyscore", "zrank", "zrem", "zremrangebylex", "zremrangebyrank",
        "zremrangebyscore", "zrevrange", "zrevrangebylex", "zrevrangebyscore", "zrevrank", "zscan",
        "zscore");
  }

  /**
   * @return commands which hash on the fourth argument
   */
  static const absl::flat_hash_set<std::string>& evalCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "eval", "evalsha");
  }

  /**
   * @return commands which are sent to multiple servers and coalesced by summing the responses
   */
  static const absl::flat_hash_set<std::string>& hashMultipleSumResultCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "del", "exists", "touch", "unlink");
  }

  /**
   * @return commands which handle Redis transactions.
   */
  static const absl::flat_hash_set<std::string>& transactionCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "multi", "exec", "discard");
  }

  /**
   * @return auth command
   */
  static const std::string& auth() { CONSTRUCT_ON_FIRST_USE(std::string, "auth"); }

  /**
   * @return mget command
   */
  static const std::string& mget() { CONSTRUCT_ON_FIRST_USE(std::string, "mget"); }

  /**
   * @return mset command
   */
  static const std::string& mset() { CONSTRUCT_ON_FIRST_USE(std::string, "mset"); }

  /**
   * @return ping command
   */
  static const std::string& ping() { CONSTRUCT_ON_FIRST_USE(std::string, "ping"); }

  /**
   * @return time command
   */
  static const std::string& time() { CONSTRUCT_ON_FIRST_USE(std::string, "time"); }

  /**
   * @return quit command
   */
  static const std::string& quit() { CONSTRUCT_ON_FIRST_USE(std::string, "quit"); }

  /**
   * @return commands which alters the state of redis
   */
  static const absl::flat_hash_set<std::string>& writeCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "append", "bitfield", "decr", "decrby",
                           "del", "discard", "exec", "expire", "expireat", "eval", "evalsha",
                           "geoadd", "hdel", "hincrby", "hincrbyfloat", "hmset", "hset", "hsetnx",
                           "incr", "incrby", "incrbyfloat", "linsert", "lmove", "lpop", "lpush",
                           "lpushx", "lrem", "lset", "ltrim", "mset", "multi", "persist", "pexpire",
                           "pexpireat", "pfadd", "psetex", "restore", "rpop", "rpush", "rpushx",
                           "sadd", "set", "setbit", "setex", "setnx", "setrange", "spop", "srem",
                           "zadd", "zincrby", "touch", "zpopmin", "zpopmax", "zrem",
                           "zremrangebylex", "zremrangebyrank", "zremrangebyscore", "unlink");
  }

  static bool isReadCommand(const std::string& command) {
    return !writeCommands().contains(command);
  }
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
