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
* Participate when called upon in the [security release process](SECURITY_RELEASE_PROCESS.md). Note
  that although this should be a rare occurrence, if a serious vulnerability is found, the process
  may take up to several full days of work to implement. This reality should be taken into account
  when discussing time commitment obligations with employers.
* In general continue to be willing to spend at least 25% of ones time working on Envoy (~1.25
  business days per week).
* We currently maintain an "on-call" rotation within the maintainers. Each on-call is 1 week.
  Although all maintainers are welcome to perform all of the above tasks, it is the on-call
  maintainer's responsibility to triage incoming issues/questions and marshal ongoing work
  forward. To reiterate, it is *not* the responsibility of the on-call maintainer to answer all
  questions and do all reviews, but it is their responsibility to make sure that everything is
  being actively covered by someone.
* The on-call rotation is tracked at PagerDuty. The calendar is visible [here](https://pagerduty.github.io/addons/PDcal/index.html?iCalURL=https://cncf.pagerduty.com/private/e44caf2604ce6c5ccc616b7b84f99b94dc801dba4cceb8d71fb128338f75b9af/feed/PXU9KPH) or you can subscribe to the iCal feed [here](https://cncf.pagerduty.com/private/e44caf2604ce6c5ccc616b7b84f99b94dc801dba4cceb8d71fb128338f75b9af/feed/PXU9KPH).

## When does a maintainer lose maintainer status

If a maintainer is no longer interested or cannot perform the maintainer duties listed above, they
should volunteer to be moved to emeritus status. In extreme cases this can also occur by a vote of
the maintainers per the voting process below.

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
