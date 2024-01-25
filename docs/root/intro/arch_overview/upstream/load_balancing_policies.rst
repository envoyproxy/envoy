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

.. literalinclude:: /_configs/load-balancing/load-balancing-policies.yaml
    :language: yaml
    :lines: 26-33
    :emphasize-lines: 3-8
    :linenos:
    :lineno-start: 26
    :caption: :download:`load-balancing-policies.yaml </_configs/load-balancing/load-balancing-policies.yaml>`
