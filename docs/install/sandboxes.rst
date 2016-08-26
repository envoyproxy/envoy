.. _install_sandboxes:

Sandboxes
=========

To get a flavor of what Envoy has to offer, we are releasing a 
`docker compose <https://docs.docker.com/compose/>`_ sandbox that deploys a front
envoy and a couple of services (simple flask apps) colocated with a running 
service envoy. The three containers will be deployed inside a virtual network
called ``envoymesh``.

Below you can see a graphic showing the docker compose deployment:

.. image:: /_static/docker_compose_v0.1.svg
  :width: 100%

All incomingrequests are routed via the front envoy, which is acting as a reverse proxy
sitting on the edge of the ``envoymesh`` network. Port ``80`` is mapped to 
port ``8000`` by docker compose (see `docker-compose.yml <https://github.com/lyft/envoy/blob/docker-example/example/docker-compose.yml>`_). Moreover, notice that all 
traffic routed by the front envoy to the service containers is actually routed
to the service envoys (routes setup in `front-envoy.json <https://github.com/lyft/envoy/blob/docker-example/example/front-envoy.json>`_). In turn the service envoys route the 
request to the flask app via the loopback address (routes setup in 
`service-envoy.json <https://github.com/lyft/envoy/blob/docker-example/example/service-envoy.json>`_). This setup illustrates the advantage of runnig service envoys 
colocated with your services: all requests are handled by the service envoy, and
efficiently routed to your services.

Running the Sandbox
-------------------

The following documentation runs through the setup of an envoy cluster organized
as is described in the image above.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`, ``docker-compose`` and
``docker-machine`` installed.

A simple way to achieve this is via the `Docker Toolbox <https://www.docker.com/products/docker-toolbox>`_.

**Step 2: Docker Machine setup**

First let's create a new machine which will hold the containers::

    $ docker-machine create --driver virtualbox default
    $ eval $(docker-machine env default)

**Step 4: Start all of our containers**

::
    
    $ pwd
    /src/envoy/example
    $ docker-compose up --build -d
    $ docker-compose ps
            Name                       Command               State      Ports
    -------------------------------------------------------------------------------------------------------------
    example_service1_1      /bin/sh -c /usr/local/bin/ ...    Up       80/tcp
    example_service2_1      /bin/sh -c /usr/local/bin/ ...    Up       80/tcp
    example_front-envoy_1   /bin/sh -c /usr/local/bin/ ...    Up       0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp

**Step 5: Test Envoy's routing capabilities**

You can now send a request to both services via the front-envoy.

For service1::

    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:39:19 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact

For service2::

    $ curl -v $(docker-machine ip default):8000/service/2
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/2 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 2
    < server: envoy
    < date: Fri, 26 Aug 2016 19:39:23 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 2)! hostname: 92f4a3737bbc resolvedhostname: 172.19.0.2
    * Connection #0 to host 192.168.99.100 left intact

Notice that each request, while sent to the front envoy, was correctly routed
to the respective application.

**Step 6: Test Envoy's load balancing capabilities**

Now let's scale up our service1 nodes to demonstrate the clustering abilities
of envoy.::

    $ docker-compose scale service1=3
    Creating and starting example_service1_2 ... done
    Creating and starting example_service1_3 ... done

Now if we send a request to service1, the fron envoy will load balance the
request by doing a round robin of the three service1 machines::

    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:40:21 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: 85ac151715c6 resolvedhostname: 172.19.0.3
    * Connection #0 to host 192.168.99.100 left intact
    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:40:22 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: 20da22cfc955 resolvedhostname: 172.19.0.5
    * Connection #0 to host 192.168.99.100 left intact
    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:40:24 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact
