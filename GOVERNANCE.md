# Process for becoming a maintainer

Becoming a maintainer generally means that you are going to be spending substantial time on
Envoy for the foreseeable future. You should have domain expertise and be extremely proficient in C++.

* Express interest to the
  [envoy-maintainers](https://groups.google.com/forum/#!forum/envoy-announce)
  that you are interested in becoming a maintainer and, if your company does not have pre-existing maintainers,
  that your organization is interested in and willing to sponsoring a maintainer.
* We will expect you to start contributing increasingly complicated PRs, under the guidance
  of the existing maintainers.
* We may ask you to fix some issues from our backlog.
* As you gain experience with the code base and our standards, we will ask you to do code reviews
  for incoming PRs (i.e., all maintainers are expected to shoulder a proportional share of
  community reviews).
* After a period of approximately 2-3 months of contributions demonstrating understanding of (at least parts of)
  the Envoy code base, reach back out to the maintainers list asking for feedback.  At this point, you will either
  be granted maintainer status, or be given actionable feedback on any remaining gaps between the contributions
  demonstrated and those expected of maintainers, at which point you can close those gaps and reach back out.

## Maintainer responsibilities

* Monitor email aliases.
* Monitor Slack (delayed response is perfectly acceptable).
* Triage GitHub issues and perform pull request reviews for other maintainers and the community.
  The areas of specialization listed in [OWNERS.md](OWNERS.md) can be used to help with routing
  an issue/question to the right person.
* Triage build and CI issues. Monitor #envoy-ci and file issues for flaking or failing builds,
  or new bugs, and either fix or find someone to fix any main build breakages.
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

The API shepherds are distinct to the [xDS working
group](https://github.com/cncf/xds/blob/main/README.md), which aims to evolve xDS directionally
towards a universal dataplane API. API shepherds are responsible for the execution of the xDS
day-to-day and guiding xDS implementation changes. Proposals from xDS-WG will be aligned with the
xDS API shepherds to ensure that xDS is heading towards the xDS goal of client and server neutral
xDS. xDS API shepherds operate under the [envoyproxy](https://github.com/envoyproxy) organization
but are expected to keep in mind the needs of all xDS clients (currently Envoy and gRPC, but we are
aware of other in-house implementations) and the goals of xDS-WG.

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
