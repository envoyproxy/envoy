.. _arch_overview_redis:

Redis
=======

Envoy 可以作为 Redis 代理，在集群的实例间对命令进行分区。在这种模式下， Envoy 的目标是在一致性前提下维护可用性和分区容错。这是 Envoy 和 `Redis 集群
<https://redis.io/topics/cluster-spec>`_ 关键差异。 Envoy 被设计为尽力而为的缓存，意味着它不会试图协调不一致的数据或者保持全局集群成员一致视图。它还支持基于不同的访问模式，驱逐或隔离需求，将命令从不同的工作负载路由到不同的上游集群。

Redis 项目中提供了与 Redis 分区相关的全面参考。请查看 "`分区: 如何在多个 Redis 实例间分片？
<https://redis.io/topics/partitioning>`_"。

**Envoy Redis 特性**:

* Redis 协议 <https://redis.io/topics/protocol>_ 编解码
* 基于哈希的分区
* Ketama 分布式一致性哈希算法
* 命令统计详情
* 主动和被动健康检查
* 哈希标记
* 路由前缀
* 下游客户端和上游服务器分别进行身份验证
* 针对所有请求或写请求监控
* 管控 :ref:`读请求路由 <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.read_policy>`. 仅适用于 Redis 集群。

**计划的未来增强功能**：

* 额外的时间统计
* 熔断
* 对分散的命令进行请求折叠
* 复制
* 内置重试
* 追踪

.. _arch_overview_redis_configuration:

配置
-------------

过滤器配置细节，请查看 Redis 代理过滤器 :ref:`配置参考 <config_network_filters_redis_proxy>`。

一致的集群定义应该配置 :ref:`环哈希负载均衡 <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>`。

如果使用:ref:`主动健康检查 <arch_overview_health_checking>`，集群需要配置 :ref:` 自定义健康检查 <envoy_v3_api_field_config.core.v3。HealthCheck.custom_health_check>` 其配置为 :ref:`Redis 健康检查 <config_health_checkers_redis>`。

如果使用被动健康检查，需要配置 :ref: `异常检测 <arch_overview_outlier_detection>`。

为了进行被动健康检查，需要将连接超时、命令超时和连接关闭都映射到 5xx 响应，而来自 Redis 的所有其他响应都视为成功。


.. _arch_overview_redis_cluster_support:

Redis 集群支持（实验性）
----------------------------------------

Envoy 目前为 `Redis 集群 <https://redis.io/topics/cluster-spec>`_ 提供实验性支持。

服务可以使用以任意语言实现的非集群 Redis 客户端连接到代理，就像它是一个单节点 Redis 实例一样。Envoy 代理将跟踪集群拓扑，并根据 `规范 <https://redis.io/topics/cluster-spec>`_ 向集群中正确的 Redis 节点发送命令。还可以将高级功能（例如从副本中读取）添加到 Envoy 代理中，而不用更新每种语言的 Redis 客户端。

Envoy 通过定期向集群中的随机节点发送 cluster slot `cluster slots <https://redis.io/commands/cluster-slots>`_ 命令来跟踪群集的拓扑，并维护以下信息：

* 已知节点列表
* 每个分片的主节点
* 集群节点的增加或减少

更多拓扑配置细节，请查看 Redis 集群 :ref:`v2 API 参考 <envoy_v3_api_msg_extensions.clusters.redis.v3.RedisClusterConfig>`.

每个 Redis 集群都有它自己的额外信息统计树，根路径为 *cluster.<name>.redis_cluster.* 包含以下统计:

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  max_upstream_unknown_connections_reached, Counter, 重定向达到连接池 max_upstream_unknown_connections 限制后，上游到未知主机连接未创建的总数
  upstream_cx_drained, Counter, 连接关闭前已退出的活跃请求的上游连接总数
  upstream_commands.upstream_rq_time, Histogram, 所有类型请求的上游请求时间直方图

.. _arch_overview_redis_cluster_command_stats:

每个集群的命令统计可以通过设置 :ref:`enable_command_stats <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.enable_command_stats>` 开启:

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  upstream_commands.[command].success, Counter, 特定 Redis 命令的请求成功总数
  upstream_commands.[command].failure, Counter, 特定 Redis 命令的请求失败或撤销总数
  upstream_commands.[command].total, Counter, 特定 Redis 命令的请求总数（成功和失败数总和）
  upstream_commands.[command].latency, Histogram, 特定 Redis 命令的延迟

支持的命令
------------------

在协议级别支持管道。MULTI （事务块）不支持。尽可能使用管道以获得最佳性能。

在命令级别，Envoy 仅支持可以被可靠地哈希到一台服务器的命令。只有 AUTH 和 PING 命令例外。如果下游配置了密码，Envoy 将在本地处理 AUTH，并且在配置了密码之后，在身份认证成功之前，Envoy 不会处理任何其他命令。如果上游为整个集群配置了密码，Envoy 将在连接到上游服务器后透明地发送 AUTH 命令。Envoy 会立即为 PING 命令返回 PONG。PING 命令不接受参数。所有其他支持的参数必须包含一个 key。除了执行失败的情况外，所有支持的命令功能与原始 Redis 命令完全一致。

