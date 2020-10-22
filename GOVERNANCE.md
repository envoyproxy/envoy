# Process for becoming a maintainer

## Your organization is not yet a maintainer

* Express interest to the senior maintainers that your organization is interested in becoming a
  maintainer. Becoming a maintainer generally means that you are going to be spending substantial
  time (>25%) on Envoy for the foreseeable future. You should have domain expertise and be extremely
  proficient in C++. Ultimately your goal is to become a senior maintainer that will represent your
  organization.
* We will expect you to start contributing increasingly complicated PRs, under the guidance
  of the existing senior maintainers.
* We may ask you to do some PRs from our backlog.
* As you gain experience with the code base and our standards, we will ask you to do code reviews
  for incoming PRs (i.e., all maintainers are expected to shoulder a proportional share of
  community reviews).
* After a period of approximately 2-3 months of working together and making sure we see eye to eye,
  the existing senior maintainers will confer and decide whether to grant maintainer status or not.
  We make no guarantees on the length of time this will take, but 2-3 months is the approximate
  goal.

## Your organization is currently a maintainer

* First decide whether your organization really needs more people with maintainer access. Valid
  reasons are "blast radius", a large organization that is working on multiple unrelated projects,
  etc.
* Contact a senior maintainer for your organization and express interest.
* Start doing PRs and code reviews under the guidance of your senior maintainer.
* After a period of 1-2 months the existing senior maintainers will discuss granting "standard"
  maintainer access.
* "Standard" maintainer access can be upgraded to "senior" maintainer access after another 1-2
  months of work and another conference of the existing senior committers.

## Maintainer responsibilities

* Monitor email aliases.
* Monitor Slack (delayed response is perfectly acceptable).
* Triage GitHub issues and perform pull request reviews for other maintainers and the community.
  The areas of specialization listed in [OWNERS.md](OWNERS.md) can be used to help with routing
  an issue/question to the right person.
* Triage build issues - file issues for known flaky builds or bugs, and either fix or find someone
  to fix any master build breakages.
* During GitHub issue triage, apply all applicable [labels](https://github.com/envoyproxy/envoy/labels)
  to each new issue. Labels are extremely useful for future issue follow up. Which labels to apply
  is somewhat subjective so just use your best judgment. A few of the most important labels that are
  not self explanatory are:
  * **beginner**: Mark any issue that can reasonably be accomplished by a new contributor with
    this label.
  * **help wanted**: Unless it is immediately obvious that someone is going to work on an issue (and
    if so assign it), mark it help wanted.
  * **question**: If it's unclear if an issue is immediately actionable, mark it with the
    question label. Questions are easy to search for and close out at a later time. Questions
    can be promoted to other issue types once it's clear they are actionable (at which point the
    question label should be removed).
* Make sure that ongoing PRs are moving forward at the right pace or closing them.
* Participate when called upon in the [security release process](SECURITY.md). Note that although
  this should be a rare occurrence, if a serious vulnerability is found, the process may take up to
  several full days of work to implement. This reality should be taken into account when discussing
  time commitment obligations with employers.
* In general continue to be willing to spend at least 25% of ones time working on Envoy (~1.25
  business days per week).
* We currently maintain an "on-call" rotation within the maintainers. Each on-call is 1 week.
  Although all maintainers are welcome to perform all of the above tasks, it is the on-call
  maintainer's responsibility to triage incoming issues/questions and marshal ongoing work
  forward. To reiterate, it is *not* the responsibility of the on-call maintainer to answer all
  questions and do all reviews, but it is their responsibility to make sure that everything is
  being actively covered by someone.
