.. _start_quick_start:


Quick start
===========

The following instructions walk through starting Envoy as a system daemon or using
the Envoy Docker image.

.. _start_quick_start_version:

Check your Envoy version
------------------------

Once you have :ref:`installed Envoy <install>`, you can check the version information as follows:

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy --version

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm envoyproxy/|envoy_docker_image| --version

.. _start_quick_start_help:

View the Envoy command line options
-----------------------------------

You can view the Envoy :ref:`command line options <operations_cli>` with the ``--help``
flag:

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy --help

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm envoyproxy/|envoy_docker_image| --help

.. _start_quick_start_config:

Run Envoy with the demo configuration
-------------------------------------

The ``-c`` or ``--config-path`` flag tells Envoy the path to its initial configuration.

.. tabs::

   .. tab:: System

      To start Envoy as a system daemon :download:`download the demo configuration <_include/envoy-demo.yaml>`, and start
      as follows:

      .. code-block:: console

	 $ envoy -c envoy-demo.yaml

   .. tab:: Docker

      You can start the Envoy Docker image without specifying a configuration file, and
      it will use the demo config by default.

      .. substitution-code-block:: console

	 $ docker run --rm -d -p 9901:9901 -p 10000:10000 envoyproxy/|envoy_docker_image|

      To specify a custom configuration you can mount the config into the container, and specify the path with ``-c``.

      .. substitution-code-block:: console

	 $ docker run --rm -d -v envoy-custom.yaml:/envoy-custom.yaml -p 9901:9901 -p 10000:10000 envoyproxy/|envoy_docker_image| -c /envoy-custom.yaml

Check Envoy is proxying on http://localhost:10000

.. code-block:: console

   $ curl -v localhost:10000

The Envoy admin endpoint should also be available at http://localhost:9901

.. code-block:: console

   $ curl -v localhost:9901

.. _start_quick_start_override:

Override the default configuration by merging a config file
-----------------------------------------------------------

You can provide a configuration override file using ``--config-yaml`` which will merge with the main
configuration.

Save the following snippet to ``envoy-override.yaml``:

.. code-block:: yaml

   listeners:
     - name: listener_0
       address:
         socket_address:
           port_value: 20000

Next, start the Envoy server using the override configuration.

.. tabs::

   .. tab:: System

      .. code-block:: console

	 $ envoy -c envoy-demo.yaml --config-yaml envoy-override.yaml

   .. tab:: Docker

      .. substitution-code-block:: console

	 $ docker run --rm -d -v envoy-override.yaml:/envoy-override.yaml -p 20000:20000 envoyproxy/|envoy_docker_image| --config-yaml /envoy-override.yaml

Envoy should now be proxying on http://localhost:20000

.. code-block:: console

   $ curl -v localhost:20000

The Envoy admin endpoint should also be available at http://localhost:9901

.. code-block:: console

   $ curl -v localhost:9901

.. _start_quick_start_static:

Static configuration
--------------------

To start Envoy with static configuration, you will need to specify :ref:`listeners <start_quick_start_static_listeners>`
and :ref:`clusters <start_quick_start_static_clusters>` as
:ref:`static_resources <start_quick_start_static_static_resources>`.

You can also add an :ref:`admin <start_quick_start_static_admin>` section if you wish to monitor Envoy
or retrieve stats.

The following sections walk through the static configuration provided in the
:download:`demo configuration file <_include/envoy-demo.yaml>` used as the default in the Envoy Docker container.

.. _start_quick_start_static_static_resources:

Static configuration: ``static_resources``
******************************************

The :ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>` contain
everything that is configured statically when Envoy starts, as opposed to dynamically at runtime.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-3
    :emphasize-lines: 1

.. _start_quick_start_static_listeners:

Static configuration: ``listeners``
***********************************

The example configures a :ref:`listener <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>`
on port ``10000``.

All paths are matched and routed to the ``service_envoyproxy_io``
:ref:`cluster <start_quick_start_static_clusters>`.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-29
    :emphasize-lines: 3-27

.. _start_quick_start_static_clusters:

Static configuration: ``clusters``
**********************************

The ``service_envoyproxy_io`` :ref:`cluster <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>`
proxies over ``TLS`` to https://www.envoyproxy.io.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :lineno-start: 27
    :lines: 27-50
    :emphasize-lines: 3-22

.. _start_quick_start_static_admin:

Static configuration: ``admin``
*******************************

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to enable and configure
the administration server.

The ``address`` key specifies the listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in the demo configuration is ``0.0.0.0:9901``.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :lineno-start: 48
    :lines: 48-55
    :emphasize-lines: 3-8

.. warning::

   You may wish to restrict the network address the admin server listens to in your own deployment.

.. _start_quick_start_dynamic:

Dynamic configuration
---------------------

Setting up Envoy with dynamic configuration is slightly more complex as you must also set up a control plane
to provide Envoy with its configuration.

There are a number of control planes compatible with Envoy's API such as `Gloo <https://docs.solo.io/gloo/latest/>`_
or `Istiod <https://istio.io/latest/docs/ops/deployment/architecture/#istiod>`_.

