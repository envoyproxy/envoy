.. _arch_overview_google_vrp:

Google Vulnerability Reward Program (VRP)
=========================================

Envoy is a participant in `Google's Vulnerability Reward Program (VRP)
<https://www.google.com/about/appsecurity/reward-program/>`_. This is open to all security
researchers and will provide rewards for vulnerabilities discovered and reported according to the
rules below.

.. _arch_overview_google_vrp_rules:

Rules
-----

The goal of the VRP is to provide a formal process to honor contributions from external
security researchers to Envoy's security. Vulnerabilities should meet the following conditions
to be eligible for the program:

1. Vulnerabilities must meet one of the below :ref:`objectives
   <arch_overview_google_vrp_objectives>`, demonstrated with the supplied Docker-based
   :ref:`execution environment <arch_overview_google_vrp_ee>` and be consistent with the
   program's :ref:`threat model <arch_overview_google_vrp_threat_model>`.

2. Vulnerabilities must be reported to envoy-security@googlegroups.com and be kept under embargo
   while triage and potential security releases occur. Please follow the :repo:`disclosure guidance
   <SECURITY.md#disclosures>` when submitting reports. Disclosure SLOs are documented :repo:`here
   <SECURITY.md#fix-and-disclosure-slos>`. In general, security disclosures are subject to the
   `Linux Foundation's privacy policy <https://www.linuxfoundation.org/privacy/>`_ with the added
   proviso that VRP reports (including reporter e-mail address and name) may be freely shared with
   Google for VRP purposes.

3. Vulnerabilities must not be previously known in a public forum, e.g. GitHub issues trackers,
   CVE databases (when previously associated with Envoy), etc. Existing CVEs that have not been
   previously associated with an Envoy vulnerability are fair game.

4. Vulnerabilities must not be also submitted to a parallel reward program run by Google or
   `Lyft <https://www.lyft.com/security>`_.

Rewards are at the discretion of the Envoy OSS security team and Google. They will be conditioned on
the above criteria. If multiple instances of the same vulnerability are reported at the same time by
independent researchers or the vulnerability is already tracked under embargo by the OSS Envoy
security team, we will aim to fairly divide the reward amongst reporters.

.. _arch_overview_google_vrp_threat_model:

Threat model
------------

The base threat model matches that of Envoy's :ref:`OSS security posture
<arch_overview_threat_model>`. We add a number of temporary restrictions to provide a constrained
attack surface for the initial stages of this program. We exclude any threat from:

* Untrusted control planes.
* Runtime services such as access logging, external authorization, etc.
* Untrusted upstreams.
* DoS attacks except as stipulated below.
* Any filters apart from the HTTP connection manager network filter and HTTP router filter.
* Admin console; this is disabled in the execution environment.

We also explicitly exclude any local attacks (e.g. via local processes, shells, etc.) against
the Envoy process. All attacks must occur via the network data plane on port 10000. Similarly,
kernel and Docker vulnerabilities are outside the threat model.

In the future we may relax some of these restrictions as we increase the sophistication of the
program's execution environment.

.. _arch_overview_google_vrp_ee:

Execution environment
---------------------

We supply Docker images that act as the reference environment for this program:

* `envoyproxy/envoy-google-vrp <https://hub.docker.com/r/envoyproxy/envoy-google-vrp/tags/>`_ images
  are based on Envoy point releases. Only the latest point release at the time of vulnerability
  submission is eligible for the program. The first point release available for VRP will be the
  1.15.0 Envoy release.

* `envoyproxy/envoy-google-vrp-dev <https://hub.docker.com/r/envoyproxy/envoy-google-vrp-dev/tags/>`_
  images are based on Envoy master builds. Only builds within the last 5 days at the time of
  vulnerability submission are eligible for the program. They must not be subject to any
  publicly disclosed vulnerability at that point in time.

Two Envoy processes are available when these images are launched via `docker run`:

