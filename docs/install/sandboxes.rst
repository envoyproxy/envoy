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
