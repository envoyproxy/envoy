# Release Process

## Active development

Active development is happening on the `main` branch, and a new version is released from it.

## Stable releases

Stable releases of Envoy include:

* Major releases in which a new version a created directly from the `main` branch.
* Minor releases for versions covered by the extended maintenance window (any version released in the last 12 months).
  * Security fixes backported from the `main` branch (including those deemed not worthy
    of creating a CVE).
  * Stability fixes backported from the `main` branch (anything that can result in a crash,
    including crashes triggered by a trusted control plane).
  * Bugfixes, deemed worthwhile by the maintainers of stable releases.

Major releases happen quartely and follow the schedule below. Security fixes typically happen
quarterly as well, but this depends on the number and severity of security bugs. Other releases
are ad-hoc and best-effort.

### Security releases

Critical security fixes are owned by the Envoy security team, which provides fixes for the
`main` branch. Once those fixes are ready, the maintainers
of stable releases backport them to the remaining supported stable releases.

### Backports

All other security and reliability fixes can be nominated for backporting to stable releases
by Envoy maintainers, Envoy security team, the change author, or members of the Envoy community
by adding the `backport/review` or `backport/approved` label (this can be done using [repokitteh]'s
`/backport` command). Changes nominated by the change author and/or members of the Envoy community
are evaluated for backporting on a case-by-case basis, and require approval from either the release
manager of stable release, Envoy maintainers, or Envoy security team. Once approved, those fixes
are backported from the `main` branch to all supported stable branches by the maintainers of
stable releases. New stable versions from non-critical security fixes are released on a regular
schedule, initially aiming for the bi-weekly releases.

### Release management

Major releases are handled by the maintainer on-call and do not involve any backports.
The details are outlined in the "Cutting a major release" section below.
Security releases are handled by a Release Manager and a Fix Lead. The Release Manager is
responsible for approving and merging backports, with responsibilties outlined
in [BACKPORTS.md](BACKPORTS.md).
The Fix Lead is a member of the security
team and is responsible for coordinating the overall release. This includes identifying
issues to be fixed in the release, communications with the Envoy community, and the
actual mechanics of the release itself.

