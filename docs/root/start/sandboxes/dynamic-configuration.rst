.. _install_sandboxes_wasm_filter:

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

	Name                     Command                State             Ports
    -----------------------------------------------------------------------------------------------


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
   [
     {
       "cluster": {
	 "@type": "type.googleapis.com/envoy.api.v2.Cluster",
	 "name": "xds_cluster",
	 "type": "STRICT_DNS",
	 "connect_timeout": "1s",
	 "http2_protocol_options": {},
	 "load_assignment": {
	   "cluster_name": "xds_cluster",
	   "endpoints": [
	     {
	       "lb_endpoints": [
		 {
		   "endpoint": {
		     "address": {
		       "socket_address": {
			 "address": "go-control-plane",
			 "port_value": 18000
		       }
		     }
		   }
		 }
	       ]
	     }
	   ]
	 }
       },
       "last_updated": "2020-10-24T18:30:55.458Z"
     }
   ]


Step 5: Start the go-control-plane
**********************************

.. code-block:: console

    $ docker-compose up -d go-control-plane
    $ docker-compose ps

	Name                     Command                State             Ports
    -----------------------------------------------------------------------------------------------


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
