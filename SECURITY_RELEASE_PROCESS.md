# Security Release Process

Envoy is a large growing community of volunteers, users, and vendors. The Envoy community has
adopted this security disclosures and response policy to ensure we responsibly handle critical
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
  calculated CVSS; it is better to move quickly than make the CVSS perfect.
- The Fix Team will notify the Fix Lead that work on the fix branch is complete once there are LGTMs
  on all commits in the private repo from one or more maintainers.

If the CVSS score is under 4.0 ([a low severity
score](https://www.first.org/cvss/specification-document#i5)) the Fix Team can decide to slow the
release process down in the face of holidays, developer bandwidth, etc. These decisions must be
discussed on the envoy-security mailing list.

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
- The Fix Lead will email the patches to envoy-distributors-announce@googlegroups.com so
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

The information members receive on envoy-distributors-announce must not be made public, shared, nor
even hinted at anywhere beyond the need-to-know within your specific team except with the list's
explicit approval. This holds true until the public disclosure date/time that was agreed upon by the
list. Members of the list and others may not use the information for anything other than getting the
issue fixed for your respective distribution's users.

Before any information from the list is shared with respective members of your team required to fix
said issue, they must agree to the same terms and only find out information on a need-to-know basis.

In the unfortunate event you share the information beyond what is allowed by this policy, you _must_
urgently inform the envoy-security@googlegroups.com mailing list of exactly what information leaked
and to whom. A retrospective will take place after the leak so we can assess how to not make the
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

To be eligible for the envoy-distributors-announce mailing list, your
distribution should:

1. Be an actively maintained distribution of Envoy components OR offer Envoy as a publicly
   available service in which the product clearly states that it is built on top of Envoy. E.g.,
   "SuperAwesomeLinuxDistro" which offers Envoy pre-built packages OR
   "SuperAwesomeCloudProvider's Envoy as a Service (EaaS)". A cloud service that uses Envoy for a
   product but does not publicly say they are using Envoy does not qualify.
2. Have a user base not limited to your own organization.
3. Have a publicly verifiable track record up to present day of fixing security
   issues.
4. Not be a downstream or rebuild of another distribution.
5. Be a participant and active contributor in the community.
6. Accept the [Embargo Policy](#embargo-policy) that is outlined above.
7. Be willing to [contribute back](#contributing-back) as outlined above.
8. Have someone already on the list vouch for the person requesting membership
   on behalf of your distribution.

### Requesting to Join

New membership requests are sent to envoy-security@googlegroups.com.

In the body of your request please specify how you qualify and fulfill each
criterion listed in [Membership Criteria](#membership-criteria).

Here is a pseudo example:

```
To: envoy-security@googlegroups.com
Subject: Seven-Corp Membership to envoy-distributors-announce

Below are each criterion and why I think we, Seven-Corp, qualify.

> 1. Be an actively maintained distribution of Envoy components OR offer Envoy as a publicly
     available service in which the product clearly states that it is built on top of Envoy.

We distribute the "Seven" distribution of Envoy [link]. We have been doing
this since 1999 before proxies were even cool.

> 2. Have a user base not limited to your own organization.

Our user base spans of the extensive "Seven" community. We have a slack and
GitHub repos and mailing lists where the community hangs out. [links]

> 3. Have a publicly verifiable track record up to present day of fixing security
     issues.

We announce on our blog all upstream patches we apply to "Seven." [link to blog
posts]

> 4. Not be a downstream or rebuild of another distribution.

This does not apply, "Seven" is a unique snowflake distribution.

> 5. Be a participant and active contributor in the community.

Our members, Acidburn, Cereal, and ZeroCool are outstanding members and are well
known throughout the Envoy community. Especially for their contributions
in hacking the Gibson.

> 6. Accept the Embargo Policy that is outlined above.

We accept.

> 7. Be willing to contribute back as outlined above.

We are definitely willing to help!

> 8. Have someone already on the list vouch for the person requesting membership
     on behalf of your distribution.

CrashOverride will vouch for Acidburn joining the list on behalf of the "Seven"
distribution.
```
