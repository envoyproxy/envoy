Added SSL termination support to the MySQL proxy filter with RSA-mediated ``caching_sha2_password``
authentication. The filter can now terminate downstream TLS connections using the
:ref:`starttls transport socket <envoy_v3_api_msg_extensions.transport_sockets.starttls.v3.StartTlsConfig>`
and transparently mediate MySQL 8.0+ ``caching_sha2_password`` full authentication by performing
RSA public key exchange on behalf of the client. Added a new
:ref:`downstream_ssl <envoy_v3_api_field_extensions.filters.network.mysql_proxy.v3.MySQLProxy.downstream_ssl>`
config option with ``DISABLE``, ``REQUIRE``, and ``ALLOW`` modes.
