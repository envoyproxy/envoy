# Release Process

## Active development

Active development is happening on the `main` branch, and a new version is released from it
at the end of each quarter.

## Stable releases

Stable releases of Envoy include:

* Extended maintenance window (any version released in the last 12 months).
* Security fixes backported from the `main` branch (including those deemed not worthy
  of creating a CVE).
* Stability fixes backported from the `main` branch (anything that can result in a crash,
  including crashes triggered by a trusted control plane).
* Bugfixes, deemed worthwhile by the maintainers of stable releases.

### Hand-off

Hand-off to the maintainers of stable releases happens after Envoy maintainers release a new
version from the `main` branch by creating a `vX.Y.0` tag and a corresponding `release/vX.Y`
branch, with merge permissions given to the release manager of stable releases, and CI configured
to execute tests on it.

### Security releases

Critical security fixes are owned by the Envoy security team, which provides fixes for the
`main` branch, and the latest release branch. Once those fixes are ready, the maintainers
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

Release managers of stable releases are responsible for approving and merging backports, tagging
stable releases and sending announcements about them. This role is rotating on a quarterly basis.

| Quarter |       Release manager                                          |
|:-------:|:--------------------------------------------------------------:|
| 2020 Q1 | Piotr Sikora ([PiotrSikora](https://github.com/PiotrSikora))   |
| 2020 Q2 | Piotr Sikora ([PiotrSikora](https://github.com/PiotrSikora))   |
| 2020 Q3 | Yuchen Dai ([lambdai](https://github.com/lambdai))             |
| 2020 Q4 | Christoph Pakulski ([cpakulski](https://github.com/cpakulski)) |
| 2021 Q1 | Rei Shimizu ([Shikugawa](https://github.com/Shikugawa))        |
| 2021 Q2 | Dmitri Dolguikh ([dmitri-d](https://github.com/dmitri-d))      |
| 2021 Q3 | Takeshi Yoneda ([mathetake](https://github.com/mathetake))     |

## Release schedule

In order to accommodate downstream projects, new Envoy releases are produced on a fixed release
schedule (at the end of each quarter), with an acceptable delay of up to 2 weeks, with a hard
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
| 1.20.0  | 2021/09/30 |            |            |             |

[repokitteh]: https://github.com/repokitteh
