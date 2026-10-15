Extended :ref:`wait_for_warm_on_init <envoy_v3_api_field_config.cluster.v3.Cluster.wait_for_warm_on_init>`
to all cluster types. Setting it to ``false`` now opts the cluster out of pausing CDS while it warms, so a
cluster that warms slowly or indefinitely (for example one waiting on an SDS secret with
``initial_fetch_timeout: 0s``) no longer holds back the CDS ACK and other cluster updates.