| Quarter |       Release Manager                                          |         Fix Lead                                                         |
|:-------:|:--------------------------------------------------------------:|:-------------------------------------------------------------------------|
| 2020 Q1 | Piotr Sikora ([PiotrSikora](https://github.com/PiotrSikora))   |                                                                          |
| 2020 Q2 | Piotr Sikora ([PiotrSikora](https://github.com/PiotrSikora))   |                                                                          |
| 2020 Q3 | Yuchen Dai ([lambdai](https://github.com/lambdai))             |                                                                          |
| 2020 Q4 | Christoph Pakulski ([cpakulski](https://github.com/cpakulski)) |                                                                          |
| 2021 Q1 | Rei Shimizu ([Shikugawa](https://github.com/Shikugawa))        |                                                                          |
| 2021 Q2 | Dmitri Dolguikh ([dmitri-d](https://github.com/dmitri-d))      |                                                                          |
| 2021 Q3 | Takeshi Yoneda ([mathetake](https://github.com/mathetake))     |                                                                          |
| 2021 Q4 | Otto van der Schaaf ([oschaaf](https://github.com/oschaaf))    |                                                                          |
| 2022 Q1 | Otto van der Schaaf ([oschaaf](https://github.com/oschaaf))    | Ryan Hamilton ([RyanTheOptimist](https://github.com/RyanTheOptimist))    |
| 2022 Q2 | Pradeep Rao ([pradeepcrao](https://github.com/pradeepcrao))    | Matt Klein ([mattklein123](https://github.com/mattklein123))             |
| 2022 Q4 | Can Cecen ([cancecen](https://github.com/cancecen))            | Tony Allen ([tonya11en](https://github.com/tonya11en))                   |
| 2023 Q3 | Boteng Yao ([botengyao](https://github.com/botengyao))         | Kateryna Nezdolii ([nezdolik](https://github.com/nezdolik))              |
| 2023 Q4 | Paul Merrison ([pmerrison](https://github.com/pmerrison))      | Brian Sonnenberg ([briansonnenberg](https://github.com/briansonnenberg)) |

## Major release schedule

In order to accommodate downstream projects, new Envoy releases are produced on a fixed release
schedule (the 15th day of each quarter), with an acceptable delay of up to 2 weeks, with a hard
deadline of 3 weeks.

| Version |  Expected  |   Actual   | Difference | End of Life |
|:-------:|:----------:|:----------:|:----------:|:-----------:|
| 1.12.0  | 2019/09/30 | 2019/10/31 |  +31 days  | 2020/10/31  |
| 1.13.0  | 2019/12/31 | 2020/01/20 |  +20 days  | 2021/01/20  |
| 1.14.0  | 2020/03/31 | 2020/04/08 |   +8 days  | 2021/04/08  |
| 1.15.0  | 2020/06/30 | 2020/07/07 |   +7 days  | 2021/07/07  |
| 1.16.0  | 2020/09/30 | 2020/10/08 |   +8 days  | 2021/10/08  |
| 1.17.0  | 2020/12/31 | 2021/01/11 |  +11 days  | 2022/01/11  |
| 1.18.0  | 2021/03/31 | 2021/04/15 |  +15 days  | 2022/04/15  |
| 1.19.0  | 2021/06/30 | 2021/07/13 |  +13 days  | 2022/07/13  |
| 1.20.0  | 2021/09/30 | 2021/10/05 |   +5 days  | 2022/10/05  |
| 1.21.0  | 2022/01/15 | 2022/01/12 |   -3 days  | 2023/01/12  |
| 1.22.0  | 2022/04/15 | 2022/04/15 |    0 days  | 2023/04/15  |
| 1.23.0  | 2022/07/15 | 2022/07/15 |    0 days  | 2023/07/15  |
| 1.24.0  | 2022/10/15 | 2022/10/19 |   +4 days  | 2023/10/19  |
| 1.25.0  | 2023/01/15 | 2023/01/18 |   +3 days  | 2024/01/18  |
| 1.26.0  | 2023/04/15 | 2023/04/18 |   +3 days  | 2024/04/18  |
| 1.27.0  | 2023/07/14 | 2023/07/27 |  +13 days  | 2024/07/27  |
| 1.28.0  | 2023/10/16 | 2023/10/19 |   +3 days  | 2024/10/19  |
| 1.29.0  | 2024/01/16 | 2024/01/16 |    0 days  | 2025/01/16  |
| 1.30.0  | 2024/04/16 |            |            |             |

### Cutting a major release

#### In the week prior to the release

* Check the [release milestone](https://github.com/envoyproxy/envoy/milestones).
* Take a look at open issues tagged with the current release, by
  [searching](https://github.com/envoyproxy/envoy/issues) for
  "is:open is:issue milestone:[current milestone]" and either hold off until
  they are fixed or bump them to the next milestone.
* Begin marshalling the ongoing PR flow in this repo.
* Ask maintainers to hold off merging any particularly risky PRs until after the release is tagged.
* Conversely, encourage maintainers to land anything that should be included.
* Do a final check of the [release notes](changelogs/current.yaml):
  * Make any needed corrections (grammar, punctuation, formatting, etc.).
  * Check to see if any security/stable version release notes are duplicated in
    the major version release notes. These should not be duplicated.
* Make sure there's a stable maintainer signed up for next quarter.
* Select a date for the next scheduled release.

#### The day before release

* Create a release PR running the Github workflow `create-release` from:

    https://github.com/envoyproxy/envoy/actions/workflows/envoy-release.yml

  This will warm the caches and ensure the workflow CI can run correctly.
* Create a summary by updating the markdown in the [release summary](changelogs/summary.md).
* Re-check the steps [above](#in-the-week-prior-to-the-release)
* Clear the [release milestone](https://github.com/envoyproxy/envoy/milestones) as far as possible, either by finding resolutions or removing issues/PRs.

#### On the day of the release

* Re-create the release PR and lock the branch.
* If necessary, make any last minute adjustments to the summary in the PR. The commit message will be used to create the release.
* Once happy, approve the PR and **wait for tests to pass**, before landing.
* When the PR has landed, CI will automatically create the [tagged release](https://github.com/envoyproxy/envoy/releases) and corresponding [release branch](https://github.com/envoyproxy/envoy/branches/all?query=release%2Fv).
* Create a PR to switch the repo back to "dev" mode by running `bazel run @envoy_repo//:dev`.
* It is important that no commits land until this PR lands, and once it does the branch should be unlocked.
* Publishing docs can take some time as they are first [archived](https://github.com/envoyproxy/archive) before [updating the website](https://app.netlify.com/sites/envoy-website/deploys).
* Create a PR to update the [RELEASES](RELEASES.md) readme with the actual release date, the date for the next release and stable maintainer info.
* Run the `deprecate_versions.py` script (`bazel run //tools/deprecate_version:deprecate_version`)
* If you haven't done this before, request posting permission from admins for all the groups in the next bullet.
* Craft a witty/uplifting email and send it to all the email aliases:
  - envoy-announce@googlegroups.com
  - envoy-users@googlegroups.com
  - envoy-dev@googlegroups.com
  - envoy-maintainers@googlegroups.com -
  Include in this email a link to the latest [release page](https://github.com/envoyproxy/envoy/releases) (ending in `tag/[version]`)
* Announce in Slack channels:
  - [#envoy-dev](https://envoyproxy.slack.com/archives/C78HA81DH)
  - [#envoy-users](https://envoyproxy.slack.com/archives/C78M4KW76)
* Close the old [release milestone](https://github.com/envoyproxy/envoy/milestones) (which should be empty!) and create a new one.
* When the docs have been published to the [website](https://www.envoyproxy.io), create a PR to update the version history by running the Github workflow `sync-versions` from:

    https://github.com/envoyproxy/envoy/actions/workflows/envoy-release.yml

## Security release schedule

There is no fixed scheduled for security fixes. Zero-day vulnerabilities might necessitate
an emergency release with little or no warning. However, historically security release have
happened roughly once per quarter, midway between major releases.
