.. _arch_overview_initialization:

Initialization
==============

How Envoy initializes itself when it starts up is complex. This section explains at a high level
how the process works. All of the following happens before any listeners start listening and
accepting new connections.

* During startup, the :ref:`cluster manager <arch_overview_cluster_manager>` goes through a
  multi-phase initialization where it first initializes static/DNS clusters, then predefined
  :ref:`EDS <arch_overview_dynamic_config_eds>` clusters. Then it initializes
  :ref:`CDS <arch_overview_dynamic_config_cds>` if applicable, waits for one response (or failure)
  for a :ref:`bounded period of time <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>`,
  and does the same primary/secondary initialization of CDS provided clusters.
* If clusters use :ref:`active health checking <arch_overview_health_checking>`, Envoy also does a
  single active health check round.
* Once cluster manager initialization is done, :ref:`RDS <arch_overview_dynamic_config_rds>` and
  :ref:`LDS <arch_overview_dynamic_config_lds>` initialize (if applicable). The server waits
  for a :ref:`bounded period of time <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>`
  for at least one response (or failure) for LDS/RDS requests. After which, it starts accepting connections.
* If LDS itself returns a listener that needs an RDS response, Envoy further waits for
  a :ref:`bounded period of time <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>` until an RDS
  response (or failure) is received. Note that this process takes place on every future listener
  addition via LDS and is known as :ref:`listener warming <config_listeners_lds>`.
* After all of the previous steps have taken place, the listeners start accepting new connections.
  This flow ensures that during hot restart the new process is fully capable of accepting and
  processing new connections before the draining of the old process begins.

A key design principle of initialization is that an Envoy is always guaranteed to initialize within
:ref:`initial_fetch_timeout <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>`,
with a best effort made to obtain the complete set of xDS configuration within that subject to the
management server availability.
