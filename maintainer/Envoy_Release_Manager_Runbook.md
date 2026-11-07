# Envoy Release Manager Runbook

*Quarterly releases, stable patch releases, and zero-day response*

**Audience:** New Envoy release managers and fix leads

## How to use this runbook

Start with the path that matches the release you are cutting. Use the common preflight first, then follow either the quarterly x.y.0 path or the stable/security patch path. The zero-day section modifies the security path; it does not remove the technical release gates.

> [!IMPORTANT]
> **Core rule:** The release is not a single workflow click. It is an ordered chain of scope control, backports, changelog preparation, generated release PRs, CI/publishing, verification, communications, and branch reopening.

> [!NOTE]
> **Example policy:** Every example in this runbook links to public Envoy GitHub material. When a gate should not create a PR—such as embargo approval, workflow verification, or an announcement—the runbook labels that boundary explicitly and uses only the closest public evidence.

| Release type | Use this path | Defining difference |
| --- | --- | --- |
| Quarterly x.y.0 | Section 4 | Cut from main; no backports; the release event creates the new release branch. |
| Stable/security patch | Section 5 | Cut the supported `release/vX.Y` branches, normally oldest to newest, after all approved backports land. |
| Zero-day emergency | Section 6 + Section 5 | Public disclosure compresses coordination and communications, but fixes and supported backports must still be ready. |

## 1. Roles, access, and authority

### 1.1 Name the owners before work starts

| Role | Primary responsibilities |
| --- | --- |
| Release Manager | Own the ordered checklist; triage and merge backports; run release workflows; monitor CI; verify publication; coordinate branch reopening and post-release cleanup. |
| Fix Lead | Own security scope, severity decisions, disclosure timing, advisory readiness, and the authoritative list of fixes. Assign communications explicitly. |
| Maintainer reviewers | Review main, backport, changelog, release, and dev-mode PRs; provide fast approvals during the release window. |
| Communications owner | Draft and send distributor, user, mailing-list, and Slack announcements; confirm permissions before release day. |

> [!CAUTION]
> **Clarify at kickoff:** Record the exact split between Fix Lead and Release Manager in the tracking issue; do not rely on an implied handoff.

### 1.2 Required access

- Envoy GitHub triage or maintainer rights, including the ability to label, approve, merge, run Actions workflows, and edit releases.

- Access to the project's private security channels and tooling for embargoed work.

- Access to the Envoy/release workflow and to CI logs and artifacts.

- Posting rights for the Envoy announcement mailing lists and permission to notify #envoy-dev and #envoy-users. Request @here permission before release day if needed.

### 1.3 Source of authority

Use one release tracking issue or checklist as the operational source of truth. For a security release, keep embargoed scope in the private security system; a public issue may contain dates or high-level status only after the security team approves disclosure.

| Tracking field | Required content |
| --- | --- |
| Ownership | Release Manager, Fix Lead, communications owner, backup reviewer. |
| Schedule | Patch/tarball date, distributor notification, embargo end/public date, target release window, timezone. |
| Scope | Approved fix list, supported branch matrix, zero-day handling. |
| Execution | Main PRs, individual backports, rollup/stack status, changelog summaries, workflow runs, generated PRs. |
| Exit | Published tags, artifact verification, docs/images, announcements, dev-mode PRs, sync PR, follow-up bugs. |

## 2. Common preflight

Complete these gates for every release. The timing may compress in an emergency, but skipping a gate must be an explicit maintainer decision with an owner and follow-up.

1. **Confirm scope and supported branches.** Resolve the current support window from `RELEASES.md`. For patch releases, include every affected supported branch. Remove or explicitly defer ambiguous candidates.

