.. _config_network_filters_mysql_proxy:

MySQL 代理
===========

MySQL 代理过滤器解码 MySQL 客户端与服务端之间的有线协议。它解码负载中的 SQL 查询（仅 SQL99 格式）。解码后的信息以动态元数据的形式发出，可以与访问日志过滤器相结合，以获得所访问表的详细信息，以及对每个表执行的操作。

.. attention::

   mysql_proxy 过滤器目前在积极开发中，处于试验阶段。随着时间推移，功能将会得到扩展，同时配置结构也有可能会改变。

.. warning::

   mysql_proxy 过滤器使用 MySQL v5.5 版本进行测试。由于协议实现的不同，该过滤器可能不能与其他 MySQL 版本一起使用。

.. _config_network_filters_mysql_proxy_config:

配置
-------------

MySQL 代理过滤器应该与 TCP 代理过滤器链接，如下配置片段所示：

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.mysql_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.mysql_proxy.v3.MySQLProxy
        stat_prefix: mysql
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: ...


.. _config_network_filters_mysql_proxy_stats:

统计信息
----------

每个配置的 MySQL 代理过滤器都有以 *mysql.<stat_prefix>.* 为根，如下所示的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  auth_switch_request, Counter, 上游服务器请求客户端更换其他认证方法的次数
  decoder_errors, Counter, MySQL 协议解码异常数
  login_attempts, Counter, 登陆尝试次数
  login_failures, Counter, 登录失败次数
  protocol_errors, Counter, 会话中遇到的无序协议消息数
  queries_parse_error, Counter, MySQL 解析失败的查询数
  queries_parsed, Counter, MySQL 解析成功的查询数
  sessions, Counter, 自启动以来的 MySQL 会话数
  upgraded_to_ssl, Counter, 升级为 SSL 的会话/连接数

.. _config_network_filters_mysql_proxy_dynamic_metadata:

动态元数据
----------------

MySQL 过滤器为每个解析的 SQL 查询发出以下动态元数据：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  <table.db>, string, 资源名称使用 *table.db* 格式。如果无法连接数据库，则资源名称默认使用正在访问的表。
  [], list, 字符串列表表示可以对资源执行的操作。操作可以是插入、更新、查询、删除、创建、更新、展示其中之一。

.. _config_network_filters_mysql_proxy_rbac:

对表实施 RBAC 访问
----------------------------------

MySQL 过滤器发出的动态元数据可以与 RBAC 过滤器一起使用，用于控制对数据库的单个表的访问。
下面的配置片段展示了一个 RBAC 过滤器配置的示例，该配置拒绝在 _productdb_ 数据库中的 _catalog_ 表上使用 _update_ 语句的 SQL 查询。

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.mysql_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.mysql_proxy.v3.MySQLProxy
        stat_prefix: mysql
    - name: envoy.filters.network.rbac
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
        stat_prefix: rbac
        rules:
          action: DENY
          policies:
            "product-viewer":
              permissions:
              - metadata:
                  filter: envoy.filters.network.mysql_proxy
                  path:
                  - key: catalog.productdb
                  value:
                    list_match:
                      one_of:
                        string_match:
                          exact: update
              principals:
              - any: true
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: mysql