* The *edge* Envoy is listening on ports 10000 (HTTPS). It has a :repo:`static configuration
  </configs/google-vrp/envoy-edge.yaml>` that is configured according to Envoy's :ref:`edge hardening
  principles <faq_edge>`. It has sinkhole, direct response and request forwarding routing rules (in
  order):

  1. `/content/*`: route to the origin Envoy server.
  2. `/*`: return 403 (denied).


* The *origin* Envoy is an upstream of the edge Envoy. It has a :repo:`static configuration
  </configs/google-vrp/envoy-origin.yaml>` that features only direct responses, effectively acting
  as an HTTP origin server. There are two route rules (in order):

  1. `/blockedz`: return 200 `hidden treasure`. It should never be possible to have
     traffic on the Envoy edge server's 10000 port receive this response unless a
     qualifying vulnerability is present.
  2. `/*`: return 200 `normal`.

When running the Docker images, the following command line options should be supplied:

* `-m 3g` to ensure that memory is bounded to 3GB. At least this much memory should be available
  to the execution environment. Each Envoy process has an overload manager configured to limit
  at 1GB.

* `-e ENVOY_EDGE_EXTRA_ARGS="<...>"` supplies additional CLI args for the edge Envoy. This
  needs to be set but can be empty.

* `-e ENVOY_ORIGIN_EXTRA_ARGS="<...>"` supplies additional CLI args for the origin Envoy. This
  needs to be set but can be empty.

.. _arch_overview_google_vrp_objectives:

Objectives
----------

Vulnerabilities will be evidenced by requests on 10000 that trigger a failure mode
that falls into one of these categories:

* Query-of-death: requests that cause the Envoy process to segfault or abort
  in some immediate way.
* OOM: requests that cause the edge Envoy process to OOM. There should be no more than
  100 connections and streams in total involved to cause this to happen (i.e. brute force
  connection/stream DoS is excluded).
* Routing rule bypass: requests that are able to access `hidden treasure`.
* TLS certificate exfiltration: requests that are able to obtain the edge Envoy's
  `serverkey.pem`.
* Remote code exploits: any root shell obtained via the network data plane.
* At the discretion of the OSS Envoy security team, sufficiently interesting vulnerabilities that
  don't fit the above categories but are likely to fall into the category of high or critical
  vulnerabilities.

Working with the Docker images
------------------------------

A basic invocation of the execution environment that will bring up the edge Envoy on local
port 10000 looks like:

.. code-block:: bash

   docker run -m 3g -p 10000:10000 --name envoy-google-vrp \
     -e ENVOY_EDGE_EXTRA_ARGS="" \
     -e ENVOY_ORIGIN_EXTRA_ARGS="" \
     envoyproxy/envoy-google-vrp-dev:latest

When debugging, additional args may prove useful, e.g. in order to obtain trace logs, make
use of `wireshark` and `gdb`:

.. code-block:: bash

   docker run -m 3g -p 10000:10000 --name envoy-google-vrp \
     -e ENVOY_EDGE_EXTRA_ARGS="-l trace" \
     -e ENVOY_ORIGIN_EXTRA_ARGS="-l trace" \
     --cap-add SYS_PTRACE --cap-add NET_RAW --cap-add NET_ADMIN \
     envoyproxy/envoy-google-vrp-dev:latest

You can obtain a shell in the Docker container with:

.. code-block:: bash

  docker exec -it envoy-google-vrp /bin/bash

The Docker images include `gdb`, `strace`, `tshark` (feel free to contribute other
suggestions via PRs updating the :repo:`Docker build file </ci/Dockerfile-envoy-google-vrp>`).

Rebuilding the Docker image
---------------------------

It's helpful to be able to regenerate your own Docker base image for research purposes.
To do this without relying on CI, follow the instructions at the top of
:repo:`ci/docker_rebuild_google-vrp.sh`. An example of this flow looks like:

.. code-block:: bash

   bazel build //source/exe:envoy-static
   ./ci/docker_rebuild_google-vrp.sh bazel-bin/source/exe/envoy-static
   docker run -m 3g -p 10000:10000 --name envoy-google-vrp \
     -e ENVOY_EDGE_EXTRA_ARGS="" \
     -e ENVOY_ORIGIN_EXTRA_ARGS="" \
     envoy-google-vrp:local
