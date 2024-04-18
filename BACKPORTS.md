As documented in [RELEASES.md](RELEASES.md), the individual(s) on backports rotation are responsible for doing backports for security releases, and other qualifying backports.

Backports rotation folks should make sure they have [triage](https://github.com/orgs/envoyproxy/teams/envoy-triage) rights (ask one of the [admins](https://github.com/orgs/envoyproxy/teams/admins) to give you access if you do not otherwise have it), and regularly triage [backports-review](https://github.com/pulls?q=+label%3Abackport%2Freview+) issues, either removing the backports-review tag and adding backports-approved, or pushing back in comments for why the issue does not qualify based on [RELEASES.md](RELEASES.md)

For approved PRs, the person on rotation should backport them to supported binaries, based on the support window also in [RELEASES.md](RELEASES.md)

The individual [branches](https://github.com/envoyproxy/envoy/branches) can be checked out per normal github workflow, and reviews for backport PRs sent to the original PR author and/or reviewers for approval.

See [#17814](https://github.com/envoyproxy/envoy/pull/17814) as an example backport, referencing the original PR

See the [fix lead checklist](https://docs.google.com/document/d/1cuU0m9hTQ73Te3i06-8LjQkFVn83IL22FbCoc_4IFEY/edit#heading=h.ioqzv16eyrfa) for git commands to backport an example patch.  Backports rotation assignee may or may not be the fix lead for a given security release.

When the maintainer team kicks off a security release, the fix lead should add backports rotation to the release-specific slack channel for general coordination, and one of the [admins](https://github.com/orgs/envoyproxy/teams/admins) should grant access to [envoy-setec](https://github.com/envoyproxy/envoy-setec) repo.  As issues tagged [cve-next](https://github.com/envoyproxy/envoy-setec/issues?q=is%3Aissue+is%3Aopen+label%3Acve%2Fnext) are merged into main, the backports folks are responsible for backporting to the supported releases.  In this case, fix lead is responsible for kicking off the actual release branches.

For security backports best-practices include creating a patch per backport to make sure they individually pass CI, and then creating one rollup patch which is those patches combined, to make sure they merge cleanly.
