# Envoy Security Process Email Templates

This is a collection of email templates to handle various situations the security team encounters.

## Upcoming security release to envoy-security-announce@googlegroups.com

```
Subject: Upcoming security release of Envoy $VERSION
To: envoy-security-announce@googlegroups.com
Cc: envoy-announce@googlegroups.com, envoy-security@googlegroups.com, envoy-maintainers@googlegroups.com

Hello Envoy Community,

The Envoy security team would like to announce the forthcoming release of Envoy
$VERSION.

This release will be made available on the $ORDINALDAY of $MONTH $YEAR at
$PDTHOUR PDT ($GMTHOUR GMT). This release will fix $NUMDEFECTS security
defect(s). The highest rated security defect is considered $SEVERITY severity.

No further details or patches will be made available in advance of the release.

Thanks,
$PERSON (on behalf of the Envoy security team and maintainers)
```

## Upcoming security release to cncf-envoy-distributors-announce@lists.cncf.io

```
Subject: [CONFIDENTIAL] Upcoming security release of Envoy $VERSION
To: cncf-envoy-distributors-announce@lists.cncf.io
Cc: envoy-security@googlegroups.com

Hello Envoy Distributors,

The Envoy security team would like to provide advanced notice to the Envoy
Private Distributors List of some details on the pending Envoy $VERSION
security release, following the process described at
https://github.com/envoyproxy/envoy/blob/master/SECURITY.md.

This release will be made available on the $ORDINALDAY of $MONTH $YEAR at
$PDTHOUR PDT ($GMTHOUR GMT). This release will fix $NUMDEFECTS security
defect(s). The highest rated security defect is considered $SEVERITY severity.

Below we provide details of these vulnerabilities under our embargo policy
(https://github.com/envoyproxy/envoy/blob/master/SECURITY.md#embargo-policy).
This information should be treated as confidential until public release by the
Envoy maintainers on the Envoy GitHub.

We will address the following CVE(s):

* CVE-YEAR-ABCDEF (CVSS score $CVSS, $SEVERITY): $CVESUMMARY
  - Link to the appropriate section of the CVE writeup document with gh-cve-template.md content.
...

We intend to make candidates release patches available under embargo on the
$ORDINALDAY of $MONTH $YEAR, which you may use for testing and preparing your
distributions.

Please direct further communication amongst private distributors to this list
or to envoy-security@googlegroups.com for direct communication with the Envoy
security team.

Thanks,
$PERSON (on behalf of the Envoy security team and maintainers)
```

## Candidate release patches to cncf-envoy-distributors-announce@lists.cncf.io

```
Subject: [CONFIDENTIAL] Further details on security release of Envoy $VERSION
To: cncf-envoy-distributors-announce@lists.cncf.io
Cc: envoy-security@googlegroups.com

Hello Envoy Distributors,

Please find attached candidate patches for CVE-YEAR-ABCDEF. You may use the
attached patches for testing and preparing your distributions. The patches can
be applied with "git am".

Patches starting with "$VERSION" should be applied against the $OLDVERSION release.

Patches starting with "master-" should be applied against commit $COMMIT.

As a reminder, these patches are under embargo until $ORDINALDAY of $MONTH $YEAR
at $PDTHOUR PDT ($GMTHOUR GMT). The information below should be treated as
confidential and shared only on a need-to-know basis. The rules outline in our
embargo policy
(https://github.com/envoyproxy/envoy/blob/master/SECURITY.md#embargo-policy)
still apply, and it is extremely important that any communication related to
these CVEs are not forwarded further.

No fixes should be made publicly available, either in binary or source form,
before the aforementioned disclosure date.

We would appreciate any feedback on these patches. Please direct further
communication amongst private distributors to this list or to
envoy-security@googlegroups.com for direct communication with the Envoy
security team.

Thanks,
$PERSON (on behalf of the Envoy security team and maintainers)
```

## Security Fix Announcement

```
Subject: Security release of Envoy $VERSION is now available
To: envoy-security-announce@googlegroups.com
Cc: envoy-announce@googlegroups.com, envoy-security@googlegroups.com, envoy-maintainers@googlegroups.com

Hello Envoy Community,

The Envoy security team would like to announce the availability of Envoy $VERSION.
This addresses the following CVE(s):

* CVE-YEAR-ABCDEF (CVSS score $CVSS): $CVESUMMARY
...

Upgrading to $VERSION is encouraged to fix these issues.

GitHub tag: https://github.com/envoyproxy/envoy/releases/tag/v$VERSION
Docker images: https://hub.docker.com/r/envoyproxy/envoy/tags
Release notes: https://www.envoyproxy.io/docs/envoy/v$VERSION/version_history/current.rst
Docs: https://www.envoyproxy.io/docs/envoy/v$VERSION/

**Am I vulnerable?**

Run `envoy --version` and if it indicates a base version of $OLDVERSION or
older you are running a vulnerable version.

<!-- Provide details on features, extensions, configuration that make it likely that a system is
vulnerable in practice. -->

**How do I mitigate the vulnerability?**

<!--
[This is an optional section. Remove if there are no mitigations.]
-->

Avoid the use of feature XYZ in Envoy configuration.

**How do I upgrade?**

Update to $VERSION via your Envoy distribution or rebuild from the Envoy GitHub
source at the $VERSION tag or HEAD @ master.

**Vulnerability Details**

<!--
[For each CVE]
-->

***CVE-YEAR-ABCDEF***

$CVESUMMARY

This issue is filed as $CVE. We have rated it as [$CVSSSTRING]($CVSSURL)
($CVSS, $SEVERITY) [See the GitHub issue for more details]($GITHUBISSUEURL)

**Thank you**

Thank you to $REPORTER, $DEVELOPERS, and the $RELEASEMANAGERS for the
coordination in making this release.

Thanks,

$PERSON (on behalf of the Envoy security team and maintainers)
```
