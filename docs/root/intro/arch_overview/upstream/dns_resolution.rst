.. _arch_overview_dns_resolution:

DNS Resolution
==============

Many Envoy components resolve DNS: different cluster types (
:ref:`strict dns <arch_overview_service_discovery_types_strict_dns>`,
:ref:`logical dns <arch_overview_service_discovery_types_logical_dns>`);
the :ref:`dynamic forward proxy <arch_overview_http_dynamic_forward_proxy>` system (which is
composed of a cluster and a filter);
the UDP :ref:`dns filter <arch_overview_dns_filter>`, etc.

Envoy provides DNS resolution through pluggable extensions. By default, Envoy uses
`c-ares <https://github.com/c-ares/c-ares>`_ as its DNS resolution library. On Apple OSes, Envoy
additionally offers resolution using Apple-specific APIs via the
``envoy.restart_features.use_apple_api_for_dns_lookups`` runtime feature.

Envoy contains 4 built-in DNS resolver extensions:

* c-ares: :ref:`CaresDnsResolverConfig <envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>`
* Apple (iOS/macOS only): :ref:`AppleDnsResolverConfig <envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>`
* getaddrinfo: :ref:`GetAddrInfoDnsResolverConfig <envoy_v3_api_msg_extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig>`
* Hickory DNS: :ref:`HickoryDnsResolverConfig <envoy_v3_api_msg_extensions.network.dns_resolver.hickory.v3.HickoryDnsResolverConfig>`

  A pure Rust DNS resolver built on the `Hickory DNS <https://github.com/hickory-dns/hickory-dns>`_
  library. It supports standard DNS (UDP/TCP), DNS-over-TLS (DoT), DNS-over-HTTPS (DoH), and
  DNSSEC validation. The resolver runs asynchronously on its own Tokio runtime threads, separate
  from Envoy's event loop, which means DNS resolution does not block the dispatcher thread.
  Hickory DNS is integrated via the dynamic modules framework.

For an example of a built-in DNS typed configuration, see the
:ref:`HTTP filter configuration documentation <config_http_filters_dynamic_forward_proxy>`.

The c-ares-based DNS resolver emits the following statistics rooted in the ``dns.cares`` stats tree:

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    resolve_total, Counter, Number of DNS queries
    pending_resolutions, Gauge, Number of pending DNS queries
    not_found, Counter, Number of DNS queries that returned ``NXDOMAIN`` or ``NODATA`` response
    get_addr_failure, Counter, Number of general failures during DNS queries
    timeouts, Counter, Number of DNS queries that resulted in a timeout
    reinits, Counter, Number of c-ares channel reinitializations

The Apple-based DNS resolver emits the following statistics rooted in the ``dns.apple`` stats tree:

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    connection_failure, Counter, Number of failed attempts to connect to the DNS server
    get_addr_failure, Counter, Number of general failures when calling GetAddrInfo API
    network_failure, Counter, Number of failures due to network connectivity
    processing_failure, Counter, Number of failures when processing data from the DNS server
    socket_failure, Counter, Number of failed attempts to obtain a file descriptor to the socket to the DNS server
    timeout, Counter, Number of queries that resulted in a timeout

The Hickory DNS resolver emits the following statistics rooted in the ``dns.hickory`` stats tree:

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    resolve_total, Counter, Number of completed DNS queries
    pending_resolutions, Gauge, Number of DNS queries currently in flight
    not_found, Counter, Number of DNS queries that returned ``NXDOMAIN`` or ``NODATA`` responses
    get_addr_failure, Counter, Number of general failures during DNS queries
    timeouts, Counter, Number of DNS queries that resulted in a timeout

.. note::

   The getaddrinfo resolver does not currently emit resolver-specific statistics.
