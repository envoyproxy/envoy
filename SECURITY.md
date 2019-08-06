# Security Release Process

Envoy is a large growing community of volunteers, users, and vendors. The Envoy community has
adopted this security disclosure and response policy to ensure we responsibly handle critical
issues.

## Product Security Team (PST)

Security vulnerabilities should be handled quickly and sometimes privately. The primary goal of this
process is to reduce the total time users are vulnerable to publicly known exploits.

The Product Security Team (PST) is responsible for organizing the entire response including internal
communication and external disclosure but will need help from relevant developers to successfully
run this process.

The initial Product Security Team will consist of all [maintainers](OWNERS.md) in the private
[envoy-security](https://groups.google.com/forum/#!forum/envoy-security) list. In the future we may
decide to have a subset of maintainers work on security response given that this process is time
consuming.

## Disclosures

### Private Disclosure Processes

The Envoy community asks that all suspected vulnerabilities be privately and responsibly disclosed
via the [reporting policy](README.md#reporting-security-vulnerabilities).

### Public Disclosure Processes

If you know of a publicly disclosed security vulnerability please IMMEDIATELY email
[envoy-security](https://groups.google.com/forum/#!forum/envoy-security) to inform the Product
Security Team (PST) about the vulnerability so they may start the patch, release, and communication
process.

If possible the PST will ask the person making the public report if the issue can be handled via a
private disclosure process (for example if the full exploit details have not yet been published). If
the reporter denies the request for private disclosure, the PST will move swiftly with the fix and
release process. In extreme cases GitHub can be asked to delete the issue but this generally isn't
necessary and is unlikely to make a public disclosure less damaging.

## Patch, Release, and Public Communication

For each vulnerability a member of the PST will volunteer to lead coordination with the "Fix Team"
and is responsible for sending disclosure emails to the rest of the community. This lead will be
referred to as the "Fix Lead."

The role of Fix Lead should rotate round-robin across the PST.

Note that given the current size of the Envoy community it is likely that the PST is the same as
the "Fix team." (I.e., all maintainers). The PST may decide to bring in additional contributors
for added expertise depending on the area of the code that contains the vulnerability.

All of the timelines below are suggestions and assume a private disclosure. The Fix Lead drives the
schedule using their best judgment based on severity and development time. If the Fix Lead is
dealing with a public disclosure all timelines become ASAP (assuming the vulnerability has a CVSS
score >= 4; see below). If the fix relies on another upstream project's disclosure timeline, that
will adjust the process as well. We will work with the upstream project to fit their timeline and
best protect our users.

### Released versions and master branch

If the vulnerability affects the last point release version, e.g. 1.10, then the full security
release process described in this document will be activated. A security point release will be
created for 1.10, e.g. 1.10.1, together with a fix to master if necessary. Older point releases,
e.g. 1.9, are not supported by the Envoy project and will not have any security release created.

If a security vulnerability affects only these older versions but not master or the last supported
point release, the Envoy security team will share this information with the private distributor
list, following the standard embargo process, but not create a security release. After the embargo
expires, the vulnerability will be described as a GitHub issue. A CVE will be filed if warranted by
severity.

If a vulnerability does not affect any point release but only master, additional caveats apply:

* If the issue is detected and a fix is available within 5 days of the introduction of the
  vulnerability, the fix will be publicly reviewed and landed on master. A courtesy e-mail will be
  sent to envoy-users@googlegroups.com, envoy-dev@googlegroups.com and
  cncf-envoy-distributors-announce@lists.cncf.io if the severity is medium or greater.
* If the vulnerability has been in existence for more than 5 days, we will activate the security
  release process for any medium or higher vulnerabilities. Low severity vulnerabilities will still
  be merged onto master as soon as a fix is available.

We advise distributors and operators working from the master branch to allow at least 3 days soak
time after cutting a binary release before distribution or rollout, to allow time for our fuzzers to
detect issues during their execution on ClusterFuzz. A soak period of 5 days provides an even stronger
guarantee, since we will invoke the security release process for medium or higher severity issues
for these older bugs.

### Confidentiality, integrity and availability

We consider vulnerabilities leading to the compromise of data confidentiality or integrity to be our
highest priority concerns. Availability, in particular in areas relating to DoS and resource
exhaustion, is also a serious security concern for Envoy operators, in particular those utilizing
Envoy in edge deployments.

The Envoy availability stance around CPU and memory DoS, as well as Query-of-Death (QoD), is still
evolving. We will continue to iterate and fix well known resource issues in the open, e.g. overload
manager and watermark improvements. We will activate the security process for disclosures that
appear to present a risk profile that is significantly greater than the current Envoy availability
hardening status quo. Examples of disclosures that would elicit this response:
* QoD; where a single query from a client can bring down an Envoy server.
* Highly asymmetric resource exhaustion attacks, where very little traffic can cause resource
  exhaustion, e.g. that delivered by a single client.

Note that we do not currently consider the default settings for Envoy to be safe from an availability
perspective. It is necessary for operators to explicitly configure watermarks, the overload manager,
circuit breakers and other resource related features in Envoy to provide a robust availability
story. We will not act on any security disclosure that relates to a lack of safe defaults. Over
time, we will work towards improved safe-by-default configuration, but due to backwards
compatibility and performance concerns, this will require following the breaking change deprecation
policy.

### Fix Team Organization

These steps should be completed within the first 24 hours of disclosure.

- The Fix Lead will work quickly to identify relevant engineers from the affected projects and
  packages and CC those engineers into the disclosure thread. These selected developers are the Fix
  Team.
- The Fix Lead will get the Fix Team access to private security repos to develop the fix.

### Fix Development Process

These steps should be completed within the 1-7 days of Disclosure.

- The Fix Lead and the Fix Team will create a
  [CVSS](https://www.first.org/cvss/specification-document) using the [CVSS
  Calculator](https://www.first.org/cvss/calculator/3.0). The Fix Lead makes the final call on the
  calculated CVSS; it is better to move quickly than making the CVSS perfect.
- The Fix Team will notify the Fix Lead that work on the fix branch is complete once there are LGTMs
  on all commits in the private repo from one or more maintainers.

If the CVSS score is under 4.0 ([a low severity
score](https://www.first.org/cvss/specification-document#i5)) the Fix Team can decide to slow the
release process down in the face of holidays, developer bandwidth, etc. These decisions must be
discussed on the envoy-security mailing list.

A two week window will be provided to members of the private distributor list from candidate patch
availability until the security release date. It is expected that distributors will normally be able
to perform a release within this time window. If there are exceptional circumstances, the Envoy
security team will raise this window to four weeks. The release window will be reduced if the
security issue is public or embargo is broken.

### Fix Disclosure Process

With the fix development underway, the Fix Lead needs to come up with an overall communication plan
for the wider community. This Disclosure process should begin after the Fix Team has developed a Fix
or mitigation so that a realistic timeline can be communicated to users.

**Disclosure of Forthcoming Fix to Users** (Completed within 1-7 days of Disclosure)

- The Fix Lead will email [envoy-announce@googlegroups.com](https://groups.google.com/forum/#!forum/envoy-announce)
  informing users that a security vulnerability has been disclosed and that a fix will be made
  available at YYYY-MM-DD HH:MM UTC in the future via this list. This time is the Release Date.
- The Fix Lead will include any mitigating steps users can take until a fix is available.

The communication to users should be actionable. They should know when to block time to apply
patches, understand exact mitigation steps, etc.

**Optional Fix Disclosure to Private Distributors List** (Completed within 1-14 days of Disclosure):

- The Fix Lead will make a determination with the help of the Fix Team if an issue is critical enough
  to require early disclosure to distributors. Generally this Private Distributor Disclosure process
  should be reserved for remotely exploitable or privilege escalation issues. Otherwise, this
  process can be skipped.
- The Fix Lead will email the patches to cncf-envoy-distributors-announce@lists.cncf.io so
  distributors can prepare builds to be available to users on the day of the issue's announcement.
  Distributors should read about the [Private Distributors List](#private-distributors-list) to find
  out the requirements for being added to this list.
- **What if a vendor breaks embargo?** The PST will assess the damage. The Fix Lead will make the
  call to release earlier or continue with the plan. When in doubt push forward and go public ASAP.

**Fix Release Day** (Completed within 1-21 days of Disclosure)

- The maintainers will create a new patch release branch from the latest patch release tag + the fix
  from the security branch. As a practical example if v1.5.3 is the latest patch release in Envoy.git
  a new branch will be created called v1.5.4 which includes only patches required to fix the issue.
- The Fix Lead will cherry-pick the patches onto the master branch and all relevant release branches.
  The Fix Team will LGTM and merge. Maintainers will merge these PRs as quickly as possible. Changes
  shouldn't be made to the commits even for a typo in the CHANGELOG as this will change the git sha
  of the commits leading to confusion and potentially conflicts as the fix is cherry-picked around
  branches.
- The Fix Lead will request a CVE from [DWF](https://github.com/distributedweaknessfiling/DWF-Documentation)
  and include the CVSS and release details.
- The Fix Lead will email envoy-{dev,users,announce}@googlegroups.com now that everything is public
  announcing the new releases, the CVE number, and the relevant merged PRs to get wide distribution
  and user action. As much as possible this email should be actionable and include links on how to apply
  the fix to user's environments; this can include links to external distributor documentation.
- The Fix Lead will remove the Fix Team from the private security repo.

### Retrospective

These steps should be completed 1-3 days after the Release Date. The retrospective process
[should be blameless](https://landing.google.com/sre/book/chapters/postmortem-culture.html).

- The Fix Lead will send a retrospective of the process to envoy-dev@googlegroups.com including
  details on everyone involved, the timeline of the process, links to relevant PRs that introduced
  the issue, if relevant, and any critiques of the response and release process.
- Maintainers and Fix Team are also encouraged to send their own feedback on the process to
  envoy-dev@googlegroups.com. Honest critique is the only way we are going to get good at this as a
  community.

## Private Distributors List

This list is intended to be used primarily to provide actionable information to
multiple distribution vendors at once. This list is not intended for
individuals to find out about security issues.

### Embargo Policy

The information members receive on cncf-envoy-distributors-announce must not be made public, shared, nor
even hinted at anywhere beyond the need-to-know within your specific team except with the list's
explicit approval. This holds true until the public disclosure date/time that was agreed upon by the
list. Members of the list and others may not use the information for anything other than getting the
issue fixed for your respective distribution's users.

Before any information from the list is shared with respective members of your team required to fix
said issue, they must agree to the same terms and only find out information on a need-to-know basis.

The embargo applies to information shared, source code and binary images. **It is a violation of the
embargo policy to share binary distributions of the security fixes before the public release date.**
This includes, but is not limited to, Envoy binaries and Docker images. It is expected that
distributors have a method to stage and validate new binaries without exposing them publicly.

In the unfortunate event you share the information beyond what is allowed by this policy, you _must_
urgently inform the envoy-security@googlegroups.com mailing list of exactly what information leaked
and to whom. A retrospective will take place after the leak so we can assess how to prevent making the
same mistake in the future.

If you continue to leak information and break the policy outlined here, you will be removed from the
list.

### Contributing Back

This is a team effort. As a member of the list you must carry some water. This
could be in the form of the following:

**Technical**

- Review and/or test the proposed patches and point out potential issues with
  them (such as incomplete fixes for the originally reported issues, additional
  issues you might notice, and newly introduced bugs), and inform the list of the
  work done even if no issues were encountered.

**Administrative**

- Help draft emails to the public disclosure mailing list.
- Help with release notes.

### Membership Criteria

To be eligible for the cncf-envoy-distributors-announce mailing list, your
distribution should:

1. Be either:
   1. An actively maintained distribution of Envoy components. An example is
      "SuperAwesomeLinuxDistro" which offers Envoy pre-built packages. Another
      example is "SuperAwesomeServiceMesh" which offers a service mesh product
      that includes Envoy as a component.

   OR

   2. Offer Envoy as a publicly available infrastructure or platform service, in
      which the product clearly states (e.g. public documentation, blog posts,
      marketing copy, etc.) that it is built on top of Envoy. E.g.,
      "SuperAwesomeCloudProvider's Envoy as a Service (EaaS)". An infrastructure
      service that uses Envoy for a product but does not publicly say they are
      using Envoy does not qualify. This is essentially IaaS or PaaS, if you use
      Envoy to support a SaaS, e.g. "SuperAwesomeCatVideoService", this does not
      qualify.
2. Have a user or customer base not limited to your own organization. We will use the size
   of the user or customer base as part of the criteria to determine
   eligibility.
3. Have a publicly verifiable track record up to present day of fixing security
   issues.
4. Not be a downstream or rebuild of another distribution.
5. Be a participant and active contributor in the community.
6. Accept the [Embargo Policy](#embargo-policy) that is outlined above. You must
   have a way to privately stage and validate your updates that does not violate
   the embargo.
7. Be willing to [contribute back](#contributing-back) as outlined above.
8. Be able to perform a security release of your product within a two week window from candidate fix
   patch availability.
9. Have someone already on the list vouch for the person requesting membership
   on behalf of your distribution.
10. Nominate an e-mail alias or list for your organization to receive updates. This should not be
    an individual user address, but instead a list that can be maintained by your organization as
    individuals come and go. A good example is envoy-security@seven.com, a bad example is
    acidburn@seven.com. You must accept the invite sent to this address or you will not receive any
    e-mail updates. This e-mail address will be [shared with the Envoy community](#Members).

Note that Envoy maintainers are members of the Envoy security team. [Members of the Envoy security
team](OWNERS.md#envoy-security-team) and the organizations that they represents are implicitly
included in the private distributor list. These organizations do not need to meet the above list of
criteria with the exception of the acceptance of the embargo policy.

### Requesting to Join

New membership requests are sent to envoy-security@googlegroups.com.

In the body of your request please specify how you qualify and fulfill each
criterion listed in [Membership Criteria](#membership-criteria).

Here is a pseudo example:

```
To: envoy-security@googlegroups.com
Subject: Seven-Corp Membership to cncf-envoy-distributors-announce

Below are each criterion and why I think we, Seven-Corp, qualify.

> 1. Be an actively maintained distribution of Envoy components OR offer Envoy as a publicly
     available service in which the product clearly states that it is built on top of Envoy.

We distribute the "Seven" distribution of Envoy [link]. We have been doing
this since 1999 before proxies were even cool.

> 2. Have a user or customer base not limited to your own organization. Please specify an
>    approximate size of your user or customer base, including the number of
>    production deployments.

Our user base spans of the extensive "Seven" community. We have a slack and
GitHub repos and mailing lists where the community hangs out. We have ~2000
customers, of which approximately 400 are using Seven in production. [links]

> 3. Have a publicly verifiable track record up to present day of fixing security
     issues.

We announce on our blog all upstream patches we apply to "Seven." [link to blog
posts]

> 4. Not be a downstream or rebuild of another distribution. If you offer Envoy as a publicly
>    available infrastructure or platform service, this condition does not need to apply.

This does not apply, "Seven" is a unique snowflake distribution.

> 5. Be a participant and active contributor in the community.

Our members, Acidburn, Cereal, and ZeroCool are outstanding members and are well
known throughout the Envoy community. Especially for their contributions
in hacking the Gibson.

> 6. Accept the Embargo Policy that is outlined above. You must
     have a way to privately stage and validate your updates that does not violate
     the embargo.

We accept.

> 7. Be willing to contribute back as outlined above.

We are definitely willing to help!

> 8. Be able to perform a security release of your product within a two week window from candidate fix
     patch availability.

We affirm we can spin out new security releases within a 2 week window.

> 9. Have someone already on the list vouch for the person requesting membership
>    on behalf of your distribution.

CrashOverride will vouch for the "Seven" distribution joining the distribution list.

> 10. Nominate an e-mail alias or list for your organization to receive updates. This should not be
      an individual user address, but instead a list that can be maintained by your organization as
      individuals come and go. A good example is envoy-security@seven.com, a bad example is
      acidburn@seven.com. You must accept the invite sent to this address or you will not receive any
      e-mail updates. This e-mail address will be shared with the Envoy community.
```

### Members

| E-mail                                                | Organization  |
|-------------------------------------------------------|:-------------:|
| envoy-security-team@aspenmesh.io                      | Aspen Mesh    |
| aws-app-mesh-security@amazon.com                      | AWS           |
| security@cilium.io                                    | Cilium        |
| vulnerabilityreports@cloudfoundry.org                 | Cloud Foundry |
| secalert@datawire.io                                  | Datawire      |
| google-internal-envoy-security@google.com             | Google        |
| argoprod@us.ibm.com                                   | IBM           |
| istio-security-vulnerability-reports@googlegroups.com | Istio         |
| secalert@redhat.com                                   | Red Hat       |
| envoy-security@solo.io                                | solo.io       |
| envoy-security@tetrate.io                             | Tetrate       |
| security@vmware.com                                   | VMware        |
