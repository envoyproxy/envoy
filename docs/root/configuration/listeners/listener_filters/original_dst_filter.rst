.. _config_listener_filters_original_dst:

Original Destination
====================

Linux
-----

Original destination listener filter reads the SO_ORIGINAL_DST socket option set when a connection
has been redirected by an iptables REDIRECT target, or by an iptables TPROXY target in combination
with setting the listener's :ref:`transparent <envoy_v3_api_field_config.listener.v3.Listener.transparent>` option.

Windows
-------

Original destination listener filter reads the SO_ORIGINAL_DST socket option set when a connection has been redirected by an
`HNS <https://docs.microsoft.com/en-us/virtualization/windowscontainers/container-networking/architecture#container-network-management-with-host-network-service>`_
policy applied to a container endpoint. For this filter to work the
:ref:`traffic_direction <envoy_v3_api_field_config.listener.v3.Listener.traffic_direction>` must be set
on the listener. This means that a separate listener is needed to handle inbound and outbound traffic.

Redirection is not available for use with all types of network traffic. The types of packets that are supported for redirection are shown in the following list:

* TCP/IPv4
* UDP
* Raw UDPv4 without the header include option
* Raw ICMP

For more info see `Using Bind or Connect Redirection <https://docs.microsoft.com/en-us/windows-hardware/drivers/network/using-bind-or-connect-redirection>`_

.. note::

    At the time of writing (February 2021) the OS support for original destination is only available through the
    `Windows insider program <https://insider.windows.com/en-us/for-developers>`_.
    The feature will be fully supported in the upcoming Windows Server release, see
    `Windows Server Release info <https://docs.microsoft.com/en-us/windows-server/get-started/windows-server-release-info>`_.

Later processing in Envoy sees the restored destination address as the connection's local address,
rather than the address at which the listener is listening at. Furthermore, :ref:`an original
destination cluster <arch_overview_service_discovery_types_original_destination>` may be used to
forward HTTP requests or TCP connections to the restored destination address.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.original_dst.v3.OriginalDst``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.listener.original_dst.v3.OriginalDst>`
