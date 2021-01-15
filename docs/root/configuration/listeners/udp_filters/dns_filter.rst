.. _config_udp_listener_filters_dns_filter:

DNS 过滤器
============

.. attention::

  DNS 过滤器还在开发中，应该被视为 alpha 且不是生产可用。

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig>`
* 这个过滤器应该使用名字 *envoy.filters.udp_listener.dns_filter* 来配置

概览
--------

DNS 过滤器允许 Envoy 将转发的 DNS 查询解析为任何已配置域的权威服务器。过滤器的配置指定 Envoy 会应答的名称和地址，以及从外部向未知域发送查询所需的配置。

过滤器支持本地和外部 DNS 解析。如果名称查找与静态配置的域或配置的群集名称不匹配，Envoy 可以将查询引至外部解析器以寻求结果。用户可以选择指定 Envoy 将用于外部解析的 DNS 服务器。用户可以通过省略客户端配置对象来禁用外部 DNS 解析。

过滤器支持 :ref:`per-filter 配置
<envoy_v3_api_msg_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig>`。
下面的示例配置说明了如何使用过滤器。

示例配置
-------------

.. code-block:: yaml

  listener_filters:
    name: envoy.filters.udp.dns_filter
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig"
      stat_prefix: "dns_filter_prefix"
      client_config:
        resolution_timeout: 5s
        upstream_resolvers:
        - socket_address:
            address: "8.8.8.8"
            port_value: 53
        - socket_address:
            address: "8.8.4.4"
            port_value: 53
        max_pending_lookups: 256
      server_config:
        inline_dns_table:
          known_suffixes:
          - suffix: "domain1.com"
          - suffix: "domain2.com"
          - suffix: "domain3.com"
          - suffix: "domain4.com"
          - suffix: "domain5.com"
          virtual_domains:
            - name: "www.domain1.com"
              endpoint:
                address_list:
                  address:
                  - 10.0.0.1
                  - 10.0.0.2
            - name: "www.domain2.com"
              endpoint:
                address_list:
                  address:
                    - 2001:8a:c1::2800:7
            - name: "www.domain3.com"
              endpoint:
                address_list:
                  address:
                  - 10.0.3.1
            - name: "www.domain4.com"
              endpoint:
                cluster_name: cluster_0
            - name: "voip.domain5.com"
              endpoint:
                service_list:
                  services:
                    - service_name: "sip"
                      protocol: { number: 6 }
                      ttl: 86400s
                      targets:
                      - host_name: "primary.voip.domain5.com"
                        priority: 10
                        weight: 30
                        port: 5060
                      - host_name: "secondary.voip.domain5.com"
                        priority: 10
                        weight: 20
                        port: 5060
                      - host_name: "backup.voip.domain5.com"
                        priority: 10
                        weight: 10
                        port: 5060


在此示例中，Envoy 被配置为响应四个域的客户端查询。对于任何其他查询，它将上游转发到外部解析器。过滤器将返回与输入查询类型匹配的地址。如果查询针对的是 A 类型的记录，并且没有配置 A 记录，则 Envoy 将不返回任何地址并设置适当的响应代码。相反，如果有匹配查询类型的记录，则返回每个已配置的地址。对于 AAAA 记录也是如此。仅支持 A、AAAA 和 SRV 记录。如果过滤器解析其他记录类型的查询，则过滤器将立即响应，指示不支持该类型。过滤器还可以将对 DNS 名称的查询重定向到集群的各个端点。配置中的“www.domain4.com”对此进行了证明。除地址列表外，集群名称是 DNS 名称的有效端点。

DNS 过滤器还支持响应对服务记录的查询。“domain5.com”的记录说明了支持响应 SRV 记录所必需的配置。除非目标是集群，否则配置中填充的目标名称必须是全限定域名。对于非集群目标，必须在 DNS 过滤器表中定义每个引用的目标名称，以便 Envoy 可以解析目标主机的 IP 地址。对于集群，Envoy 将为每个集群端点返回一个地址。

每个服务记录的协议都可以通过名称或编号来定义。按照示例中的配置，过滤器将成功响应对“_sip._tcp.voip.domain5.com”的 SRV 记录请求。如果指定了数值，Envoy 会尝试将数字解析为名称。协议的字符串值将在出现时使用。在服务和协议之前加一个下划线，以遵守 RFC 中概述的约定。

过滤器还可以通过外部 DNS 表来自定义其域配置。静态配置中出现相同实体可以作为 JSON 或 YAML 的形式存储在独立的文件中，并使用 :ref:`external_dns_table DataSource <envoy_api_msg_core.DataSource>` 指令进行引用：

外部 DnsTable 配置示例
-----------------------------

.. code-block:: yaml

    listener_filters:
      name: "envoy.filters.udp.dns_filter"
      typed_config:
        '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig'
        stat_prefix: "my_prefix"
        server_config:
          external_dns_table:
            filename: "/home/ubuntu/configs/dns_table.json"

在该文件中，表可以如下定义：

DnsTable JSON 配置
--------------------------

.. code-block:: json

  {
    "known_suffixes": [
      { "suffix": "suffix1.com" },
      { "suffix": "suffix2.com" }
    ],
    "virtual_domains": [
      {
        "name": "www.suffix1.com",
        "endpoint": {
          "address_list": {
            "address": [ "10.0.0.1", "10.0.0.2" ]
          }
        }
      },
      {
        "name": "www.suffix2.com",
        "endpoint": {
          "address_list": {
            "address": [ "2001:8a:c1::2800:7" ]
          }
        }
      }
    ]
  }


通过使用此配置，可以将 DNS 响应与 Envoy 配置分开配置。
