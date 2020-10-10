.. _incremental_deploys:

Incremental blue/green deploys
==============================

One of most common workflows for any microservice is releasing a new version.
Thinking about releases as a traffic management problem—as opposed to an
infrastructure update—opens up new tools for protecting users from bad releases.

We’ll begin with the simple routes we set up previously
[on your laptop](on-your-laptop).
Next, we’ll extend that config to release a new version of one of the services
using traffic shifting. We’ll also cover header-based routing and weighted load
balancing to show how to use traffic management to canary a release, first to
special requests (e.g. requests from your laptop), then to a small fraction of
all requests.

The setup
~~~~~~~~~

For this guide, we’ll need:

- [Docker](https://docs.docker.com/install/)
- [Docker Compose](https://docs.docker.com/compose/install/)
- [Git](https://help.github.com/articles/set-up-git/)
- [curl](https://curl.haxx.se/)

Header-based Routing
~~~~~~~~~~~~~~~~~~~~

First, we'll create a new version of service1 to illustrate the value of
header-based routing for our services. Still following along with the
[front-proxy](https://github.com/envoyproxy/envoy/tree/master/examples/front-proxy)
example from the Envoy repo, modify the
[docker-compose.yml](https://github.com/envoyproxy/envoy/blob/master/examples/front-proxy/docker-compose.yml)
to spin up a new service, called ``service1a``.

.. code-block:: yaml

   service1a:
     build:
       context: .
       dockerfile: Dockerfile-service
     volumes:
     - ./service-envoy.yaml:/etc/service-envoy.yaml
     networks:
       envoymesh:
         aliases:
         - service1a
     environment:
     - SERVICE_NAME=1a
     expose:
     - "80"

To make sure Envoy can discover this service, we’ll also add it to the
``clusters`` section of our configuration file.

.. code-block:: yaml

   - name: service1a
     connect_timeout: 0.25s
     type: strict_dns
     lb_policy: round_robin
     http2_protocol_options: {}
     hosts:
     - socket_address:
         address: service1a
         port_value: 80

To make this routable, we can add a new route with a ``headers`` field in the
match. Since routing rules are applied in order, we’ll add this rule to the top
of the ``routes`` key. Requests with a header that matches our new rule will be
sent to our new service, while requests that don't include this header will
still get service 1.

.. code-block:: yaml

   routes:
   - match:
       prefix: "/service/1"
       headers:
       - name: "x-canary-version"
         exact_match: "service1a"
     route:
       cluster: service1a
   - match:
       prefix: "/service/1"
     route:
       cluster: service1
   - match:
       prefix: "/service/2"
     route:
       cluster: service2

Shut down and then relaunch our example services with:

.. code-block:: console

   $ docker-compose down --remove-orphans
   $ docker-compose up --build -d

In a production Envoy deployment, configuration changes like this won’t require
a restart of Envoy, but since we’re running everything locally, we aren’t able
to take advantage of its dynamic configuration abilities.

If we make a request to our service with no headers, we'll get a response
from service 1:

.. code-block:: console

   $ curl localhost:8000/service/1
   Hello from behind Envoy (service 1)! hostname: d0adee810fc4 resolvedhostname: 172.18.0.2

However if we include the ``x-canary-version`` header, Envoy will route our
request to service 1a:

.. code-block:: console

   $ curl -H 'x-canary-version: service1a' localhost:8000/service/1
   Hello from behind Envoy (service 1a)! hostname: 569ee89eebc8 resolvedhostname: 172.18.0.6

Header-based routing with Envoy unlocks the ability to
[test development code in production](https://opensource.com/article/17/8/testing-production).

Weighted Load Balancing
~~~~~~~~~~~~~~~~~~~~~~~

Next, let's modify our config further to enable an incremental release to our
new service version. The following config should look familiar, but we've
swapped out the ``cluster`` key for ``clusters`` array under the default
routing rule, which moves 25% of the traffic pointed at our service to this
new version.

.. code-block:: yaml

   - match:
       prefix: "/service/1"
     route:
       weighted_clusters:
         clusters:
	 - name: service1a
           weight: 25
	 - name: service1
           weight: 75

With this in place, shut down your previous example services by running:

.. code-block:: console

   $ docker-compose down --remove-orphans

Then, start it again with:

.. code-block:: console

   $ docker-compose up --build -d

Now, if we make a request to our service with no headers we should see
responses from service 1a about 25% of the time, or when the appropriate header
is loaded.

This example illustrates the power of an incremental release of your service,
and in the wild would also be paired with monitoring to ensure the delta
between versions of services, or between heterogeneous backends was trending
well before increasing or completing a release.

If we wanted to simulate a successful release, we could set the value of our
rule to 100, which would ensure all traffic is now sent to service 1a instead
of service 1. Similarly, by setting this value to 0, we could roll-back a bad
release.

Best practices
~~~~~~~~~~~~~~

With the basics of using header-based routing and incremental weighted release,
you can now take advantage of a few best-practice patterns of software deploy
and release.

To start, separate the deploy and release processes. For most teams, this means
using CI/CD to get new versions of software onto your infrastructure, but
taking no traffic. Then release the software by incrementally shifting
production traffic as described above. This can either be automated through
CI/CD (after the deploy step), or run as a manual process. By separating these
steps, you ensuring software on production infrastructure isn’t immediately
production taking traffic, limiting the damage of a bad release.

Wrap-up
~~~~~~~

While not every release will require all of these capabilities, you can use
Envoy’s routing tools to build a process to release software incrementally
while gaining confidence in it. Once your new service is deployed, routing all
internal traffic to it with a header will let your teams verify a PR, or
internally test it. Once you think it’s ready for users, you can then use
weighted incremental release patterns to gracefully release your new version to
them. A good pattern for weights as you approach 100% of traffic starts small
and takes progressively large leaps 1%, 5%, 10%, 50%. This pattern gives you
actionable feedback on your release (watch the metrics after each adjustment!),
with only small portions of your users initially affected.

By separating deploy from release, using header-based routing to test
production deploys before release, and building incremental release
thoughtfully, your teams will greatly benefit  from the capabilities of Envoy.

Now that you've seen a few examples of incremental and header-based routing
using Envoy, you may want to investigate more advanced features of Envoy, like
[automatic retries](automatic-retries)
or learn how to
[dynamically configure routing](routing-configuration).
