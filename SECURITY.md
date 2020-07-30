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

* If the issue is detected and a fix is available within 7 days of the introduction of the
  vulnerability, or the issue is deemed a low severity vulnerability by the Envoy maintainer and
  security teams, the fix will be publicly reviewed and landed on master. If the severity is at least
  medium or at maintainer discretion a courtesy e-mail will be sent to envoy-users@googlegroups.com,
  envoy-dev@googlegroups.com, envoy-security-announce@googlegroups.com and
  cncf-envoy-distributors-announce@lists.cncf.io.
* If the vulnerability has been in existence for more than 7 days and is medium or higher, we will
  activate the security release process.

We advise distributors and operators working from the master branch to allow at least 5 days soak
time after cutting a binary release before distribution or rollout, to allow time for our fuzzers to
detect issues during their execution on ClusterFuzz. A soak period of 7 days provides an even stronger
guarantee, since we will invoke the security release process for medium or higher severity issues
for these older bugs.

### Threat model

See https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/threat_model.
Vulnerabilities are evaluated against this threat model when deciding whether to activate the Envoy
security release process.

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

A three week window will be provided to members of the private distributor list from candidate patch
availability until the security release date. It is expected that distributors will normally be able
to perform a release within this time window. If there are exceptional circumstances, the Envoy
security team will raise this window to four weeks. The release window will be reduced if the
security issue is public or embargo is broken.

We will endeavor not to overlap this three week window with or place it adjacent to major corporate
holiday periods or end-of-quarter (e.g. impacting downstream Istio releases), where possible.

### Fix and disclosure SLOs

* All reports to envoy-security@googlegroups.com will be triaged and have an
  initial response within 1 business day.

* Privately disclosed issues will be fixed or publicly disclosed within 90 days
  by the Envoy security team. In exceptional circumstances we reserve the right
  to work with the discloser to coordinate on an extension, but this will be
  rarely used.

* Any issue discovered by the Envoy security team and raised in our private bug
  tracker will be converted to a public issue within 90 days. We will regularly
  audit these issues to ensure that no major vulnerability (from the perspective
  of the threat model) is accidentally leaked.

* Fuzz bugs are subject to a 90 day disclosure deadline.

* Three weeks notice will be provided to private distributors from patch
  availability until the embargo deadline.

* Public zero days will be fixed ASAP, but there is no SLO for this, since this
  will depend on the severity and impact to the organizations backing the Envoy
  security team.

### Fix Disclosure Process

With the fix development underway, the Fix Lead needs to come up with an overall communication plan
for the wider community. This Disclosure process should begin after the Fix Team has developed a Fix
or mitigation so that a realistic timeline can be communicated to users.

**Disclosure of Forthcoming Fix to Users** (Completed within 1-7 days of Disclosure)

