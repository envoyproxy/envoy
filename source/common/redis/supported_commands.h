#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Redis {

struct SupportedCommands {
  /**
   * @return commands which hash to a single server
   */
  static const std::vector<std::string>& allToOneCommands() {
    // TODO(danielhochman): DEL should hash to multiple servers and is only added for testing single
    // key deletes
    static const std::vector<std::string>* commands =
        new std::vector<std::string>{"append",
                                     "bitcount",
                                     "bitfield",
                                     "bitpos",
                                     "decr",
                                     "decrby",
                                     "del",
                                     "dump",
                                     "expire",
                                     "expireat",
                                     "geoadd",
                                     "geodist",
                                     "geohash",
                                     "geopos",
                                     "get",
                                     "getbit",
                                     "getrange",
                                     "getset",
                                     "hdel",
                                     "hexists",
                                     "hget",
                                     "hgetall",
                                     "hincrby",
                                     "hincrbyfloat",
                                     "hkeys",
                                     "hlen",
                                     "hmget",
                                     "hmset",
                                     "hscan",
                                     "hset",
                                     "hsetnx",
                                     "hstrlen",
                                     "hvals",
                                     "incr",
                                     "incrby",
                                     "incrbyfloat",
                                     "lindex",
                                     "linsert",
                                     "llen",
                                     "lpop",
                                     "lpush",
                                     "lpushx",
                                     "lrange",
                                     "lrem",
                                     "lset",
                                     "ltrim",
                                     "persist",
                                     "pexpire",
                                     "pexpireat",
                                     "psetex",
                                     "pttl",
                                     "restore",
                                     "rpop",
                                     "rpush",
                                     "rpushx",
                                     "sadd",
                                     "scard",
                                     "set",
                                     "setbit",
                                     "setex",
                                     "setnx",
                                     "setrange",
                                     "sismember",
                                     "smembers",
                                     "spop",
                                     "srandmember",
                                     "srem",
                                     "sscan",
                                     "strlen",
                                     "ttl",
                                     "type",
                                     "zadd",
                                     "zcard",
                                     "zcount",
                                     "zincrby",
                                     "zlexcount",
                                     "zrange",
                                     "zrangebylex",
                                     "zrangebyscore",
                                     "zrank",
                                     "zrem",
                                     "zremrangebylex",
                                     "zremrangebyrank",
                                     "zremrangebyscore",
                                     "zrevrange",
                                     "zrevrangebylex",
                                     "zrevrangebyscore",
                                     "zrevrank",
                                     "zscan",
                                     "zscore"};
    return *commands;
  }
  // TODO(danielhochman): static vector of commands that hash to multiple servers: mget, mset, del
};

} // namespace Redis
} // namespace Envoy
