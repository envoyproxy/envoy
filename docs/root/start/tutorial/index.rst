.. _tutorial:

Envoy tutorial
==============

Distributed systems are hard, but upgrading your networking with Envoy doesn’t have to be. Read on for common best practices of successful, production-grade Envoy deployments, drawn from the experience of engineers from the largest cloud-native applications on the planet.

Getting Started
~~~~~~~~~~~~~~~

You can get Envoy set up on your laptop with a bootstrap config, then extend it as you need more functionality. Start seeing the benefits, and learn how to talk to the community when you get stuck.

.. toctree::
  :maxdepth: 2

  on-your-laptop
  routing-basic
  getting-help

Dynamic configuration
~~~~~~~~~~~~~~~~~~~~~

Envoy's powerful configuration model is miles ahead of other open-source serving layers. Envoy uses pluggable, dynamic APIs instead of static files, allowing changes to your environment to be applied instantly, with no service interruption. You can get Envoy set up on your laptop with a bootstrap config, then extend it as you need more functionality. As your Envoy fleet grows, centralize configuration in a control plane that implements Envoy's xDS APIs.

.. toctree::
  :maxdepth: 2

  service-discovery
  routing-configuration
  ssl

Observability
~~~~~~~~~~~~~

Once Envoy is emitting the right data, what do you do with it?

.. toctree::
  :maxdepth: 2

  log-parsing
  metrics-aggregation
  change-logging

Deployment models
~~~~~~~~~~~~~~~~~

Envoy’s small footprint allows for a variety of models. Most companies end up running multiple models to support:

.. toctree::
  :maxdepth: 2

  front-proxy
  service-mesh
  hosted-where-you-are

Resilience
~~~~~~~~~~

No two distributed systems are alike, and this only gets more true as systems get larger. Here’s how teams have approached the particularly thorny problem of managing dozens or hundreds of services at scale. Use their experience to solve your issues quickly.

.. toctree::
  :maxdepth: 2

  circuit-breaking
  automatic-retries
  health-check
  backpressure


Building on Envoy
~~~~~~~~~~~~~~~~~

Tools are good. Solutions are better. The best companies use Envoy’s capabilities to democratize capabilities that normally require arcane knowledge and deep expertise. Be careful: once you’ve tried some of these workflows, you may not be able to go back.

.. toctree::
  :maxdepth: 2

  incremental-deploys
  verifying-in-production
  migrations
