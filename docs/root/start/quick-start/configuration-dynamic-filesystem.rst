.. _start_quick_start_dynamic_filesystem:

Configuration: Dynamic from filesystem
======================================

You can start Envoy with dynamic configuration that uses files as a form of
:ref:`Extensible discovery service (xDS) <xds_protocol>`.

When the files are changed on the filesystem, Envoy will automatically update its configuration.

At a minimum, you will need to start Envoy configured with the following sections:

- :ref:`node <start_quick_start_dynamic_fs_node>` to uniquely identify the proxy node.
- :ref:`dynamic_resources <start_quick_start_dynamic_fs_dynamic_resources>` to tell Envoy where to find its
  dynamic configuration.

For the given example you will also need two dynamic configuration files:

- :ref:`lds.yaml <start_quick_start_dynamic_fs_dynamic_lds>` for listeners.
- :ref:`cds.yaml <start_quick_start_dynamic_fs_dynamic_cds>` for clusters.

You can also add an :ref:`admin <start_quick_start_dynamic_fs_admin>` section if you wish to monitor Envoy or
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

.. _start_quick_start_dynamic_fs_dynamic_resources:

``dynamic_resources``
---------------------

The :ref:`dynamic_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` specify
where to load dynamic configuration.

In this example, the configuration is provided by the ``yaml`` files set below.

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 3-11
    :lineno-start: 3
    :emphasize-lines: 3-7

.. _start_quick_start_dynamic_fs_dynamic_lds:

``resources`` - Listener discovery service (LDS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example of a :download:`dynamic LDS file <_include/envoy-dynamic-lds-demo.yaml>`,
configures an ``HTTP`` listener on port ``10000``.

All paths are matched and routed to the ``service_envoyproxy_io`` cluster.

.. literalinclude:: _include/envoy-dynamic-lds-demo.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 5-7, 29

.. _start_quick_start_dynamic_fs_dynamic_cds:

``resources`` - Cluster discovery service (CDS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In the following example of a :download:`dynamic CDS file <_include/envoy-dynamic-cds-demo.yaml>`,
the ``service_envoyproxy_io`` :ref:`cluster <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>`
proxies over ``TLS`` to https://www.envoyproxy.io.

.. literalinclude:: _include/envoy-dynamic-cds-demo.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 8, 14-15

.. _start_quick_start_dynamic_fs_admin:

``admin``
---------

Configuring the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` section is
the same as for :ref:`static configuration <start_quick_start_static_admin>`.

Enabling the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` interface with
dynamic configuration, allows you to use the :ref:`config_dump <operations_admin_interface_config_dump>`
endpoint to see how Envoy is currently configured.

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 9-16
    :lineno-start: 9
    :emphasize-lines: 3-8
