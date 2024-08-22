.. _start_quick_start_dynamic_control_plane:

Configuration: Dynamic from control plane
=========================================

These instructions are slightly more complex as you must also set up a control plane to provide Envoy with its
configuration.

There are a number of control planes compatible with Envoy's API such as `Gloo <https://docs.solo.io/gloo/latest/>`_
or `Istio <https://istio.io>`_.

You may also wish to explore implementing your own control plane, in which case the
`Go Control Plane <https://github.com/envoyproxy/go-control-plane>`_ provides a reference implementation
that is a good place to start.

At a minimum, you will need to start Envoy configured with the following sections:

- :ref:`node <start_quick_start_dynamic_node>` to uniquely identify the proxy node.
- :ref:`dynamic_resources <start_quick_start_dynamic_dynamic_resources>` to tell Envoy which configurations should be updated dynamically
- :ref:`static_resources <start_quick_start_dynamic_static_resources>` to specify where Envoy should retrieve its configuration from.

You can also add an :ref:`admin <start_quick_start_admin>` section if you wish to monitor Envoy or
retrieve stats or configuration information.

The following sections walk through the dynamic configuration provided in the
:download:`demo dynamic control plane configuration file <_include/envoy-dynamic-control-plane-demo.yaml>`.

.. _start_quick_start_dynamic_node:

``node``
--------

The :ref:`node <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` should specify ``cluster`` and ``id``.

.. literalinclude:: _include/envoy-dynamic-control-plane-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-5
    :emphasize-lines: 1-3
    :caption: :download:`envoy-dynamic-control-plane-demo.yaml <_include/envoy-dynamic-control-plane-demo.yaml>`

.. _start_quick_start_dynamic_dynamic_resources:

``dynamic_resources``
---------------------

The :ref:`dynamic_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` specify
the configuration to load dynamically, and the :ref:`cluster <start_quick_start_dynamic_static_resources>`
to connect for dynamic configuration updates.

In this example, the configuration is provided by the ``xds_cluster`` configured below.

.. literalinclude:: _include/envoy-dynamic-control-plane-demo.yaml
    :language: yaml
    :linenos:
    :lines: 3-19
    :lineno-start: 3
    :emphasize-lines: 3-15
    :caption: :download:`envoy-dynamic-control-plane-demo.yaml <_include/envoy-dynamic-control-plane-demo.yaml>`

.. _start_quick_start_dynamic_static_resources:

``static_resources``
--------------------

Here we specify the :ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>`
to retrieve dynamic configuration from.

The ``xds_cluster`` is configured to query a control plane at http://my-control-plane:18000 .

.. literalinclude:: _include/envoy-dynamic-control-plane-demo.yaml
    :language: yaml
    :linenos:
    :lines: 17-38
    :lineno-start: 17
    :emphasize-lines: 3-20
    :caption: :download:`envoy-dynamic-control-plane-demo.yaml <_include/envoy-dynamic-control-plane-demo.yaml>`
