.. _config_overview_bootstrap:

Bootstrap configuration
-----------------------

To use the xDS API, it's necessary to supply a bootstrap configuration file. This
provides static server configuration and configures Envoy to access :ref:`dynamic
configuration if needed <arch_overview_dynamic_config>`. This is supplied on the command-line via
the :option:`-c` flag, i.e.:

.. code-block:: console

  ./envoy -c <path to config>.{json,yaml,pb,pb_text}

where the extension reflects the underlying config representation.

The :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` message is the root of the
configuration. A key concept in the :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>`
message is the distinction between static and dynamic resources. Resources such
as a :ref:`Listener <envoy_v3_api_msg_config.listener.v3.Listener>` or :ref:`Cluster
<envoy_v3_api_msg_config.cluster.v3.Cluster>` may be supplied either statically in
:ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>` or have
an xDS service such as :ref:`LDS
<config_listeners_lds>` or :ref:`CDS <config_cluster_manager_cds>` configured in
:ref:`dynamic_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>`.
