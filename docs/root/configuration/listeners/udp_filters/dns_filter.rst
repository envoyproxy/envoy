.. _config_udp_listener_filters_dns_filter:

DNS Filter
==========

.. attention::

  DNS Filter is under active development and should be considered alpha and not production ready.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.dns_filter.v3.DnsFilterConfig>`

Overview
--------

The DNS filter allows Envoy to resolve forward DNS queries as an authoritative server for any
configured domains. The filter's configuration specifies the names and addresses for which Envoy
will answer as well as the configuration needed to send queries externally for unknown domains.

The filter supports local and external DNS resolution. If a lookup for a name does not match a
statically configured domain, or a provisioned cluster name, Envoy can refer the query to an
external resolver for an answer. Users have the option of specifying the DNS servers that Envoy
will use for external resolution. Users can disable external DNS resolution by omitting the
client configuration object.

The filter supports :ref:`per-filter configuration
<envoy_v3_api_msg_extensions.filters.udp.dns_filter.v3.DnsFilterConfig>`.
An Example configuration follows that illustrates how the filter can be used.

Example Configuration
---------------------

.. code-block:: yaml

  listener_filters:
    name: envoy.filters.udp.dns_filter
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig"
      stat_prefix: "dns_filter_prefix"
      client_config:
        resolution_timeout: 5s
        dns_resolution_config:
          dns_resolver_options:
            use_tcp_for_dns_lookups: false
            no_default_search_domain: false
          resolvers:
          - socket_address:
              address: "8.8.8.8"
              port_value: 53
          - socket_address:
              address: "8.8.4.4"
              port_value: 53
        max_pending_lookups: 256
      server_config:
        inline_dns_table:
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


In this example, Envoy is configured to respond to client queries for five domains. For any
other query, it will forward upstream to external resolvers. The filter will return an address
matching the input query type. If the query is for type A records and no A records are configured,
Envoy will return no addresses and set the response code appropriately. Conversely, if there are
matching records for the query type, each configured address is returned. This is also true for
AAAA records. Only A, AAAA, and SRV records are supported. If the filter parses queries for other
record types, the filter immediately responds indicating that the type is not supported. The
filter can also redirect a query for a DNS name to the endpoints of a cluster. "www.domain4.com"
in the configuration demonstrates this. Along with an address list, a cluster name is a valid
endpoint for a DNS name.

The DNS filter also supports responding to queries for service records. The records for "domain5.com"
illustrate the configuration necessary to support responding to SRV records. The target name
populated in the configuration must be fully qualified domain names, unless the target is a cluster.
For non-cluster targets, each referenced target name must be defined in the DNS Filter table so that
Envoy can resolve the target hosts' IP addresses. For a cluster, Envoy will return an address for
each cluster endpoint.

Each service record's protocol can be defined by a name or number. As configured in the example,
the filter will successfully respond to SRV record requests for "_sip._tcp.voip.domain5.com". If a
numerical value is specified, Envoy will attempt to resolve the number to a name. String values for
protocols are used as they appear. An underscore is prepended to both the service and protocol to
adhere to the convention outlined in the RFC.

The filter can also consume its domain configuration from an external DNS table. The same entities
appearing in the static configuration can be stored as JSON or YAML in a separate file and referenced
using the :ref:`external_dns_table DataSource <envoy_v3_api_msg_config.core.v3.datasource>` directive:

Example External DnsTable Configuration
---------------------------------------

.. code-block:: yaml

    listener_filters:
      name: "envoy.filters.udp.dns_filter"
      typed_config:
        '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig'
        stat_prefix: "my_prefix"
        server_config:
          external_dns_table:
            filename: "/home/ubuntu/configs/dns_table.json"

In the file, the table can be defined as follows:

DnsTable JSON Configuration
---------------------------

.. code-block:: json

  {
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


By utilizing this configuration, the DNS responses can be configured separately from the Envoy
configuration.
