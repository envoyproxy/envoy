Fixed the QUIC-LB connection ID generator (``envoy.quic.connection_id_generator.quic_lb``) accepting
an SDS update that changed ``encryption_key`` without changing ``configuration_version``, while its
workers kept using the previous key until Envoy restarted. The new key is now applied, with a warning:
connection IDs already issued under the previous key may no longer be routed to this server by a load
balancer holding the new one, which changing ``configuration_version`` with the key avoids. A secret
update that keeps the key and configuration version is now a no-op.
