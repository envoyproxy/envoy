.. _install_sandboxes_dynamic_config_cp:

Dynamic configuration (control plane)
=====================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

   :ref:`jq <start_sandboxes_setup_jq>`
        Parse ``json`` output from the upstream echo servers.

This example walks through configuring Envoy using the `Go Control Plane <https://github.com/envoyproxy/go-control-plane>`_
reference implementation.

It demonstrates how configuration provided to Envoy persists, even when the control plane is not available,
and provides a trivial example of how to update Envoy's configuration dynamically.

Step 1: Start the proxy container
*********************************

Change directory to ``examples/dynamic-config-cp`` in the Envoy repository.

First build the containers and start the ``proxy`` container.

This should also start two upstream ``HTTP`` echo servers, ``service1`` and ``service2``.

The control plane has not yet been started.

.. code-block:: console

    $ pwd
    envoy/examples/dynamic-config-cp
    $ docker-compose pull
    $ docker-compose up --build -d proxy
    $ docker-compose ps

           Name                            Command                State                     Ports
    ------------------------------------------------------------------------------------------------------------------------
    dynamic-config-cp_proxy_1      /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-config-cp_service1_1   /bin/echo-server               Up      8080/tcp
    dynamic-config-cp_service2_1   /bin/echo-server               Up      8080/tcp

Step 2: Check initial config and web response
*********************************************

As we have not yet started the control plane, nothing should be responding on port ``10000``.

.. code-block:: console

   $ curl http://localhost:10000
   curl: (56) Recv failure: Connection reset by peer

Dump the proxy's :ref:`static_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.static_clusters>`
configuration and you should see the cluster named ``xds_cluster`` configured for the control plane:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].static_clusters'

.. literalinclude:: _include/dynamic-config-cp/response-config-cluster.json
   :language: json
   :emphasize-lines: 10, 18-19

No :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>`
have been configured yet:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].dynamic_active_clusters'
   null

Step 3: Start the control plane
*******************************

Start up the ``go-control-plane`` service.

You may need to wait a moment or two for it to become ``healthy``.

.. code-block:: console

    $ docker-compose up --build -d go-control-plane
    $ docker-compose ps

            Name                                Command                  State                    Ports
    -------------------------------------------------------------------------------------------------------------------------------------
    dynamic-config-cp_go-control-plane_1  bin/example -debug             Up (healthy)
    dynamic-config-cp_proxy_1             /docker-entrypoint.sh /usr ... Up            0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-config-cp_service1_1          /bin/echo-server               Up            8080/tcp
    dynamic-config-cp_service2_1          /bin/echo-server               Up            8080/tcp

Step 4: Query the proxy
***********************

Once the control plane has started and is ``healthy``, you should be able to make a request to port ``10000``,
which will be served by ``service1``.

.. code-block:: console

   $ curl http://localhost:10000
   Request served by service1

   HTTP/1.1 GET /

   Host: localhost:10000
   Accept: */*
   X-Forwarded-Proto: http
   X-Request-Id: 1d93050e-f39c-4602-90f8-a124d6e78d26
   X-Envoy-Expected-Rq-Timeout-Ms: 15000
   Content-Length: 0
   User-Agent: curl/7.72.0

Step 5: Dump Envoy's ``dynamic_active_clusters`` config
*******************************************************

If you now dump the proxy's :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>`
configuration, you should see it is configured with the ``example_proxy_cluster`` pointing to ``service1``, and a version of ``1``.

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-config-cp/response-config-active-clusters.json
   :language: json
   :emphasize-lines: 3, 11, 19-20

Step 6: Stop the control plane
******************************

Stop the ``go-control-plane`` service:

.. code-block:: console

    $ docker-compose stop go-control-plane

The Envoy proxy should continue proxying responses from ``service1``.

.. code-block:: console

   $ curl http://localhost:10000 | grep "served by"
   Request served by service1

Step 7: Edit ``go`` file and restart the control plane
******************************************************

The example setup starts the ``go-control-plane``
service with a custom :download:`resource.go <_include/dynamic-config-cp/resource.go>` file which
specifies the configuration provided to Envoy.

Update this to have Envoy proxy instead to ``service2``.

Edit ``resource.go`` in the dynamic configuration example folder and change the ``UpstreamHost``
from ``service1`` to ``service2``:

.. literalinclude:: _include/dynamic-config-cp/resource.go
   :language: go
   :lines: 34-43
   :lineno-start: 34
   :emphasize-lines: 6
   :linenos:

Further down in this file you must also change the configuration snapshot version number from
``"1"`` to ``"2"`` to ensure Envoy sees the configuration as newer:

.. literalinclude:: _include/dynamic-config-cp/resource.go
   :language: go
   :lineno-start: 164
   :lines: 174-186
   :emphasize-lines: 3
   :linenos:

Now rebuild and restart the control plane:

.. code-block:: console

    $ docker-compose up --build -d go-control-plane

You may need to wait a moment or two for the ``go-control-plane`` service to become ``healthy`` again.

Step 8: Check Envoy uses the updated configuration
**************************************************

Now when you make a request to the proxy it should be served by the ``service2`` upstream service.

.. code-block:: console

   $ curl http://localhost:10000 | grep "served by"
   Request served by service2

Dumping the :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>`
you should see the cluster configuration now has a version of ``2``, and ``example_proxy_cluster``
is configured to proxy to ``service2``:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-config-cp/response-config-active-clusters-updated.json
   :language: json
   :emphasize-lines: 3, 11, 19-20

.. seealso::

   :ref:`Dynamic configuration (control plane) quick start guide <start_quick_start_dynamic_control_plane>`
      Quick start guide to dynamic configuration of Envoy with a control plane.

   :ref:`Envoy admin quick start guide <start_quick_start_admin>`
      Quick start guide to the Envoy admin interface.

   :ref:`Dynamic configuration (filesystem) sandbox <install_sandboxes_dynamic_config_fs>`
      Configure Envoy using filesystem-based dynamic configuration.

   `Go control plane <https://github.com/envoyproxy/go-control-plane>`_
      Reference implementation of Envoy control plane written in ``go``.
