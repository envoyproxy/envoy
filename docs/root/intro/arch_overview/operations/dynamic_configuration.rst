.. _arch_overview_dynamic_config:

xDS 配置 API 概述
==============================

Envoy 的架构支持使用不同类型的配置管理方法。部署中采用的方法将取决于实现者的需求。简单部署可以通过全静态配置来实现。更复杂的部署可以逐步地添加更复杂的动态配置，缺点是实现者必须提供一个或多个基于 gRPC/REST的外部配置提供 API。这些 API 统称为 :ref:`"xDS" <xds_protocol>` (* 服务发现)。本文档概述了当前可用的选项。

* 顶级配置 :ref:`参考 <config>`。
* :ref:`参考配置 <install_ref_configs>`。
* Envoy :ref:`v3 API 概述 <config_overview>`。
* :ref:`xDS API 端点 <config_overview_management_server>`。

全静态
------------

在全静态配置中，实现者提供一组:ref:`监听器 <config_listeners>` （和 :ref:`过滤器链 <envoy_v3_api_msg_config.listener.v3.Filter>`）、:ref:`集群 <config_cluster_manager>` 等。动态主机发现仅能通过基于 DNS 的 :ref:`服务发现 <arch_overview_service_discovery>`。配置重载必须通过内置的 :ref:`热重启 <arch_overview_hot_restart>` 机制进行。

虽然简单，但可以使用静态配置和优雅的热重启来创建相当复杂的部署。

.. _arch_overview_dynamic_config_eds:

EDS
---

:ref:`端点发现服务（EDS） API <arch_overview_service_discovery_types_eds>` 提供了一种更先进的机制，通过它 Envoy 可以发现上游集群的成员。在静态配置基础上，EDS 允许 Envoy 部署避开 DNS 的限制（响应中的最大记录等），并使用更多信息用于负载均衡和路由（例如，灰度发布、区域等）。

.. _arch_overview_dynamic_config_cds:

CDS
---

:ref:`集群发现服务（CDS） API <config_cluster_manager_cds>` 是 Envoy 的一种机制，在路由期间可以用来发现使用的上游集群。Envoy 将优雅地添加、更新和删除由 API 指定的集群。该 API 允许实现者构建一个拓扑，在这个拓扑中，Envoy 不需要在初始配置时就知道所有上游群集。通常，当使用 CDS 进行 HTTP 路由时（但没有路由发现服务），实现者将利用路由器将请求转发到 :ref:`HTTP request header <envoy_v3_api_field_config.route.v3.RouteAction.cluster_header>` 中指定的集群。尽管可以通过指定全静态集群来使用不带 EDS 的 CDS，i但是对于用 CDS 指定的集群，我们依旧建议使用 EDS API。在内部更新集群定义时，操作是优雅的。但是，所有现有的连接池都将被排空并重新连接。EDS 不受此限制。当通过 EDS 添加和删除主机时，群集中的现有主机不受影响。

.. _arch_overview_dynamic_config_rds:

RDS
---

ref:`路由发现服务（RDS） API <config_http_conn_man_rds>` 是 Envoy 的一种机制，可以在运行时发现用于 HTTP 连接管理器过滤器的整个路由配置。路由配置将优雅地交换，而不会影响现有的请求。该 API 与 EDS 和 CDS 一起使用时，允许执行者构建复杂的路由拓扑（ :ref:`流量转移 <config_http_conn_man_route_table_traffic_splitting>`、蓝/绿部署等），除了获取新的 Envoy 二进制文件外，不需要任何 Envoy 重启。

VHDS
----

:ref:`虚拟主机发现服务 <config_http_conn_man_vhds>` 允许根据需要分别请求属于路由配置的虚拟主机和路由配置。该 API 通常用于路由配置中存在大量虚拟主机的部署中。

SRDS
----

:ref:`作用域路由发现服务（SRDS） API <arch_overview_http_routing_route_scope>` 允许将路由表划分成多个部分。该 API 通常用于具有大量路由表的 HTTP 路由部署中，在这种情况下，简单的线性搜索是不可行的。 

.. _arch_overview_dynamic_config_lds:

LDS
---

:ref:`监听器发现服务（LDS） API <config_listeners_lds>` 是 Envoy 的一种机制，可以在运行时发现整个监听器。这包括所有的过滤器堆栈，并包含带有内嵌到 :ref:`RDS <config_http_conn_man_rds>` 应用的 HTTP 过滤器。将 LDS 添加到组合中，几乎可以动态配置 Envoy 的每个方面。仅在非常少见的配置更改（管理员、追踪驱动程序等）、证书更换或二进制更新时才需要热重启。

SDS
---

:ref:`加密发现服务（SDS） API <config_secret_discovery_service>` 是 Envoy 的一种机制，通过该机制，Envoy 可以为其监听器发现加密数据（证书加私钥、TLS session 密钥），以及配置对等证书验证逻辑（可信根证书、撤销等）。

RTDS
----

:ref:`运行时发现服务（RTDS） API <config_runtime_rtds>` 允许通过 xDS API 来获取 :ref:`运行时 <config_runtime>`。这可能有利于文件系统层，或说是对文件系统层的增强。

ECDS
----

:ref:`扩展配置发现服务（ECDS） API <config_overview_extension_discovery>` 允许对独立于监听器的配置进行扩展（如 HTTP 过滤器配置）。当构建更适合与主控制平面分开的系统（例如 WAF、故障测试等）时，此功能很有用。

聚合的 xDS ("ADS")
----------------------

EDS、CDS 等都是单独的服务，具有不同的 REST/gRPC 服务名称，例如 StreamListeners、StreamSecrets。对于那些希望能够控制不同类型的资源到达 Envoy 顺序的用户，聚合 xDS 是一个不错的选择，它是一个单一的 gRPC 服务，单个 gRPC 流中承载所有资源类型（仅 gRPC 支持 ADS）。
:ref:`有关 ADS 的更多详细信息 <config_overview_ads>` 。

.. _arch_overview_dynamic_config_delta:

增量 gRPC xDS
--------------

标准的 xDS 是每个更新都必须包含所有的资源，而更新中没有资源信息意味着该资源已消失。Envoy 支持 xDS（包括 ADS）的”增量（delta）”变体，其更新仅包含添加/更改/删除的资源信息。增量 xDS 是一个新的协议，其请求/响应 API 与 SotW 不同。
:ref:`有关增量（Delta）的更多详细信息 <config_overview_delta>` 。
