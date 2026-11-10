.. _arch_overview_google_prp:

Google Patch Reward Program (PRP)
=================================

Envoy is a participant in `Google's Patch Reward Program (PRP)
<https://bughunters.google.com/open-source-security/patch-rewards/>`_. This is open to all security
researchers and will provide rewards for vulnerabilities discovered and reported according to the
rules below.

.. _arch_overview_google_prp_rules:

Rules
-----

The goal of the PRP is to provide a formal process to honor contributions from external
security researchers to Envoy's security. Vulnerabilities should meet the following conditions
to be eligible for the program:

1. Vulnerabilities must be reported to the Envoy project, preferably by
   `opening a GitHub Security Advisory <https://github.com/envoyproxy/envoy/security/advisories/new>`_
   — alternatively, you may email envoy-security@googlegroups.com
   Vulnerabilities must be kept under embargo while triage and potential security releases occur.
   Please follow the :repo:`disclosure guidance <SECURITY.md#disclosures>` when submitting reports.
   Disclosure SLOs are documented :repo:`here <SECURITY.md#fix-and-disclosure-slos>`. In general,
   security disclosures are subject to the `Linux Foundation's privacy policy
   <https://www.linuxfoundation.org/privacy/>`_ with the added proviso that PRP reports (including
   reporter e-mail address and name) may be freely shared with Google for PRP purposes.

2. After the vulnerability has been confirmed by the Envoy project and the patch has been merged
   and publicly released, you may apply to `Google's Patch Reward Program
   <https://bughunters.google.com/open-source-security/patch-rewards/>`_.
   The program requires you to submit the PR as well as evidence of improvement, such as a resolved
   security advisory, which are only available after public disclosure.

3. Vulnerabilities must not be previously known in a public forum, e.g. GitHub issues trackers,
   CVE databases (when previously associated with Envoy), etc. Existing CVEs that have not been
   previously associated with an Envoy vulnerability are fair game.

4. Vulnerabilities must not be also submitted to a parallel reward program run by Google or
   `Lyft <https://www.lyft.com/security>`_.

Rewards are at the discretion of the Envoy OSS security team and Google. They will be conditioned on
the above criteria. If multiple instances of the same vulnerability are reported at the same time by
independent researchers or the vulnerability is already tracked under embargo by the OSS Envoy
security team, we will aim to fairly divide the reward amongst reporters.

Rewards should be claimed from Google PRP following the corresponding Envoy security release.

Threat model
------------

The threat model matches that of Envoy's :ref:`OSS security posture <arch_overview_threat_model>`.
