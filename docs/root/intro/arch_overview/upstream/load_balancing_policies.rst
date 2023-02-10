Load balancing policies
=======================

Extendable load balancing policies could be configured for each cluster by
:ref:`load balancer policy <envoy_v3_api_field_config.cluster.v3.Cluster.load_balancing_policy>`.
And the developer can implement a custom load balancing policy and configured it.


See built-in load balancing policies by :ref:`APIs <envoy_v3_api_config_load_balancer_policies>`.


:ref:`Enum based load balancing policies <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>`
will be suupported for backward compatibility and marked as deprecated. The new extendable load
balancing policies should be used as a priority if the related policies are implemented.


Use :ref:`random load balancing policy <envoy_v3_api_msg_extensions.load_balancing_policies.random.v3.Random>`
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
