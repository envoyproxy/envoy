#pragma once

#include <string>
#include <vector>

#include "common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

struct SupportedCommands {
  /**
   * @return commands which hash to a single server
   */
  static const std::vector<std::string>& simpleCommands() {
    CONSTRUCT_ON_FIRST_USE(
        std::vector<std::string>, "append", "bitcount", "bitfield", "bitpos", "decr", "decrby",
        "dump", "expire", "expireat", "geoadd", "geodist", "geohash", "geopos", "georadius_ro",
        "georadiusbymember_ro", "get", "getbit", "getrange", "getset", "hdel", "hexists", "hget",
        "hgetall", "hincrby", "hincrbyfloat", "hkeys", "hlen", "hmget", "hmset", "hscan", "hset",
        "hsetnx", "hstrlen", "hvals", "incr", "incrby", "incrbyfloat", "lindex", "linsert", "llen",
        "lpop", "lpush", "lpushx", "lrange", "lrem", "lset", "ltrim", "persist", "pexpire",
        "pexpireat", "psetex", "pttl", "restore", "rpop", "rpush", "rpushx", "sadd", "scard", "set",
        "setbit", "setex", "setnx", "setrange", "sismember", "smembers", "spop", "srandmember",
        "srem", "sscan", "strlen", "ttl", "type", "zadd", "zcard", "zcount", "zincrby", "zlexcount",
        "zrange", "zrangebylex", "zrangebyscore", "zrank", "zrem", "zremrangebylex",
        "zremrangebyrank", "zremrangebyscore", "zrevrange", "zrevrangebylex", "zrevrangebyscore",
        "zrevrank", "zscan", "zscore");
  }

  /**
   * @return commands which hash on the fourth argument
   */
  static const std::vector<std::string>& evalCommands() {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, "eval", "evalsha");
  }

  /**
   * @return commands which are sent to multiple servers and coalesced by summing the responses
   */
  static const std::vector<std::string>& hashMultipleSumResultCommands() {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, "del", "exists", "touch", "unlink");
  }

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
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
