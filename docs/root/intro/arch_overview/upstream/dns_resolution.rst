.. _arch_overview_dns_resolution:

DNS Resolution
==============

Many Envoy components resolve DNS: different cluster types (
:ref:`strict dns <arch_overview_service_discovery_types_strict_dns>`,
:ref:`logical dns <arch_overview_service_discovery_types_logical_dns>`);
the :ref:`dynamic forward proxy <arch_overview_http_dynamic_forward_proxy>` system (which is
composed of a cluster and a filter);
the udp :ref:`dns filter <arch_overview_dns_filter>`, etc.
Envoy uses `c-ares <https://github.com/c-ares/c-ares>`_ as a third party DNS resolution library.
On Apple OSes Envoy additionally offers resolution using Apple specific APIs via the
``envoy.restart_features.use_apple_api_for_dns_lookups`` runtime feature.

Envoy provides DNS resolution through extensions, and contains 3 built-in extensions:

1) c-ares: :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>`

2) Apple (iOS/macOS only): :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>`

3) getaddrinfo: :ref:`GetAddrInfoDnsResolverConfig <envoy_v3_api_msg_extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig>`

For an example of a built-in DNS typed configuration see the :ref:`HTTP filter configuration documentation <config_http_filters_dynamic_forward_proxy>`.

The c-ares based DNS Resolver emits the following stats rooted in the ``dns.cares`` stats tree:

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    resolve_total, Count, Number of DNS queries
    pending_resolutions, Gauge, Number of pending DNS queries
    not_found, Counter, Number of DNS queries that returned NXDOMAIN or NODATA response
    timeout, Counter, Number of DNS queries that resulted in timeout
    get_addr_failure, Counter, Number of general failures during DNS quries

The Apple-based DNS Resolver emits the following stats rooted in the ``dns.apple`` stats tree:

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    connection_failure, Counter, Number of failed attempts to connect to the DNS server
    get_addr_failure, Counter, Number of general failures when calling GetAddrInfo API
    network_failure, Counter, Number of failures due to network connectivity
    processing_failure, Counter, Number of failures when processing data from the DNS server
    socket_failure, Counter, Number of failed attempts to obtain a file descriptor to the socket to the DNS server
    timeout, Counter, Number of queries that resulted in a timeout