> **Past public example:** [PR #45990 - update Q2/Q3 release details](https://github.com/envoyproxy/envoy/pull/45990)

2. **Clear release-blocking branch failures.** If main or a release branch is failing, land the blocker on main and backport it before generating release PRs. The June 23 dependency workaround landed on main and was backported before the final release PR.

> **Past public example:** [PR #45766 - fix the blocker on main](https://github.com/envoyproxy/envoy/pull/45766); [PR #45770 - backport it to v1.35](https://github.com/envoyproxy/envoy/pull/45770)

3. **Triage pending backports.** Review backport/review items, especially closed or nearly complete fixes. Promote qualifying security/reliability fixes, reject out-of-policy requests, and confirm branch coverage.

> **Past public example:** [PR #45603 - accepted v1.35 backport](https://github.com/envoyproxy/envoy/pull/45603)

4. **Stabilize CI.** Deflake release branches. Repeated retries are a warning that release verification is unreliable, not a normal operating mode.

> **Past public example:** [PR #45766 - dependency workaround that unblocked release CI](https://github.com/envoyproxy/envoy/pull/45766)

5. **Check base images and scanners.** Confirm Ubuntu and distroless image bumps are current on main and inherited by release branches. Run the CVE/dependency scan expected for the release.

> **Past public example:** [PR #46096 - Ubuntu image bump](https://github.com/envoyproxy/envoy/pull/46096); [PR #45855 - distroless base bump](https://github.com/envoyproxy/envoy/pull/45855). *Scanner execution is a workflow/check, not a PR.*

6. **Prepare summaries and communications.** Write changelog summaries only after the final backport set is known. Prepare distributor and public drafts from the same approved scope list.

> **Past public example:** [PR #46119 - v1.39.0 release summary](https://github.com/envoyproxy/envoy/pull/46119). *Announcements are external communications and do not create a repository PR.*

7. **Arrange reviewers and permissions.** Pre-brief maintainers who can approve backport, release, and dev-mode PRs. Verify mailing-list and Slack announcement permissions.

> **Past public example:** [PR #46121 - public review record for the final v1.39.0 PR](https://github.com/envoyproxy/envoy/pull/46121). *Slack and mailing-list permissions are a coordination gate, not a PR.*

## 3. The Envoy/release workflow

**Workflow:** [Envoy/release](https://github.com/envoyproxy/envoy/actions/workflows/envoy-release.yml)

Select the branch first: main for an x.y.0 release; `release/vX.Y` for a patch release. The workflow runs the release tool and creates a bot PR from `release/create/<branch>`.

| Input | Recommended use |
| --- | --- |
| task | create-release for the release PR; reopen-branch after publication; sync-version-histories after a release set; deprecate-guards only when a manual rerun is required. |
| dry-run | false for a real run. A dry run does not replace a WIP PR because reviewers need the generated diff and CI. |
| pr | true so the workflow opens the generated PR. |
| wip | true for an early warm-up/rehearsal; false only when scope and summary are final. |
| summary | true for anything that can publish. The workflow rejects an empty `changelogs/summary.md` unless this is disabled; disabling it creates a WIP state and must not be merged. |
| author | Set an explicit human release author as `Name <email>` when practical. The workflow validates the email and records release responsibility/signoff. |
| pr-message | Optional context such as a tracking issue or an unusual release constraint. |

> [!WARNING]
> **Never squash the generated release PR:** The publish path derives the release text from the release commit. A squash merge can replace that body with only the PR title. Use rebase merge so the generated release commit message survives; verify the published release body immediately.

## 4. Quarterly x.y.0 release from main

### 4.1 Before generating the release PR

8. **Review the milestone.** Resolve open issues for the current milestone or move them deliberately to the next milestone.

> **Past public example:** [PR #46118 - release-critical Bazel 8 changelog item found at final review](https://github.com/envoyproxy/envoy/pull/46118). *The milestone review itself is issue/milestone triage and has no dedicated PR.*

9. **Marshal main.** Ask maintainers to hold risky changes until after the tag. Main should remain at release-candidate quality.

> **Past public example:** [PR #46118 - controlled final correction before the release PR](https://github.com/envoyproxy/envoy/pull/46118). *The temporary change hold is maintainer coordination, not a PR.*

10. **Audit `changelogs/current`.** Correct grammar and formatting, remove duplicates from prior stable/security notes, and ensure important changes such as toolchain bumps are called out.

> **Past public example:** [PR #46118 - add a missing Bazel 8 behavior-change entry](https://github.com/envoyproxy/envoy/pull/46118)

11. **Write `changelogs/summary.md`.** Create a release-level summary with breaking changes, security items, major feature areas, image/docs links, and a full-changelog comparison.

> **Past public example:** [PR #46119 - add the v1.39.0 summary](https://github.com/envoyproxy/envoy/pull/46119)

12. **Update `RELEASES.md`.** Record the actual date, end-of-life date, next schedule, and next release-management owners.

> **Past public example:** [PR #45990 - update Q2/Q3 release details](https://github.com/envoyproxy/envoy/pull/45990); [PR #46046 - add the six-week security cadence](https://github.com/envoyproxy/envoy/pull/46046)

13. **Complete scanner and image checks.** Record CVE scanner and Ubuntu/distroless status in the maintainer coordination thread.

> **Past public example:** [PR #46096 - Ubuntu image bump](https://github.com/envoyproxy/envoy/pull/46096); [PR #45855 - distroless base bump](https://github.com/envoyproxy/envoy/pull/45855). *The scanner result is recorded as a check/workflow rather than a PR.*

### 4.2 Generate, review, and publish

14. **Run a WIP warm-up.** Dispatch Envoy/release on main with `task=create-release`, `pr=true`, `wip=true`, `summary=true`. Review the generated diff and release text without treating it as final.

> **Past public example:** [PR #46117 - WIP v1.39.0 generated release PR](https://github.com/envoyproxy/envoy/pull/46117)

15. **Land final changelog corrections.** If a last-minute changelog PR is needed, merge it first and wait for the main branch to be green. Close or supersede the warm-up PR.

> **Past public example:** [PR #46118 - missing Bazel 8 entry](https://github.com/envoyproxy/envoy/pull/46118); [PR #46119 - final v1.39.0 summary](https://github.com/envoyproxy/envoy/pull/46119)

16. **Run the final workflow.** Dispatch again from the latest main with `wip=false` and an explicit author. Confirm the generated PR is based on the current main commit.

> **Past public example:** [PR #46121 - final generated v1.39.0 release PR](https://github.com/envoyproxy/envoy/pull/46121)

17. **Review the generated release PR.** Check VERSION, changelog conversion, docs version data, release message, links, and signoffs. Run formatting and all required checks.

> **Past public example:** [PR #46121 - generated VERSION and v1.39.0 changelog](https://github.com/envoyproxy/envoy/pull/46121)

18. **Rebase-merge after CI passes.** Do not squash. The merge triggers the request/publish pipeline, which builds, signs, publishes, and verifies release artifacts. The x.y.0 release event also creates `release/vX.Y` and runs guard deprecation automation.

> **Past public example:** [PR #45805 - preserved full release commit body for v1.35.13](https://github.com/envoyproxy/envoy/pull/45805); [PR #46121 - counterexample: squash result lost the body](https://github.com/envoyproxy/envoy/pull/46121)

19. **Verify before announcing.** Confirm the GitHub tag/release, body, assets, container images, documentation, release notes, full-changelog link, and `release/vX.Y` branch.

> **Past public example:** [PR #45805 - source release PR for the published v1.35.13 artifacts](https://github.com/envoyproxy/envoy/pull/45805). *The actual verification result is a workflow/release artifact, not another PR.*

### 4.3 Return main to development

20. **Run reopen-branch on main.** Use the same Envoy/release workflow with `task=reopen-branch`. This creates the dev-mode PR for the next x.y.0 version and reorganizes changelogs.

> **Past public example:** [PR #46140 - generated main dev-mode PR for v1.40.0](https://github.com/envoyproxy/envoy/pull/46140)

21. **Merge the dev-mode PR.** Wait for required checks. Confirm VERSION contains the next development version and `changelogs/current` exists.

> **Past public example:** [PR #46140 - main returned to v1.40.0 development](https://github.com/envoyproxy/envoy/pull/46140)

22. **Sync version histories if needed.** Run `task=sync-version-histories` on main after the released version set is stable, then review and merge the generated PR.

> **Past public example:** [PR #45876 - synchronize v1.35.13-v1.38.3 histories](https://github.com/envoyproxy/envoy/pull/45876)

23. **Announce.** Send the mailing-list announcement and post to #envoy-dev and #envoy-users with the final release link.

> **Past public example:** [PR #46121 - release PR supplying the public release content](https://github.com/envoyproxy/envoy/pull/46121). *The mailing-list and Slack posts themselves do not create a PR.*

## 5. Stable/security patch release

### 5.1 Scope, embargo, and patch preparation

24. **Freeze the authoritative fix list.** The Fix Lead approves the issues included in the release. Record scope decisions in the private release tracker and publish only the approved release scope.

> **Past public example:** [PR #45795 - public post-disclosure v1.35 Q2 patch rollup](https://github.com/envoyproxy/envoy/pull/45795). *The pre-disclosure scope decision belongs in the private tracker, not a public PR.*

25. **Land every fix on main first.** Do not create release PRs while required main fixes are missing. A public emergency may change disclosure timing, not the requirement for a reviewed fix.

> **Past public example:** [PR #45766 - main fix before release-branch backports](https://github.com/envoyproxy/envoy/pull/45766)

26. **Create one backport per fix and branch.** Cherry-pick the exact main commit into every affected supported release branch. Keep individual backports independently testable and reference the upstream PR.

> **Past public example:** [PR #45770 - v1.35 backport referencing upstream PR #45766](https://github.com/envoyproxy/envoy/pull/45770)

27. **Build and test a rollup stack.** For security work, also maintain a combined stack/rollup per branch so conflicts and the full patch set are tested together before PRs are raised.

> **Past public example:** [PR #45795 - combined v1.35 Q2 security patch stack](https://github.com/envoyproxy/envoy/pull/45795)

28. **Preserve commits with rebase.** Keep upstream fixes as separate commits when they should land separately. If a backport needs adjustments, squash only those adjustments into the corresponding commit and remove branch prefixes from final commit subjects.

> **Past public example:** [PR #45795 - multi-fix rollup reviewed on the release branch](https://github.com/envoyproxy/envoy/pull/45795)

29. **Add the summary to the final stack.** After all backports are present, add `changelogs/summary.md` to each release branch stack so the PR and summary travel through one review cycle.

> **Past public example:** [PR #45803 - v1.35.13 summary after the patch stack](https://github.com/envoyproxy/envoy/pull/45803)

### 5.2 Distributor patch/tarball handoff

For embargoed security releases, follow the public distributor disclosure process in [SECURITY.md](https://github.com/envoyproxy/envoy/blob/main/SECURITY.md#fix-disclosure-process) and its [embargo policy](https://github.com/envoyproxy/envoy/blob/main/SECURITY.md#embargo-policy).

- Send only after the security team approves the final patch set and embargo date.

- List every covered branch, expected public release version, and the exact release/advisory timing in one timezone plus UTC.

- Allow the agreed distributor soak window defined in [SECURITY.md](https://github.com/envoyproxy/envoy/blob/main/SECURITY.md#fix-development-process).

- If a vulnerability becomes public, send a separate zero-day notice and keep unrelated embargoed issues confidential.

> **Past public example:** [PR #45795 - public post-disclosure Q2 patch rollup](https://github.com/envoyproxy/envoy/pull/45795). *The embargoed distributor handoff must not be represented by a public PR; this PR is only the later public code example.*

### 5.3 Cut the public patch releases

30. **Open WIP release PRs.** Run `task=create-release` with `wip=true` on each `release/vX.Y` branch after the backport stack and summary exist. Use WIP PRs to expose generated diffs and CI problems early.

> **Past public example:** [PR #45744 - WIP generated v1.35.13 release PR](https://github.com/envoyproxy/envoy/pull/45744)

31. **Resolve all branch-specific failures.** Fix formatting, changelog line length, missing placeholder directories, dependency downloads, and stale bot branches before final release PRs.

> **Past public example:** [PR #45766 - main dependency workaround](https://github.com/envoyproxy/envoy/pull/45766); [PR #45770 - matching v1.35 backport](https://github.com/envoyproxy/envoy/pull/45770)

32. **Regenerate final release PRs.** Run create-release again with `wip=false` from the latest branch tips. Close superseded WIP PRs so reviewers cannot merge stale output.

> **Past public example:** [PR #45805 - final v1.35.13 release PR superseding WIP #45744](https://github.com/envoyproxy/envoy/pull/45805)

33. **Release oldest supported branch first.** Observed June waves consistently released v1.35, then v1.36, v1.37, and v1.38. Use this order unless the Fix Lead records a reason to differ.

> **Past public example:** [PR #45805 - v1.35.13](https://github.com/envoyproxy/envoy/pull/45805); [PR #45811 - v1.36.9](https://github.com/envoyproxy/envoy/pull/45811); [PR #45814 - v1.37.5](https://github.com/envoyproxy/envoy/pull/45814); [PR #45818 - v1.38.3](https://github.com/envoyproxy/envoy/pull/45818)

34. **For each branch: wait, merge, verify.** Wait for CI; rebase-merge the generated release PR; wait for the tag, assets, images, docs, and verification to finish; only then advance to the next branch.

> **Past public example:** [PR #45805 - first branch](https://github.com/envoyproxy/envoy/pull/45805); [PR #45818 - final branch](https://github.com/envoyproxy/envoy/pull/45818). *Publishing and verification complete in Actions after each PR merges.*

35. **Publish public communications.** After the release set is available, send the user/distributor completion notice with exact versions, release links, advisories, mitigations, and any zero-day additions or exclusions.

> **Past public example:** [PR #45818 - final PR in the v1.35.13-v1.38.3 release wave](https://github.com/envoyproxy/envoy/pull/45818). *Communications themselves are not repository PRs.*

### 5.4 Reopen and synchronize after the full release set

36. **Remove stale workflow branches if necessary.** Historical reopen runs failed when `release/dev/*` branches already existed. Delete only the specific stale bot branches after confirming their PRs are closed or merged.

> **Past public example:** [PR #46140 - dev PR containing the placeholder follow-up](https://github.com/envoyproxy/envoy/pull/46140). *Deleting a confirmed stale bot branch does not create a PR.*

37. **Run reopen-branch for every released branch.** This creates a dev-mode PR, increments the patch development version, and reorganizes changelogs.

> **Past public example:** [PR #45825 - reopen v1.35 as v1.35.14-dev](https://github.com/envoyproxy/envoy/pull/45825); [PR #45826 - reopen v1.38 as v1.38.4-dev](https://github.com/envoyproxy/envoy/pull/45826)

38. **Merge every dev-mode PR.** Confirm the new -dev version and current changelog directory. Reopen failures caused by missing placeholders must be fixed before normal development resumes.

> **Past public example:** [PR #45825 - merged v1.35 dev-mode PR](https://github.com/envoyproxy/envoy/pull/45825)

39. **Run sync-version-histories on main.** Review and merge the generated sync PR after all release and dev-mode PRs land.

> **Past public example:** [PR #45876 - generated version-history synchronization](https://github.com/envoyproxy/envoy/pull/45876)

40. **Clean release metadata.** Verify human signoffs, remove any unwanted generated first line from the release body if present, and mark the newest supported release as Latest.

> **Past public example:** [PR #46121 - public source from which the v1.39.0 release body was restored](https://github.com/envoyproxy/envoy/pull/46121). *Editing release metadata and the Latest marker does not create a PR.*

## 6. Zero-day emergency modifications

> [!WARNING]
> **Decision trigger:** A vulnerability that becomes public before the planned embargo end is normally treated as a zero-day decision. Convene the security team immediately and record whether to cut an emergency release.

> **Past public example:** [PR #45416 - first final PR in the June emergency release wave](https://github.com/envoyproxy/envoy/pull/45416). *The zero-day decision itself belongs to the security team and does not create a public PR.*

41. **Separate public from still-embargoed scope.** Release and communicate the public fix without disclosing unrelated embargoed issues.

> **Past public example:** [PR #45416 - first final PR in the June emergency release wave](https://github.com/envoyproxy/envoy/pull/45416). *Scope separation is a security-team decision, not a public PR.*

42. **State the new ETA.** Notify distributors and users that an emergency release is in progress, with expected versions and a concrete time window. If the window changes, send an update rather than going silent.

> **Past public example:** [PR #45416 - public release PR once the first branch was ready](https://github.com/envoyproxy/envoy/pull/45416). *ETA messages are communications, not PRs.*

43. **Run the minimum safe technical gates.** Main fix reviewed and merged; every affected supported backport reviewed and tested; summaries correct; release PR checks green; publish/verify monitored.

> **Past public example:** [PR #45416 - v1.35.11 final release PR](https://github.com/envoyproxy/envoy/pull/45416); [PR #45445 - v1.38.1 final release PR](https://github.com/envoyproxy/envoy/pull/45445)

44. **Do not bundle by reflex.** Bundle only when the security team decides the fixes are safe and communications remain accurate. An urgent public fix may warrant its own release wave.

> **Past public example:** [PR #45416 - separately cut June emergency release wave](https://github.com/envoyproxy/envoy/pull/45416). *Whether to bundle is an explicit security-team decision.*

45. **Send two completion messages if needed.** One message closes the zero-day incident; a later message closes the scheduled embargoed release. Avoid wording that implies still-embargoed issues are public.

> **Past public example:** [PR #45805 - later scheduled security release PR](https://github.com/envoyproxy/envoy/pull/45805). *Completion messages themselves are not PRs.*

## 7. Verification checklist

| Area | Pass criteria |
| --- | --- |
| GitHub | Correct tag and target SHA; release body contains the complete summary; expected versions exist; release marked Latest where appropriate. |
| Artifacts | Signed archives and binaries exist for required architectures; checksums/signatures are present; no upload warning is unresolved. |
| Containers | Expected Docker Hub/GHCR tags exist for supported images and architectures; base-image updates match the release branch. |
| Documentation | Versioned docs and release notes load; `docs/versions.yaml` and inventories reflect the new version; full-changelog links are correct. |
| CI | Publish and verify jobs completed successfully; failures were investigated, not merely retried until green. |
| Branches | New release branch exists for x.y.0; every released branch has a merged dev-mode PR; main histories are synchronized. |
| Communications | Distributor/public/Slack notices contain the exact published versions and links; embargoed content appears only in approved channels. |

## 8. Failure playbook

| Symptom | Likely cause | Response |
| --- | --- | --- |
| create-release fails immediately | Empty `changelogs/summary.md` or wrong target branch | Stop; populate the summary and confirm branch selection. Do not merge a summary-disabled WIP PR. |
| Generated PR is stale | A backport or changelog fix landed after workflow dispatch | Close/supersede the WIP PR and rerun from the latest branch tip. |
| Release branch CI is red | Dependency/download failure or accumulated flake | Land the fix on main and backport it before generating further release PRs; deflake the branch. |
| Format check fails in generated changelog | RST/YAML line length or conversion defect | Fix the source fragment/summary, rerun formatting, then regenerate the release PR. |
| Release body has only a title | Release PR was squash-merged | Reconstruct the body from the generated PR/commit, edit the GitHub release, add signoffs, and document the incident. Require rebase merge next time. |
| reopen-branch cannot push | Stale `release/dev/*` branch or missing changelog placeholder | Confirm the old PR state, delete only the stale bot branch, apply the placeholder fix, and rerun. |
| Published artifact is missing/wrong | Wrong pipeline artifact or upload failure | Download the artifact from the exact release run, inspect the signed archive, replace/fix the asset, and re-verify. |
| Cannot announce with @here | Slack workspace permission | Ask a workspace owner/CNCF administrator in advance or assign an authorized announcer. |

## 9. Public release timeline: June–July 2026

The following chronology is based on public GitHub release metadata and generated PRs. It illustrates the WIP → final release PR → publish → reopen/sync pattern. Dates below are UTC.

| Window | Public record |
| --- | --- |
| 3–4 Jun | Final release PRs including [#45416](https://github.com/envoyproxy/envoy/pull/45416) and [#45445](https://github.com/envoyproxy/envoy/pull/45445) merged; [v1.35.11](https://github.com/envoyproxy/envoy/releases/tag/v1.35.11), [v1.36.7](https://github.com/envoyproxy/envoy/releases/tag/v1.36.7), [v1.37.3](https://github.com/envoyproxy/envoy/releases/tag/v1.37.3), and [v1.38.1](https://github.com/envoyproxy/envoy/releases/tag/v1.38.1) were published oldest-to-newest. |
| 10 Jun | [v1.35.12](https://github.com/envoyproxy/envoy/releases/tag/v1.35.12), [v1.36.8](https://github.com/envoyproxy/envoy/releases/tag/v1.36.8), [v1.37.4](https://github.com/envoyproxy/envoy/releases/tag/v1.37.4), and [v1.38.2](https://github.com/envoyproxy/envoy/releases/tag/v1.38.2) were published oldest-to-newest. |
| 22–30 Jun | WIP release PR [#45744](https://github.com/envoyproxy/envoy/pull/45744) opened on 22 Jun; dependency fixes [#45766](https://github.com/envoyproxy/envoy/pull/45766) and [#45770](https://github.com/envoyproxy/envoy/pull/45770) landed before the final release PRs. [v1.35.13](https://github.com/envoyproxy/envoy/releases/tag/v1.35.13), [v1.36.9](https://github.com/envoyproxy/envoy/releases/tag/v1.36.9), [v1.37.5](https://github.com/envoyproxy/envoy/releases/tag/v1.37.5), and [v1.38.3](https://github.com/envoyproxy/envoy/releases/tag/v1.38.3) were published on 23 Jun. History-sync PR [#45876](https://github.com/envoyproxy/envoy/pull/45876) merged on 29 Jun; dev-mode PR [#45825](https://github.com/envoyproxy/envoy/pull/45825) opened on 24 Jun and merged on 30 Jun. |
| 14–15 Jul | WIP release PR [#46117](https://github.com/envoyproxy/envoy/pull/46117) opened; changelog correction [#46118](https://github.com/envoyproxy/envoy/pull/46118) and summary [#46119](https://github.com/envoyproxy/envoy/pull/46119) landed before final release PR [#46121](https://github.com/envoyproxy/envoy/pull/46121). [v1.39.0](https://github.com/envoyproxy/envoy/releases/tag/v1.39.0) was published on 14 Jul; dev-mode PR [#46140](https://github.com/envoyproxy/envoy/pull/46140) merged on 15 Jul, reopening main to v1.40.0-dev. |

## 10. Release-day checklist

- [ ] Scope and supported branches confirmed; owners and reviewers online.
- [ ] All required main fixes and branch blockers landed.
- [ ] All affected supported backports green; rollup stacks tested.
- [ ] `changelogs/summary.md` final on every release target.
- [ ] CVE scanner and Ubuntu/distroless image checks recorded.
- [ ] WIP generated PRs reviewed; final workflow runs use the latest branch tips.
- [ ] Generated release PRs green and rebase-merged, never squash-merged.
- [ ] Each tag, release body, signed artifact, image, docs page, and verify run checked before advancing.
- [ ] Announcements contain final versions and links; embargo boundaries rechecked.
- [ ] reopen-branch and sync-version-histories PRs created and tracked.
- [ ] Release metadata/signoffs cleaned; Latest marker checked; follow-up issues filed.
