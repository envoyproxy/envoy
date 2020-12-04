.. _install_sandboxes_redis_filter:

Redis 过滤器
==============

在这个示例中，我们会展示如何将 :ref:`Redis 过滤器 <config_network_filters_redis_proxy>` 和 Envoy 代理一起使用。Envoy 代理配置包含一个 Redis 过滤器，它会将出口请求路由到 Redis 服务器。

运行沙盒
~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3： 构建沙盒
*******************

终端 1

.. code-block:: console

  $ pwd
  envoy/examples/redis
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                   Command               State                             Ports
  ------------------------------------------------------------------------------------------------------------------
  redis_proxy_1   /docker-entrypoint.sh /bin       Up      10000/tcp, 0.0.0.0:1999->1999/tcp, 0.0.0.0:8001->8001/tcp
  redis_redis_1   docker-entrypoint.sh redis       Up      0.0.0.0:6379->6379/tcp

步骤 4： 发出 Redis 命令
**************************

使用你喜欢的 Redis 客户端发出 Redis 命令，比如 ``redis-cli``，接着验证命令是否是通过 Envoy 路由。

终端 1

.. code-block:: console

  $ redis-cli -h localhost -p 1999 set foo foo
  OK
  $ redis-cli -h localhost -p 1999 set bar bar
  OK
  $ redis-cli -h localhost -p 1999 get foo
  "foo"
  $ redis-cli -h localhost -p 1999 get bar
  "bar"

步骤 5： 验证出口统计
***********************

跳转到 ``http://localhost:8001/stats?usedonly&filter=redis.egress_redis.command`` 并验证一下统计信息：

.. code-block:: none

  redis.egress_redis.command.get.total: 2
  redis.egress_redis.command.set.total: 2
