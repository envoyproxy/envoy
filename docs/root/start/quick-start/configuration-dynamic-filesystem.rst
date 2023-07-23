.. _start_quick_start_dynamic_filesystem:

Configuration: Dynamic from filesystem
======================================

You can start Envoy with dynamic configuration by using files that implement the :ref:`xDS <xds_protocol>`
protocol.

When the files are changed on the filesystem, Envoy will automatically update its configuration.

.. note::

   Envoy only updates when the configuration file is replaced by a file move, and not when the file is edited in place.

   It is implemented this way to ensure configuration consistency.

At a minimum, you will need to start Envoy configured with the following sections:

- :ref:`node <start_quick_start_dynamic_fs_node>` to uniquely identify the proxy node.
- :ref:`dynamic_resources <start_quick_start_dynamic_fs_dynamic_resources>` to tell Envoy where to find its
  dynamic configuration.

For the given example you will also need two dynamic configuration files:

- :ref:`lds.yaml <start_quick_start_dynamic_fs_dynamic_lds>` for listeners.
- :ref:`cds.yaml <start_quick_start_dynamic_fs_dynamic_cds>` for clusters.

You can also add an :ref:`admin <start_quick_start_admin>` section if you wish to monitor Envoy or
retrieve stats or configuration information.

The following sections walk through the dynamic configuration provided in the
:download:`demo dynamic filesystem configuration file <_include/envoy-dynamic-filesystem-demo.yaml>`.

.. _start_quick_start_dynamic_fs_node:

``node``
--------

The :ref:`node <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` should specify ``cluster`` and ``id``.

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-5
    :emphasize-lines: 1-3
    :caption: :download:`envoy-dynamic-filesystem-demo.yaml <_include/envoy-dynamic-filesystem-demo.yaml>`

.. _start_quick_start_dynamic_fs_dynamic_resources:

``dynamic_resources``
---------------------

The :ref:`dynamic_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` specify
where to load dynamic configuration from.

In this example, the configuration is provided by the ``yaml`` files set below.

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 3-11
    :lineno-start: 3
    :emphasize-lines: 3-7
    :caption: :download:`envoy-dynamic-filesystem-demo.yaml <_include/envoy-dynamic-filesystem-demo.yaml>`

.. _start_quick_start_dynamic_fs_dynamic_lds:

``resources`` - listeners
~~~~~~~~~~~~~~~~~~~~~~~~~

The linked ``lds_config`` should be an implementation of a :ref:`Listener discovery service (LDS) <config_listeners_lds>`.

The following example of a :download:`dynamic LDS file <_include/envoy-dynamic-lds-demo.yaml>`,
configures an ``HTTP`` :ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>`
on port ``10000``.

All domains and paths are matched and routed to the ``service_envoyproxy_io`` cluster.

The ``host`` headers are rewritten to ``www.envoyproxy.io``

.. literalinclude:: _include/envoy-dynamic-lds-demo.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 6-7, 22-23, 26, 28-29
    :caption: :download:`envoy-dynamic-lds-demo.yaml <_include/envoy-dynamic-lds-demo.yaml>`

.. _start_quick_start_dynamic_fs_dynamic_cds:

``resources`` - clusters
~~~~~~~~~~~~~~~~~~~~~~~~

The linked ``cds_config`` should be an implementation of a :ref:`Cluster discovery service (CDS) <config_cluster_manager_cds>`.

In the following example of a :download:`dynamic CDS file <_include/envoy-dynamic-cds-demo.yaml>`,
the ``example_proxy_cluster`` :ref:`cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>`
proxies over ``TLS`` to https://www.envoyproxy.io.

.. literalinclude:: _include/envoy-dynamic-cds-demo.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 11, 17-18, 22-23
    :caption: :download:`envoy-dynamic-cds-demo.yaml <_include/envoy-dynamic-cds-demo.yaml>`

.. seealso::

   :ref:`atomic swaps <config_runtime_symbolic_link_swap>`
      Details about how runtime configuration is updated.
