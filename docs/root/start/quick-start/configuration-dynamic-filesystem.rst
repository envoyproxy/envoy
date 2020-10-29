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

- :ref:`cds.yaml <start_quick_start_dynamic_fs_dynamic_cds>` for clusters.
- :ref:`lds.yaml <start_quick_start_dynamic_fs_dynamic_lds>` for listeners.

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

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 3-11
    :lineno-start: 3
    :emphasize-lines: 3-7

.. _start_quick_start_dynamic_fs_dynamic_lds:

``resources`` - Listeners Discovery Service (LDS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: _include/envoy-dynamic-lds-demo.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 1, 8-9, 28

.. _start_quick_start_dynamic_fs_dynamic_cds:

``resources`` - Clusters Discovery Service (CDS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: _include/envoy-dynamic-cds-demo.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 1, 10, 16-17

.. _start_quick_start_dynamic_fs_admin:

``admin``
---------

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 9-16
    :lineno-start: 9
    :emphasize-lines: 3-8
