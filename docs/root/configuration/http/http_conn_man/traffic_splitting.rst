.. _config_http_conn_man_route_table_traffic_splitting:

流量转移/拆分
=================

.. contents::
  :local:

Envoy 的路由器可以将流量拆分为跨两个或多个上游集群的虚拟主机中的路由。有两种常见的用例。

1. 版本升级：路由的流量逐渐从一个群集转移到另一个群集。:ref:`流量转移 <config_http_conn_man_route_table_traffic_splitting_shift>` 部分更详细地描述了这种情况。

2. A/B 测试或多变量测试：同一个服务的 ``两个或更多的版本`` 被同时测试。运行同一服务的不同版本的集群之间的路由流量必须*拆分*。:ref:`流量拆分 <config_http_conn_man_route_table_traffic_splitting_split>` 部分更详细地描述了这种情况。

.. _config_http_conn_man_route_table_traffic_splitting_shift:

两个上游间的流量转移
---------------------

路由配置中的 :ref:`运行时 <envoy_v3_api_field_config.route.v3.RouteMatch.runtime_fraction>` 对象确定选择特定路由（并因此选择其集群）的可能性。通过使用 *runtime_fraction* 配置，虚拟主机中的路由的流量会逐渐的从一个集群转移到另一个。看以下示例配置，在 Envoy 配置文件中声明的名字为 ``helloworld`` 的服务有两个版本 ``helloworld_v1`` 和 ``helloworld_v2``。

.. code-block:: yaml

  virtual_hosts:
     - name: www2
       domains:
       - '*'
       routes:
         - match:
             prefix: /
             runtime_fraction:
               default_value:
                 numerator: 50
                 denominator: HUNDRED
               runtime_key: routing.traffic_shift.helloworld
           route:
             cluster: helloworld_v1
         - match:
             prefix: /
           route:
             cluster: helloworld_v2


Envoy 使用 :ref:`首个匹配 <config_http_conn_man_route_table_route_matching>` 策略来匹配路由。
如果路由具有 runtime_fraction 对象，则将基于 runtime_fraction :ref:`值 <envoy_v3_api_field_config.route.v3.RouteMatch.runtime_fraction>` 额外匹配请求（如果未指定值，则为默认值）。因此，通过在上面的示例中连续地放置路由，并在第一条路由中指定 runtime_fraction 对象，可以通过更改 runtime_fraction 值来实现流量转移。下面是实现转移所需的大概操作顺序。

1. 首先，将 ``routing.traffic_shift.helloworld`` 设置为 ``100``，这样对于 ``helloworld`` 虚拟主机的所有请求，都将与 v1 路由匹配，并由 ``helloworld_v1`` 集群提供服务。
2. 要开始转移流量到 ``helloworld_v2`` 集群，将 ``routing.traffic_shift.helloworld`` 的值设置为 ``0 < x < 100``。例如设置为 ``90``，此时到 ``helloworld`` 虚拟主机的每 10 个请求中有 1 个请求将与 v1 路由不匹配，并将进入 v2 路由。
3. 逐渐减少 ``routing.traffic_shift.helloworld`` 的值以便更大比例的请求与 v2 路由匹配。
4. 当 ``routing.traffic_shift.helloworld`` 的值设置为 ``0`` 时，没有到 ``helloworld`` 虚拟主机的请求与 v1 路由匹配。所有流量此时会进入 v2 路由，由 ``helloworld_v2`` 集群提供服务。


.. _config_http_conn_man_route_table_traffic_splitting_split:

跨多个上游的流量分流
----------------------

重新看 ``helloworld`` 示例，现在有三个（v1、v2 和 v3）而不是两个版本。``weighted_clusters`` 选项可以用来指定每个上游集群的权重来在三个版本之间平均分配流量（比如 ``33%、33%、34%``）。

与前面的示例不同，一个 :ref:`路由
<envoy_v3_api_msg_config.route.v3.Route>` 条目就够了。路由中的 :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` 配置块可用于指定多个上游集群以及每个上游集群的权重。

.. code-block:: yaml

  virtual_hosts:
     - name: www2
       domains:
       - '*'
       routes:
         - match: { prefix: / }
           route:
             weighted_clusters:
               runtime_key_prefix: routing.traffic_split.helloworld
               clusters:
                 - name: helloworld_v1
                   weight: 33
                 - name: helloworld_v2
                   weight: 33
                 - name: helloworld_v3
                   weight: 34


默认情况下，权重的和必须等于 100。在 V2 版本的 API 中，:ref:`权重总和 <envoy_v3_api_field_config.route.v3.WeightedCluster.total_weight>` 默认为 100，但可以修改以允许更细的粒度。

可以通过运行时变量 ``routing.traffic_split.helloworld.helloworld_v1``、``routing.traffic_split.helloworld.helloworld_v2`` 和
``routing.traffic_split.helloworld.helloworld_v3`` 对每个集群的权重进行动态地调整。
