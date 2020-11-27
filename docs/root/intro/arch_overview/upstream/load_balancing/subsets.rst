.. _arch_overview_load_balancer_subsets:

负载均衡器子集
---------------------

Envoy 可被配置为根据主机所带的元数据将上游集群内的主机分为若干子集。然后，路由可以指定主机必须匹配的元数据，以便被负载平衡器选择，并可选择回退到预先设定的某些主机（包括任何主机）。

负载均衡器子集使用集群指定的负载均衡策略。由于事先不知道上游主机，所以子集可能无法使用原始目的地策略。子集与区域感知路由兼容，但要注意子集的使用很容易违反上述的最小主机匹配条件。

如果子集是 :ref:`已配置的 <envoy_v3_api_field_config.cluster.v3.Cluster.lb_subset_config>`，并且路由指定没有元数据或没有匹配元数据的子集存在，则负载均衡器子集启动其后备策略。默认策略是 ``NO_FALLBACK``，在这种情况下，请求会失败，就像集群没有主机一样。相反，``ANY_ENDPOINT`` 后备策略会在集群中的所有主机上进行负载均衡，而不考虑主机元数据。最后，``DEFAULT_SUBSET`` 后备策略会使负载均衡在符合特定元数据集的主机之间进行。通过特定的子集选择器配置，可以覆盖后备策略。

子集必须被预定义，以使负载均衡器子集能够有效地选择正确的主机子集。每个定义都是一组键，这就转化为零个或多个子集。从概念上讲，每台主机如果具有定义中所有键的元数据值，就会被添加到特定于其键值对的子集中。如果没有主机可以匹配到所有键，则该定义就不会产生子集。可以提供多个定义，如果一个主机与多个定义相匹配，它可以出现在多个子集中。

在路由过程中，路由的元数据匹配配置被用来寻找特定的子集。如果有一个子集与路由指定的键和值完全一致，则使用该子集进行负载均衡。否则，将使用后备策略。因此，集群的子集配置必须包含一个与给定路由具有相同键的定义，才能实现子集负载均衡。

子集可以被配置为每个子集中只有一台主机，这可以用于类似于 :ref:`Maglev <arch_overview_load_balancing_types_maglev>` 或 :ref:`哈希环 <arch_overview_load_balancing_types_ring_hash>` 的用例，例如基于 Cookie 的负载均衡，这类场景通常需要实现即使在新主机添加到集群后也要选择相同的主机。如果启用了 :ref:`single_host_per_subset <envoy_v3_api_field_config.cluster.v3.Cluster.LbSubsetConfig.LbSubsetSelector.single_host_per_subset>`，则端点配置更改可能会使用较少的 CPU。

只有在使用 :ref:`ClusterLoadAssignments <envoy_v3_api_msg_config.endpoint.v3.ClusterLoadAssignment>` 定义主机时，才支持主机元数据。ClusterLoadAssignments 可通过 EDS 或集群的 :ref:`load_assignment <envoy_v3_api_field_config.cluster.v3.Cluster.load_assignment>` 字段获得。用于子集负载均衡的主机元数据必须放在过滤器名称 ``"envoy.lb"`` 下。同样，路由元数据匹配条件使用 ``"envoy.lb"`` 过滤器名称。主机元数据可以是分层的（例如，顶层键的值可以是一个结构化的值或列表），但负载均衡器子集只比较顶层键和值。因此，当使用结构化值时，只有当主机的元数据中出现相同的结构化值时，路由的匹配条件才会匹配成功。

最后，需要注意的是，负载均衡器子集是无法使用 :ref:`CLUSTER_PROVIDED <envoy_v3_api_enum_value_config.cluster.v3.Cluster.LbPolicy.CLUSTER_PROVIDED>` 负载均衡器策略的。

示例
^^^^^^^^

我们将在示例中使用简单的元数据，所有的值都是字符串。假定在一个集群中有以下的主机：

======  ======================
主机    元数据
======  ======================
host1   v: 1.0, stage: prod
host2   v: 1.0, stage: prod
host3   v: 1.1, stage: canary
host4   v: 1.2-pre, stage: dev
======  ======================

集群可以像这样启用负载均衡器子集：

::

  ---
  name: cluster-name
  type: EDS
  eds_cluster_config:
    eds_config:
      path: '.../eds.conf'
  connect_timeout:
    seconds: 10
  lb_policy: LEAST_REQUEST
  lb_subset_config:
    fallback_policy: DEFAULT_SUBSET
    default_subset:
      stage: prod
    subset_selectors:
    - keys:
      - v
      - stage
    - keys:
      - stage
      fallback_policy: NO_FALLBACK

下表描述了一些路由和它们应用到集群的结果。通常，匹配标准将用于匹配请求的特定方面，如路径或请求头信息的路由。

======================  =============  ======================================================================
匹配条件                 选择目标         选择原因
======================  =============  ======================================================================
stage: canary           host3          匹配到主机子集
v: 1.2-pre, stage: dev  host4          匹配到主机子集
v: 1.0                  host1, host2   后备：没有子集可以单独匹配键值 "v" 
other: x                host1, host2   后备：没有子集可以匹配到键值  "other"
(none)                  host1, host2   后备：没有子集匹配请求
stage: test             空集群          后备策略被覆盖为 "NO_FALLBACK" 
======================  =============  ======================================================================

元数据匹配条件也可以在路由的加权集群上指定。所选加权集群的元数据匹配条件整合并覆盖路由的匹配条件：

====================  ===============================  ====================
路由匹配条件            加权集群匹配条件                    最终匹配条件
====================  ===============================  ====================
stage: canary         stage: prod                      stage: prod
v: 1.0                stage: prod                      v: 1.0, stage: prod
v: 1.0, stage: prod   stage: canary                    v: 1.0, stage: canary
v: 1.0, stage: prod   v: 1.1, stage: canary            v: 1.1, stage: canary
(none)                v: 1.0                           v: 1.0
v: 1.0                (none)                           v: 1.0
====================  ===============================  ====================


带元数据的主机示例
**************************

一个含有主机元数据的 EDS ``LbEndpoint``：

::

  ---
  endpoint:
    address:
      socket_address:
        protocol: TCP
        address: 127.0.0.1
        port_value: 8888
  metadata:
    filter_metadata:
      envoy.lb:
        version: '1.0'
        stage: 'prod'


带元数据匹配条件的路径示例
************************************

一个具备元数据匹配条件的 RDS ``Route``：

::

  ---
  match:
    prefix: /
  route:
    cluster: cluster-name
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: '1.0'
          stage: 'prod'
