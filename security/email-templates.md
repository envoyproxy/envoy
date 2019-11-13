# Envoy Security Process Email Templates

This is a collection of email templates to handle various situations the security team encounters.

## Upcoming security release to envoy-announce@googlegroups.com

```
Subject: Upcoming security release of Envoy $VERSION
To: envoy-announce@googlegroups.com
Cc: envoy-security@googlegroups.com, envoy-maintainers@googlegroups.com

Hello Envoy Community,

The Envoy maintainers would like to announce the forthcoming release of Envoy
$VERSION.

This release will be made available on the $ORDINALDAY of $MONTH $YEAR at
$PDTHOUR PDT ($GMTHOUR GMT). This release will fix $NUMDEFECTS security
defect(s). The highest rated security defect is considered $SEVERITY severity.

No further details or patches will be made available in advance of the release.

Thanks,
$PERSON (on behalf of the Envoy maintainers)
```

## Upcoming security release to cncf-envoy-distributors-announce@lists.cncf.io

```
Subject: [CONFIDENTIAL] Further details on security release of Envoy $VERSION
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
...

We intend to make candidates release patches available under embargo on the
$ORDINALDAY of $MONTH $YEAR, which you may use for testing and preparing your
distributions.

Please direct further communication amongst private distributors to this list
or to envoy-security@googlegroups.com for direct communication with the Envoy
security team.

Thanks,
$PERSON (on behalf of the Envoy security team)
```

## Security Fix Announcement

```
Subject: Security release of Envoy $VERSION is now available
To: envoy-announce@googlegroups.com
Cc: envoy-security@googlegroups.com, envoy-maintainers@googlegroups.com

Hello Envoy Community,

The Envoy maintainers would like to announce the availability of Envoy $VERSION.
This addresses the following CVE(s):

* CVE-YEAR-ABCDEF (CVSS score $CVSS): $CVESUMMARY
...

Upgrading to $VERSION is encouraged to fix these issues.

GitHub tag: https://github.com/envoyproxy/envoy/releases/tag/v$VERSION
Docker images: https://hub.docker.com/r/envoyproxy/envoy/tags
Release notes: https://www.envoyproxy.io/docs/envoy/v$VERSION/intro/version_history
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

$PERSON (on behalf of the Envoy maintainers)
```
