.. _config_listener_filters_original_dst:

Original Destination
====================

Linux
===============

Original destination listener filter reads the SO_ORIGINAL_DST socket option set when a connection
has been redirected by an iptables REDIRECT target, or by an iptables TPROXY target in combination
with setting the listener's :ref:`transparent <envoy_v3_api_field_config.listener.v3.Listener.transparent>` option.

Windows
===============

Original destination listener filter reads the SO_ORIGINAL_DST socket option set when a connection
has been redirected by an HNS policy applied to a container endpoint. For this fiter to work the 
:ref:`traffic_direction <envoy_v3_api_field_config.listener.v3.Listener.traffic_direction>` must be set
on the listener. This means that a seperate listener is needed to handle inbound and outbound traffic.

.. note::

    At the time of writing (February 2021) the OS support for original destination is only available through the `Windows insider program <https://insider.windows.com/en-us/for-developers>`.
    The feature will be fully supported in Windows Server 20H2 and eventually supported in Windows Server 2019 `Windows Server Release info <https://docs.microsoft.com/en-us/windows-server/get-started/windows-server-release-info>`. 

Later processing in Envoy sees the restored destination address as the connection's local address,
rather than the address at which the listener is listening at. Furthermore, :ref:`an original
destination cluster <arch_overview_service_discovery_types_original_destination>` may be used to
forward HTTP requests or TCP connections to the restored destination address.

* :ref:`v3 API reference <envoy_v3_api_field_config.listener.v3.ListenerFilter.name>`
* This filter should be configured with the name *envoy.filters.listener.original_dst*.
