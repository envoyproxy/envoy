.. _install_sandboxes_dynamic_configuration:

Dynamic configuration
=====================

.. include:: _include/docker-env-setup.rst


Step 3: Take a look at the proxy config
***************************************


Step 4: Start the proxy container
*********************************

First lets build our containers and start the proxy container, and two backend HTTP echo servers.

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

Step 4: Check web response
**************************

Nothing is listening on 10000

.. code-block:: console

   $ curl http://localhost:10000
   curl: (56) Recv failure: Connection reset by peer

If we config dump the clusters we just see the ``xds_cluster`` we configured for the control
plane

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump  | jq '.configs[1].static_clusters'

.. literalinclude:: _include/dynamic-configuration/response-config-cluster.json
   :language: json

Step 5: Start the go-control-plane
**********************************

.. code-block:: console

    $ docker-compose up -d go-control-plane
    $ docker-compose ps

		Name                                Command                  State                    Ports
    -----------------------------------------------------------------------------------------------------------------------------------------
    dynamic-configuration_go-control-plane_1  bin/example -debug             Up (healthy)
    dynamic-configuration_proxy_1             /docker-entrypoint.sh /usr ... Up            0.0.0.0:10000->10000/tcp, 0.0.0.0:19000->19000/tcp
    dynamic-configuration_service1_1          /bin/echo-server               Up            8080/tcp
    dynamic-configuration_service2_1          /bin/echo-server               Up            8080/tcp

Step 5: Query the proxy
***********************


Step 5: Dump the proxy config again
***********************************


Step 5: Stop the go-control-plane
*********************************


Step 5: Query the proxy
***********************


Step 5: Edit resource.go and restart the go-control-plane
*********************************************************


Step 5: Query the proxy
***********************
