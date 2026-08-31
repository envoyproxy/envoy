.. _start:

Getting Started
===============

.. container:: getting-started-hero

   .. container:: getting-started-eyebrow

      START HERE

   .. rubric:: Move your first request through Envoy
      :heading-level: 2

   Envoy is a high-performance edge and service proxy. Choose a path that matches your
   environment, then follow the quick start to run Envoy and understand its configuration.

   .. container:: getting-started-actions

      * :ref:`Run Envoy with Docker <start_quick_start_config>`
      * :ref:`Explore installation options <install>`

   .. container:: getting-started-steps

      .. container:: getting-started-step

         **01**

         Install or run the official Envoy image.

      .. container:: getting-started-step

         **02**

         Send traffic through a listener and cluster.

      .. container:: getting-started-step

         **03**

         Inspect, secure, and evolve the configuration.

Choose your path
----------------

Start with the workflow closest to how you plan to evaluate or deploy Envoy.

.. container:: getting-started-paths

   .. container:: getting-started-card

      **Docker**

      .. rubric:: Run locally
         :heading-level: 3

      Start an official Envoy container and proxy a request with a minimal configuration.

      :ref:`Open the Docker quick start <start_quick_start_config>`

   .. container:: getting-started-card

      **Kubernetes**

      .. rubric:: Deploy to a cluster
         :heading-level: 3

      Deploy Envoy Gateway as an ingress gateway in a Kubernetes environment.

      :ref:`View the Envoy Gateway installation <start_install_kubernetes>`

   .. container:: getting-started-card

      **Source**

      .. rubric:: Build Envoy
         :heading-level: 3

      Set up the toolchain and build Envoy from source for development or contribution.

      :ref:`Read the build guide <building>`

Learn the core workflow
-----------------------

Build a practical mental model before moving into the complete configuration and API references.

.. container:: getting-started-learning

   .. container:: getting-started-learning-card

      .. rubric:: 01 — Run
         :heading-level: 3

      Start Envoy, pass a configuration file, and verify the running process.

      :ref:`Run Envoy <start_quick_start_run_envoy>`

   .. container:: getting-started-learning-card

      .. rubric:: 02 — Configure
         :heading-level: 3

      Connect listeners to upstream clusters with a static bootstrap configuration.

      :ref:`Learn static configuration <start_quick_start_static>`

   .. container:: getting-started-learning-card

      .. rubric:: 03 — Discover
         :heading-level: 3

      Understand how a control plane can deliver dynamic resources through xDS.

      :ref:`Explore dynamic configuration <start_quick_start_dynamic_control_plane>`

   .. container:: getting-started-learning-card

      .. rubric:: 04 — Operate
         :heading-level: 3

      Inspect the admin interface, then add transport security before production use.

      :ref:`Use the admin interface <start_quick_start_admin>` ·
      :ref:`Secure Envoy <start_quick_start_securing>`

Go deeper
---------

.. container:: getting-started-resources

   .. container:: getting-started-resource

      .. rubric:: Try focused examples
         :heading-level: 3

      Use the :ref:`Envoy sandboxes <start_sandboxes>` to explore individual capabilities in
      runnable environments.

   .. container:: getting-started-resource

      .. rubric:: Follow a request
         :heading-level: 3

      Read the :ref:`life of a request <life_of_a_request>` for an architectural tour through
      Envoy's data path.

   .. container:: getting-started-resource

      .. rubric:: Prepare for production
         :heading-level: 3

      Move from evaluation to day-two practices with the :ref:`operations guide <operations>`.

   .. container:: getting-started-resource

      .. rubric:: Find exact fields
         :heading-level: 3

      Use the :ref:`configuration guide <config>` and :ref:`v3 API reference
      <envoy_v3_api_reference>` for complete technical details.

All getting started guides
--------------------------

.. toctree::
    :maxdepth: 1
    :titlesonly:

    install
    quick-start/index
    sandboxes/index
    docker
    building
