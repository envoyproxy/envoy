.. _install_sandboxes_dynamic_configuration:

Dynamic configuration
=====================

This example walks through configuring Envoy using the `Go Control Plane <https://github.com/envoyproxy/go-control-plane>`
reference implementation.

It demonstrates how configuration provided to Envoy persists, even when the control plane is not available,
and a trivial example of how to update Envoy's configuration dynamically.

.. include:: _include/docker-env-setup.rst

Step 3: Start the proxy container
*********************************

First lets build our containers and start the proxy container, and two backend HTTP echo servers.

The go-control-plane has not yet been started.

.. code-block:: console

    $ pwd
    envoy/examples/dynamic-configuration
    $ docker-compose build --pull
    $ docker-compose up -d proxy
    $ docker-compose ps

	      Name                            Command                 State                     Ports
    ------------------------------------------------------------------------------------------------------------------------------
    dynamic-configuration_proxy_1      /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-configuration_service1_1   /bin/echo-server               Up      8080/tcp
    dynamic-configuration_service2_1   /bin/echo-server               Up      8080/tcp

Step 4: Check initial config and web response
*********************************************

As we have not yet started the control plane, nothing should be responding on port 10000

.. code-block:: console

   $ curl http://localhost:10000
   curl: (56) Recv failure: Connection reset by peer

If you config dump the ``static_clusters`` you should see the ``xds_cluster`` configured for the control
plane:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].static_clusters'

.. literalinclude:: _include/dynamic-configuration/response-config-cluster.json
   :language: json

Step 5: Start the control plane
*******************************

Let's start up the control plane.

You may need to wait a moment or two for the service to become ``healthy``.

.. code-block:: console

    $ docker-compose up --build -d go-control-plane
    $ docker-compose ps

		Name                                Command                  State                    Ports
    -----------------------------------------------------------------------------------------------------------------------------------------
    dynamic-configuration_go-control-plane_1  bin/example -debug             Up (healthy)
    dynamic-configuration_proxy_1             /docker-entrypoint.sh /usr ... Up            0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-configuration_service1_1          /bin/echo-server               Up            8080/tcp
    dynamic-configuration_service2_1          /bin/echo-server               Up            8080/tcp

Step 6: Query the proxy
***********************

Once the control plane has started, you should be able to make a request to port ``10000``, which will be
served by ``service1``.

.. code-block:: console

   $ curl http://localhost:10000
   Request served by service1

   HTTP/1.1 GET /

   Host: service1
   Accept: */*
   X-Forwarded-Proto: http
   X-Request-Id: 1d93050e-f39c-4602-90f8-a124d6e78d26
   X-Envoy-Expected-Rq-Timeout-Ms: 15000
   Content-Length: 0
   User-Agent: curl/7.72.0

Step 5: Dump Envoy's ``dynamic_active_clusters`` config
*******************************************************

If you now ``config_dump`` the ``dynamic_active_clusters`` you should see Envoy is configured
with the cluster endpoint pointing to ``service1``

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].dynamic_active_clusters'

.. literalinclude:: _include/dynamic-configuration/response-config-active-clusters.json
   :language: json

Step 6: Stop the control plane
******************************

Stop the go-control-plane:

.. code-block:: console

    $ docker-compose stop go-control-plane

The Envoy proxy should continue proxying responses from ``service1``

.. code-block:: console

   $ curl http://localhost:10000 | grep "served by"
   Request served by service1

Step 7: Edit resource.go and restart the go-control-plane
*********************************************************

The example setup starts `go-control-plane <https://github.com/envoyproxy/go-control-plane>`_
with a custom :download:`resource.go <_include/dynamic-configuration/resource.go>` file which
specifies the configuration provided to Envoy.

If you edit this file and change the ``UpstreamHost`` from ``service1`` to ``service2``:

.. literalinclude:: _include/dynamic-configuration/resource.go
   :language: go
   :lines: 33-40
   :emphasize-lines: 6
   :linenos:

Further down in this file you must also change the configuration snapshot version number from
``"1"`` to ``"2"`` to ensure Envoy sees the new configuration as newer:

.. literalinclude:: _include/dynamic-configuration/resource.go
   :language: go
   :lineno-start: 167
   :lines: 167-177
   :emphasize-lines: 3
   :linenos:

Now rebuild and restart the control plane:

.. code-block:: console

    $ docker-compose up --build -d go-control-plane

You may need to wait a moment or two for the ``go-control-plane`` service to become ``healthy``.

Step 8: Check Envoy uses the updated configuration
**************************************************

Now when you make a request to the proxy it should be served by the ``service2`` backend.

.. code-block:: console

   $ curl http://localhost:10000
   Request served by service2

   HTTP/1.1 GET /

   Host: service1
   Accept: */*
   X-Forwarded-Proto: http
   X-Request-Id: 1d93050e-f39c-4602-90f8-a124d6e78d26
   X-Envoy-Expected-Rq-Timeout-Ms: 15000
   Content-Length: 0
   User-Agent: curl/7.72.0


Add config dump...