每个命令的使用详情请参考官方文档 `Redis 命令参考 <https://redis.io/commands>`_。

.. csv-table::
  :header: Command, Group
  :widths: 1, 1

  AUTH, Authentication
  PING, Connection
  DEL, Generic
  DUMP, Generic
  EXISTS, Generic
  EXPIRE, Generic
  EXPIREAT, Generic
  PERSIST, Generic
  PEXPIRE, Generic
  PEXPIREAT, Generic
  PTTL, Generic
  RESTORE, Generic
  TOUCH, Generic
  TTL, Generic
  TYPE, Generic
  UNLINK, Generic
  GEOADD, Geo
  GEODIST, Geo
  GEOHASH, Geo
  GEOPOS, Geo
  GEORADIUS_RO, Geo
  GEORADIUSBYMEMBER_RO, Geo
  HDEL, Hash
  HEXISTS, Hash
  HGET, Hash
  HGETALL, Hash
  HINCRBY, Hash
  HINCRBYFLOAT, Hash
  HKEYS, Hash
  HLEN, Hash
  HMGET, Hash
  HMSET, Hash
  HSCAN, Hash
  HSET, Hash
  HSETNX, Hash
  HSTRLEN, Hash
  HVALS, Hash
  LINDEX, List
  LINSERT, List
  LLEN, List
  LPOP, List
  LPUSH, List
  LPUSHX, List
  LRANGE, List
  LREM, List
  LSET, List
  LTRIM, List
  RPOP, List
  RPUSH, List
  RPUSHX, List
  EVAL, Scripting
  EVALSHA, Scripting
  SADD, Set
  SCARD, Set
  SISMEMBER, Set
  SMEMBERS, Set
  SPOP, Set
  SRANDMEMBER, Set
  SREM, Set
  SSCAN, Set
  ZADD, Sorted Set
  ZCARD, Sorted Set
  ZCOUNT, Sorted Set
  ZINCRBY, Sorted Set
  ZLEXCOUNT, Sorted Set
  ZRANGE, Sorted Set
  ZRANGEBYLEX, Sorted Set
  ZRANGEBYSCORE, Sorted Set
  ZRANK, Sorted Set
  ZREM, Sorted Set
  ZREMRANGEBYLEX, Sorted Set
  ZREMRANGEBYRANK, Sorted Set
  ZREMRANGEBYSCORE, Sorted Set
  ZREVRANGE, Sorted Set
  ZREVRANGEBYLEX, Sorted Set
  ZREVRANGEBYSCORE, Sorted Set
  ZREVRANK, Sorted Set
  ZPOPMIN, Sorted Set
  ZPOPMAX, Sorted Set
  ZSCAN, Sorted Set
  ZSCORE, Sorted Set
  APPEND, String
  BITCOUNT, String
  BITFIELD, String
  BITPOS, String
  DECR, String
  DECRBY, String
  GET, String
  GETBIT, String
  GETRANGE, String
  GETSET, String
  INCR, String
  INCRBY, String
  INCRBYFLOAT, String
  MGET, String
  MSET, String
  PSETEX, String
  SET, String
  SETBIT, String
  SETEX, String
  SETNX, String
  SETRANGE, String
  STRLEN, String

失败模式
-------------

如果 Redis 抛出错误，我们会将这个错误作为响应传递给命令。Envoy 将 Redis 返回的响应与错误数据类型视为正常响应，并将它传递给调用者。

Envoy 也可以在响应中生成它自己的错误返回给客户端。

.. csv-table::
  :header: 错误, 含义
  :widths: 1, 1

  no upstream host, "没有上游主机，环状哈希负载均衡器在为键选择环形位置上没有可用的健康主机"
  upstream failure, "上游失败，后端未在超时期限内响应或关闭连接"
  invalid request, "无效请求，因为数据类型或长度的原因，命令在命令拆分器的第一阶段被拒绝"
  unsupported command, "不支持的命令，该命令 Envoy 不能识别，所以不能被哈希到一个后端主机，无法响应"
  finished with n errors, "返回多个错误，分段的命令将会组合多个响应(例如 DEL 命令)，如果收到任何错误，将返回接收到的错误总数"
  upstream protocol error, "上游协议错误，分段命令收到一个意外的数据类型或后端响应的数据不符合 Redis 协议"
  wrong number of arguments for command, "命令参数数量错误，Envoy 中的命令参数数量检查未通过"
  NOAUTH Authentication required., "NOAUTH 需要认证，因下游设置了认证密码且客户端没有认证成功，导致命令被拒绝"
  ERR invalid password, "ERR 密码无效，因密码无效导致命令认证失败"
  "ERR Client sent AUTH, but no password is set", "ERR 客户端发送了 AUTH，但未设置密码，收到认证命令，但没有配置下游认证密码"


使用 MGET 时，每个无法获取的 key 将会生成一个错误响应。例如：如果我们获取 5 个 key 的值，其中 2 个出现后端超时，我们将会获得每个值的错误响应信息。


.. code-block:: none

  $ redis-cli MGET a b c d e
  1) "alpha"
  2) "bravo"
  3) (error) upstream failure
  4) (error) upstream failure
  5) "echo"
