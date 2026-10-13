Upstream HTTP/3 connections now honor :ref:`auto_host_sni
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.auto_host_sni>` and send
the upstream host's hostname as SNI. The server certificate must match that hostname. Hostnames
that are not valid SNI, such as IP addresses, are ignored. Clusters with HTTP/3 enabled in
``auto_config`` also look up the hostname in the alternate protocols cache. This behavioral change
can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.quic_upstream_auto_host_sni`` to ``false``.
