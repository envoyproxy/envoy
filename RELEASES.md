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
| 2022 Q2 | Pradeep Rao ([pradeepcrao](https://github.com/pradeepcrao))    | Matt Klein ([mattklein123](https://github.com/mattklein123)              |
| 2022 Q4 | Can Cecen ([cancecen](https://github.com/cancecen))            | Tony Allen ([tonya11en](https://github.com/tonya11en))                   |
| 2022 Q3 | Boteng Yao ([botengyao](https://github.com/botengyao))         | Kateryna Nezdolii ([nezdolik](https://github.com/nezdolik))              |
| 2022 Q4 | Paul Merrison ([pmerrison](https://github.com/pmerrison))      | Brian Sonnenberg ([briansonnenberg](https://github.com/briansonnenberg)) |

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
| 1.28.0  | 2023/10/16 | 2023/10/19 |  +3 days   | 2024/10/19  |
| 1.29.0  | 2024/01/16 |            |            |             |

### Cutting a major release

* Take a look at open issues tagged with the current release, by
  [searching](https://github.com/envoyproxy/envoy/issues) for
  "is:open is:issue milestone:[current milestone]" and either hold off until
  they are fixed or bump them to the next milestone.
* Begin marshalling the ongoing PR flow in this repo. Ask maintainers to hold off merging any
  particularly risky PRs until after the release is tagged. This is because we aim for main to be
  at release candidate quality at all times.
* Do a final check of the [release notes](changelogs/current.yaml):
  * Make any needed corrections (grammar, punctuation, formatting, etc.).
  * Check to see if any security/stable version release notes are duplicated in
    the major version release notes. These should not be duplicated.
  * Switch the repo to "release" mode by running `bazel run @envoy_repo//:release`. See the [project
    tool](tools/project/README.md#bazel-run-toolsprojectrelease) for further information. This tool
    will create a commit with the necessary changes for a release.
  * Update the [RELEASES](RELEASES.md) doc with the relevant dates. Now, or after you cut the
    release, please also make sure there's a stable maintainer signed up for next quarter,
    and the deadline for the next release is documented in the release schedule.
  * Get a review and merge.
* Create a pull request with the commit created by the project tool and **wait for tests to
  pass**.
* Once the tests have passed, and the PR has landed, CI will automatically create the tagged release and corresponding release branch.
* Craft a witty/uplifting email and send it to all the email aliases: envoy-announce@ envoy-users@ envoy-dev@ envoy-maintainers
* Make sure we tweet the new release: either have Matt do it or email social@cncf.io and ask them to do an Envoy account
  post.
* Switch the repo back to "dev" mode by running `bazel run @envoy_repo//:dev`. See the [project
  tool](tools/project/README.md#bazel-run-toolsprojectdev) for further information. This tool will create a commit with the
  necessary changes to continue development.
* Create a pull request with commit created by the project tool.
* Run the deprecate_versions.py script (`bazel run //tools/deprecate_version:deprecate_version`)


## Security release schedule

There is no fixed scheduled for security fixes. Zero-day vulnerabilities might necessitate
an emergency release with little or no warning. However, historically security release have
happened roughly once per quarter, midway between major releases.
