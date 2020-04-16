.. _config_udp_listener_filters_dns_filter:

DNS Filter
==========

.. attention::

  DNS Filter is under active development and should be considered alpha and not production ready.

* :ref:`v2 API reference <envoy_api_msg_config.filter.udp.dns_filter.v2alpha.DnsFilterConfig>`
* This filter should be configured with the name *envoy.filters.udp_listener.dns_filter*

Overview
--------

The DNS filter allows Envoy to respond to DNS queries as an authoritative server for any configured
domains. The filter's configuration specifies the names and addresses for which Envoy will answer
as well as the configuration needed to send queries externally for unknown domains.

The filter supports :ref:`per-filter configuration
<envoy_api_msg_config.filter.udp.dns_filter.v2alpha.DnsFilterConfig>`.
An Example configuration follows that illustrates how the filter can be used.

Example Configuration
---------------------

.. code-block:: yaml

  listener_filters:
    name: "envoy.filters.udp.dns_filter"
    typed_config:
      "@type": "type.googleapis.com/envoy.config.filter.udp.dns_filter.v2alpha.DnsFilterConfig"
      stat_prefix: "dns_filter_prefix"
      server_config:
        inline_dns_table:
          external_retry_count: 3
          known_suffixes:
            - suffix: "domain1.com"
            - suffix: "domain2.com"
            - suffix: "domain3.com"
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


In this example, Envoy is configured to respond to client queries for three domains. For any
other query, it will forward upstream to external resolvers.