- The Fix Lead will email [envoy-security-announce@googlegroups.com](https://groups.google.com/forum/#!forum/envoy-security-announce)
  (CC [envoy-announce@googlegroups.com](https://groups.google.com/forum/#!forum/envoy-announce))
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
  distributors can prepare builds to be available to users on the day of the issue's announcement. Any 
  patches against main will be updated and resent weekly.
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
multiple distribution vendors as well as a *limited* set of high impact end users at once. *This
list is not intended in the general case for end users to find out about security issues*.

### Embargo Policy

The information members receive on cncf-envoy-distributors-announce must not be made public, shared, nor
even hinted at anywhere beyond the need-to-know within your specific team except with the list's
explicit approval. This holds true until the public disclosure date/time that was agreed upon by the
list. Members of the list and others may not use the information for anything other than getting the
issue fixed for your respective users.

Before any information from the list is shared with respective members of your team required to fix
said issue, they must agree to the same terms and only find out information on a need-to-know basis.

We typically expect a single point-of-contact (PoC) at any given legal entity. Within the
organization, it is the responsibility of the PoC to share CVE and related patches internally. This
should be performed on a strictly need-to-know basis with affected groups to the extent that this is
technically plausible. All teams should be aware of the embargo conditions and accept them.
Ultimately, if an organization breaks embargo transitively through such sharing, they will lose
the early disclosure privilege, so it's in their best interest to carefully share information internally,
following best practices and use their judgement in balancing the tradeoff between protecting users
and maintaining confidentiality.

The embargo applies to information shared, source code and binary images. **It is a violation of the
embargo policy to share binary distributions of the security fixes before the public release date.**
This includes, but is not limited to, Envoy binaries and Docker images. It is expected that
distributors have a method to stage and validate new binaries without exposing them publicly.

If the information shared is under embargo from a third party, where Envoy is one of many projects
that a disclosure is shared with, it is critical to consider that the ramifications of any leak will
extend beyond the Envoy community and will leave us in a position in which we will be less likely to
receive embargoed reports in the future.

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
use of Envoy should:

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
      using Envoy does not *generally* qualify (see option 3 that follows). This is essentially IaaS
      or PaaS. If you use Envoy to support a SaaS, e.g. "SuperAwesomeCatVideoService", this does not
      *generally* qualify.

   OR

   3. An end user of Envoy that satisfies the following requirements:
       1. Is "well known" to the Envoy community. Being "well known" is fully subjective and
          determined by the Envoy maintainers and security team. Becoming "well known" would
          generally be achieved by activities such as: PR contributions, either code or
          documentation; helping other end users on Slack, GitHub, and the mailing lists; speaking
          about use of Envoy at conferences; writing about use of Envoy in blog posts; sponsoring
          Envoy conferences, meetups, and other activities; etc. This is a more strict variant of
          item 5 below.
       2. Is of sufficient size, scale, and impact to make your inclusion on the list
          worthwhile. The definition of size, scale, and impact is fully subjective and
          determined by the Envoy maintainers and security team. The definition will not be
          discussed further in this document.
       3. You *must* smoke test and then widely deploy security patches promptly and report back
          success or failure ASAP. Furthermore, the Envoy maintainers may occasionally ask you to
          smoke test especially risky public PRs before they are merged. Not performing these tasks
          in a reasonably prompt timeframe will result in removal from the list. This is a more
          strict variant of item 7 below.
       4. In order to balance inclusion in the list versus a greater chance of accidental
          disclosure, end users added to the list via this option will be limited to a total of
          **10** slots. Periodic review (see below) may allow new slots to open, so please continue
          to apply if it seems your organization would otherwise qualify. The security team also
          reserves the right to change this limit in the future.
2. Have a user or customer base not limited to your own organization (except for option 3 above).
   We will use the size of the user or customer base as part of the criteria to determine
   eligibility.
3. Have a publicly verifiable track record up to present day of fixing security
   issues.
4. Not be a downstream or rebuild of another distribution.
5. Be a participant and active contributor in the community.
6. Accept the [Embargo Policy](#embargo-policy) that is outlined above. You must
   have a way to privately stage and validate your updates that does not violate
   the embargo.
7. Be willing to [contribute back](#contributing-back) as outlined above.
8. Be able to perform a security release of your product within a three week window from candidate fix
   patch availability.
9. Have someone already on the list vouch for the person requesting membership
   on behalf of your distribution.
10. Nominate an e-mail alias or list for your organization to receive updates. This should not be
    an individual user address, but instead a list that can be maintained by your organization as
    individuals come and go. A good example is envoy-security@seven.com, a bad example is
    acidburn@seven.com. You must accept the invite sent to this address or you will not receive any
    e-mail updates. This e-mail address will be [shared with the Envoy community](#Members).

Note that Envoy maintainers are members of the Envoy security team. [Members of the Envoy security
team](OWNERS.md#envoy-security-team) and the organizations that they represent are implicitly
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
     available service in which the product clearly states that it is built on top of Envoy OR
     be a well known end user of sufficient size, scale, and impact to make your
     inclusion worthwhile.

We distribute the "Seven" distribution of Envoy [link]. We have been doing
this since 1999 before proxies were even cool.

OR

We use Envoy for our #1 rated cat video service and have 40 billion MAU, proxying 40 trillion^2 RPS
through Envoy at the edge. Secure cat videos are our top priority. We also contribute a lot to the Envoy
community by implementing features, not making Matt ask for documentation or tests, and writing blog
posts about efficient Envoy cat video serving.

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

> 8. Be able to perform a security release of your product within a three week window from candidate fix
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

envoy-security@seven.com
```

### Review of membership criteria

In all cases, members of the distribution list will be reviewed on a yearly basis by the maintainers
and security team to ensure they still qualify for inclusion on the list.

### Members

| E-mail                                                | Organization  | End User | Last Review |
|-------------------------------------------------------|:-------------:|:--------:|:-----------:|
| envoy-security-team@aspenmesh.io                      | Aspen Mesh    | No       | 12/19       |
| aws-app-mesh-security@amazon.com                      | AWS           | No       | 12/19       |
| security@cilium.io                                    | Cilium        | No       | 12/19       |
| vulnerabilityreports@cloudfoundry.org                 | Cloud Foundry | No       | 12/19       |
| secalert@datawire.io                                  | Datawire      | No       | 12/19       |
| google-internal-envoy-security@google.com             | Google        | No       | 12/19       |
| argoprod@us.ibm.com                                   | IBM           | No       | 12/19       |
| istio-security-vulnerability-reports@googlegroups.com | Istio         | No       | 12/19       |
| secalert@redhat.com                                   | Red Hat       | No       | 12/19       |
| envoy-security@solo.io                                | solo.io       | No       | 12/19       |
| envoy-security@tetrate.io                             | Tetrate       | No       | 12/19       |
| security@vmware.com                                   | VMware        | No       | 12/19       |
| envoy-security@pinterest.com                          | Pinterest     | Yes      | 12/19       |
| envoy-security@dropbox.com                            | Dropbox       | Yes      | 01/20       |
| envoy-security-predisclosure@stripe.com               | Stripe        | Yes      | 01/20       |
