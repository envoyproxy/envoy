Load balancing policies
=======================

Extendable load balancing policies can be
:ref:`configured <envoy_v3_api_field_config.cluster.v3.Cluster.load_balancing_policy>` separately for each cluster, also by calling
:ref:`APIs <envoy_v3_api_config_load_balancer_policies>`.

Developers can implement custom, configurable policies in C++.

.. note::

  In the past, Envoy used an
  :ref:`enum <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>`
  to specify load balancing policies. This ``enum`` is still supported for
  backward compatibility, but deprecated.

  :ref:`extendable load balancing policies <envoy_v3_api_config_load_balancer_policies>`
  should be used instead.

Taking :ref:`random load balancing policy <envoy_v3_api_msg_extensions.load_balancing_policies.random.v3.Random>`
as an example:

.. code-block:: yaml

    name: example_cluster
    type: STRICT_DNS
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: example_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: example.com
                port_value: 80
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.random
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.random.v3.Random