* The on-call rotation is tracked at Opsgenie. The calendar is visible
[here](https://calendar.google.com/calendar/embed?src=d6glc0l5rc3v235q9l2j29dgovh3dn48%40import.calendar.google.com&ctz=America%2FNew_York)
or you can subscribe to the iCal feed [here](webcal://kubernetes.app.opsgenie.com/webapi/webcal/getRecentSchedule?webcalToken=39dd1a892faa8d0d689f889b9d09ae787355ddff894396546726a5a02bac5b26&scheduleId=a3505963-c064-4c97-8865-947dfcb06060)

## Cutting a release

* We do releases every 3 months, at the end of each quarter, as described in the
  [release schedule](RELEASES.md#release-schedule).
* Take a look at open issues tagged with the current release, by
  [searching](https://github.com/envoyproxy/envoy/issues) for
  "is:open is:issue milestone:[current milestone]" and either hold off until
  they are fixed or bump them to the next milestone.
* Begin marshalling the ongoing PR flow in this repo. Ask maintainers to hold off merging any
  particularly risky PRs until after the release is tagged. This is because we aim for master to be
  at release candidate quality at all times.
* Do a final check of the [release notes](docs/root/version_history/current.rst):
  * Make any needed corrections (grammar, punctuation, formatting, etc.).
  * Check to see if any security/stable version release notes are duplicated in
    the major version release notes. These should not be duplicated.
  * If the "Deprecated" section is empty, delete it.
  * Remove the "Pending" tags and add dates to the top of the [release notes for this version](docs/root/version_history/current.rst).
  * Switch the [VERSION](VERSION) from a "dev" variant to a final variant. E.g., "1.6.0-dev" to
    "1.6.0".
  * Update the [RELEASES](RELEASES.md) doc with the relevant dates. Now, or after you cut the
    release, please also make sure there's a stable maintainer signed up for next quarter,
    and the deadline for the next release is documented in the release schedule.
  * Get a review and merge.
* Wait for tests to pass on [master](https://dev.azure.com/cncf/envoy/_build).
* Create a [tagged release](https://github.com/envoyproxy/envoy/releases). The release should
  start with "v" and be followed by the version number. E.g., "v1.6.0". **This must match the
  [VERSION](VERSION).**
* From the envoy [landing page](https://github.com/envoyproxy/envoy) use the branch drop-down to create a branch
  from the tagged release, e.g. "release/v1.6". It will be used for the
  [stable releases](RELEASES.md#stable-releases).
* Monitor the AZP tag build to make sure that the final docker images get pushed along with
  the final docs. The final documentation will end up in the
  [envoyproxy.github.io repository](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy).
* Update the website ([example PR](https://github.com/envoyproxy/envoyproxy.github.io/pull/148)) for the new release.
* Craft a witty/uplifting email and send it to all the email aliases including envoy-announce@.
* Make sure we tweet the new release: either have Matt do it or email social@cncf.io and ask them to do an Envoy account
  post.
* Do a new PR to setup the next version
  * Update [VERSION](VERSION) to the next development release. E.g., "1.7.0-dev". 
  * `git mv docs/root/version_history/current.rst docs/root/version_history/v1.6.0.rst`, filling in the previous
    release version number in the filename, and add an entry for the new file in the `toctree` in 
    [version_history.rst](docs/root/version_history/version_history.rst).
  * Create a new "current" version history file at the [release
  notes](docs/root/version_history/current.rst) for the following version. E.g., "1.7.0 (pending)". Use
  this text as the template for the new file:
```
1.7.0 (Pending)
===============

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
```
* Run the deprecate_versions.py script (e.g. `sh tools/deprecate_version/deprecate_version.sh`)
  to file tracking issues for runtime guarded code which can be removed.
* Check source/common/runtime/runtime_features.cc and see if any runtime guards in
  disabled_runtime_features should be reassessed, and ping on the relevant issues.

## When does a maintainer lose maintainer status

If a maintainer is no longer interested or cannot perform the maintainer duties listed above, they
should volunteer to be moved to emeritus status. In extreme cases this can also occur by a vote of
the maintainers per the voting process below.

# xDS API shepherds

The [xDS API shepherds](https://github.com/orgs/envoyproxy/teams/api-shepherds) are responsible for
approving any PR that modifies the [api/](api/) tree. They ensure that API [style](api/STYLE.md) and
[versioning](api/API_VERSIONING.md) policies are enforced and that a consistent approach is taken
towards API evolution.

The xDS API shepherds are also the xDS API maintainers; they work collaboratively with the community
to drive the xDS API roadmap and review major proposed design changes. The API shepherds are
intended to be representative of xDS client and control plane developers who are actively working on
xDS development and evolution.

As with maintainers, an API shepherd should be spending at least 25% of their time working on xDS
developments and expect to be active in this space in the near future. API shepherds are expected to
take on API shepherd review load and participate in meetings. They should be active on Slack `#xds`
and responsive to GitHub issues and PRs on which they are tagged.

The API shepherds are distinct to the [UDPA working
group](https://github.com/cncf/udpa/blob/master/README.md), which aims to evolve xDS directionally
towards a universal dataplane API. API shepherds are responsible for the execution of the xDS
day-to-day and guiding xDS implementation changes. Proposals from UDPA-WG will be aligned with the
xDS API shepherds to ensure that xDS is heading towards the UDPA goal. xDS API shepherds operate
under the [envoyproxy](https://github.com/envoyproxy) organization but are expected to keep in mind
the needs of all xDS clients (currently Envoy and gRPC, but we are aware of other in-house
implementations) and the goals of UDPA-WG.

If you wish to become an API shepherd and satisfy the above criteria, please contact an existing
API shepherd. We will factor in PR and review history to determine if the above API shepherd
requirements are met. We may ask you to shadow an existing API shepherd for a period of time to
build confidence in consistent application of the API guidelines to PRs.

# Extension addition policy

Adding new [extensions](REPO_LAYOUT.md#sourceextensions-layout) has a dedicated policy. Please
see [this](./EXTENSION_POLICY.md) document for more information.

# External dependency policy

Adding new external dependencies has a dedicated policy. Please see [this](DEPENDENCY_POLICY.md)
document for more information.

# Conflict resolution and voting

In general, we prefer that technical issues and maintainer membership are amicably worked out
between the persons involved. If a dispute cannot be decided independently, the maintainers can be
called in to decide an issue. If the maintainers themselves cannot decide an issue, the issue will
be resolved by voting. The voting process is a simple majority in which each senior maintainer
receives two votes and each normal maintainer receives one vote.

# Adding new projects to the envoyproxy GitHub organization

New projects will be added to the envoyproxy organization via GitHub issue discussion in one of the
existing projects in the organization. Once sufficient discussion has taken place (~3-5 business
days but depending on the volume of conversation), the maintainers of *the project where the issue
was opened* (since different projects in the organization may have different maintainers) will
decide whether the new project should be added. See the section above on voting if the maintainers
cannot easily decide.