You may also wish to explore implementing your own control plane, in which case the
`Go Control Plane <https://github.com/envoyproxy/go-control-plane>`_ provides a reference implementation
that is a good place to start.

At a minimum, you will need to start Envoy configured with the following sections:

- :ref:`node <start_quick_start_dynamic_node>` to uniquely identify the proxy node.
- :ref:`dynamic_resources <start_quick_start_dynamic_dynamic_resources>` to tell Envoy which configurations should be updated dynamically
- :ref:`static_resources <start_quick_start_dynamic_static_resources>` to specify where Envoy should retrieve its configuration from.
- :ref:`layered_runtime <start_quick_start_dynamic_layered_runtime>` to persist dynamically-provided configurations.

You can also add an :ref:`admin <start_quick_start_dynamic_admin>` section if you wish to monitor Envoy or
retrieve stats or configuration information.

The following sections walk through the dynamic configuration provided in the
:download:`demo dynamic configuration file <_include/envoy-dynamic-demo.yaml>`.

.. _start_quick_start_dynamic_node:

Dynamic configuration: ``node``
*******************************

The :ref:`node <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` should specify ``cluster`` and ``id``.

.. literalinclude:: _include/envoy-dynamic-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-5
    :emphasize-lines: 1-3

.. _start_quick_start_dynamic_dynamic_resources:

Dynamic configuration: ``dynamic_resources``
********************************************

The :ref:`dynamic_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` specify
the configuration to load dynamically, and the :ref:`cluster <start_quick_start_dynamic_static_resources>`
to connect to for dynamic configuration updates.

In this example, the configuration is provided by the ``xds_cluster`` configured below.

.. literalinclude:: _include/envoy-dynamic-demo.yaml
    :language: yaml
    :linenos:
    :lines: 3-19
    :lineno-start: 3
    :emphasize-lines: 3-15

.. _start_quick_start_dynamic_static_resources:

Dynamic configuration: ``static_resources``
*******************************************

Here we specify the :ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>`
to retrieve dynamic configuration from.

The ``xds_cluster`` is configured to query a control plane at http://my-control-plane:18000 .

.. literalinclude:: _include/envoy-dynamic-demo.yaml
    :language: yaml
    :linenos:
    :lines: 17-35
    :lineno-start: 17
    :emphasize-lines: 3-17

.. _start_quick_start_dynamic_layered_runtime:

Dynamic configuration: ``layered_runtime``
******************************************

A :ref:`layered_runtime <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.layered_runtime>` is
required with an :ref:`rtds_layer <envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.rtds_layer>`
to persist configuration provided by the control plane.

.. literalinclude:: _include/envoy-dynamic-demo.yaml
    :language: yaml
    :linenos:
    :lines: 33-44
    :lineno-start: 33
    :emphasize-lines: 3-10

.. _start_quick_start_dynamic_admin:

Dynamic configuration: ``admin``
********************************

Configuring the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` section is
the same as for :ref:`static configuration <start_quick_start_static_admin>`.

Enabling the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` interface with
dynamic configuration, allows you to use the :ref:`config_dump <operations_admin_interface_config_dump>`
endpoint to see how Envoy is currently configured.

.. literalinclude:: _include/envoy-dynamic-demo.yaml
    :language: yaml
    :linenos:
    :lines: 42-49
    :lineno-start: 42
    :emphasize-lines: 3-8

.. warning::

   You may wish to restrict the network address the admin server listens to in your own deployment.

Next steps
----------

- Learn more about :ref:`using the Envoy Docker image <start_docker>`
- Try out demo configurations in the :ref:`sandboxes <start_sandboxes>`
- Check out the :ref:`configuration generator <start_tools_configuration_generator>` and other
  :ref:`Envoy tools <start_sandboxes>`
