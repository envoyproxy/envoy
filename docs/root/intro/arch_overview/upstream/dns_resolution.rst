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
