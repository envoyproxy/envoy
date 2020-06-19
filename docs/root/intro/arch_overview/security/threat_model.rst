.. _arch_overview_threat_model:

Threat model
============

Below we articulate the Envoy threat model, which is of relevance to Envoy operators, developers and
security researchers. We detail our security release process at
https://github.com/envoyproxy/envoy/security/policy.

Confidentiality, integrity and availability
-------------------------------------------

We consider vulnerabilities leading to the compromise of data confidentiality or integrity to be our
highest priority concerns. Availability, in particular in areas relating to DoS and resource
exhaustion, is also a serious security concern for Envoy operators, in particular those utilizing
Envoy in edge deployments.

We will activate the security release process for disclosures that meet the following criteria:

* All issues that lead to loss of data confidentiality or integrity trigger the security release process.
* An availability issue, such as Query-of-Death (QoD) or resource exhaustion needs to meet all of the
  following criteria to trigger the security release process:
  
  - A component tagged as hardened is affected (see `Core and extensions`_ for the list of hardened components).
    
  - The type of traffic (upstream or downstream) that exhibits the issue matches the component's hardening tag.
    I.e. component tagged as “hardened to untrusted downstream” is affected by downstream request.
    
  - A resource exhaustion issue needs to meet these additional criteria:
    
    + Not covered by an existing timeout or where applying short timeout values is impractical and either
      
      + Memory exhaustion, including out of memory conditions, where per-request memory use 100x or more above
	the configured header or high watermark limit. I.e. 10 KiB client request leading to 1 MiB bytes of
	memory consumed by Envoy;
      
      + Highly asymmetric CPU utilization where Envoy uses 100x or more CPU compared to client.


The Envoy availability stance around CPU and memory DoS is still evolving, especially for brute force
attacks. We acknowledge that brute force (i.e. these with amplification factor less than 100) attacks are
likely for Envoy deployments as part of cloud infrastructure or with the use of botnets. We will continue
to iterate and fix well known resource issues in the open, e.g. overload manager and watermark improvements.
We will activate the security process for brute force disclosures that appear to present a risk to
existing Envoy deployments.

Note that we do not currently consider the default settings for Envoy to be safe from an availability
perspective. It is necessary for operators to explicitly :ref:`configure <best_practices_edge>`
watermarks, the overload manager, circuit breakers and other resource related features in Envoy to
provide a robust availability story. We will not act on any security disclosure that relates to a
lack of safe defaults. Over time, we will work towards improved safe-by-default configuration, but
due to backwards compatibility and performance concerns, this will require following the breaking
change deprecation policy.

Data and control plane
----------------------

We divide our threat model into data and control plane, reflecting the internal division in Envoy of
these concepts from an architectural perspective. Our highest priority in risk assessment is the
threat posed by untrusted downstream client traffic on the data plane. This reflects the use of
Envoy in an edge serving capacity and also the use of Envoy as an inbound destination in a service
mesh deployment.

In addition, we have an evolving position towards any vulnerability that might be exploitable by
untrusted upstreams. We recognize that these constitute a serious security consideration, given the
use of Envoy as an egress proxy. We will activate the security release process for disclosures that
appear to present a risk profile that is significantly greater than the current Envoy upstream
hardening status quo.

The control plane management server is generally trusted. We do not consider wire-level exploits
against the xDS transport protocol to be a concern as a result. However, the configuration delivered
to Envoy over xDS may originate from untrusted sources and may not be fully sanitized. An example of
this might be a service operator that hosts multiple tenants on an Envoy, where tenants may specify
a regular expression on a header match in `RouteConfiguration`. In this case, we expect that Envoy
is resilient against the risks posed by malicious configuration from a confidentiality, integrity
and availability perspective, as described above.

We generally assume that services utilized for side calls during the request processing, e.g.
external authorization, credential suppliers, rate limit services, are trusted. When this is not the
case, an extension will explicitly state this in its documentation.

Core and extensions
-------------------

Anything in the Envoy core may be used in both untrusted and trusted deployments. As a consequence,
it should be hardened with this model in mind. Security issues related to core code will usually
trigger the security release process as described in this document.

The following extensions are intended to be hardened against untrusted downstream and upstreams:

.. include:: secpos_robust_to_untrusted_downstream_and_upstream.rst

The following extensions should not be exposed to data plane attack vectors and hence are intended
to be robust to untrusted downstreams and upstreams:

.. include:: secpos_data_plane_agnostic.rst

The following extensions are intended to be hardened against untrusted downstreams but assume trusted
upstreams:

.. include:: secpos_robust_to_untrusted_downstream.rst

The following extensions should only be used when both the downstream and upstream are trusted:

.. include:: secpos_requires_trusted_downstream_and_upstream.rst


The following extensions have an unknown security posture:

.. include:: secpos_unknown.rst

Envoy currently has two dynamic filter extensions that support loadable code; WASM and Lua. In both
cases, we assume that the dynamically loaded code is trusted. We expect the runtime for Lua to be
robust to untrusted data plane traffic with the assumption of a trusted script. WASM is still in
development, but will eventually have a similar security stance.
