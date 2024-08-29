# Script for collecting PRs in need of review, and informing maintainers via
# slack.
#
# The tool can be used in `--dry_run` mode or `--report` mode. In the latter
# case it will just dump a report, and in the former it will show what it would
# post to slack
#
# NOTE: Slack IDs can be found in the user's full profile from within Slack.

import datetime
import html
import icalendar
import json
import os
import pathlib
import sys
from datetime import datetime as dt
from functools import cached_property

import aiohttp
import yaml

from slack_sdk.web.async_client import AsyncWebClient
from slack_sdk.errors import SlackApiError

from aio.api import github as github
from aio.core.functional import async_property
from aio.run import runner

ENVOY_REPO = "envoyproxy/envoy"

# Oncall calendar
CALENDAR = "https://calendar.google.com/calendar/ical/d6glc0l5rc3v235q9l2j29dgovh3dn48%40import.calendar.google.com/public/basic.ics"

ISSUE_LINK = "https://github.com/envoyproxy/envoy/issues?q=is%3Aissue+is%3Aopen+label%3Atriage"
SLACK_EXPORT_URL = "https://api.slack.com/apps/A023NPQQ33K/oauth?"
CI_TRIAGE_LINK = "https://docs.google.com/document/d/1alWWsy6o_IR9iLAyIxtrrSK7Yoy_er3zHU1w98dPdII/edit#heading=h.e8mas09g7s19"


class RepoNotifier(runner.Runner):

    @cached_property
    def api_reviewers(self):
        """Only notify API reviewers who aren't maintainers.
        Maintainers are already notified of pending PRs."""
        return {k: v["slack"] for k, v in self.reviewers.items() if v.get("api")}

    @property
    def dry_run(self):
        return self.args.dry_run

    @cached_property
    def first_pass_reviewers(self):
        """First pass reviewers who are not maintainers should get
        notifications but not result in a PR not getting assigned a
        maintainer owner."""
        return {k: v["slack"] for k, v in self.reviewers.items() if v.get("first-pass")}

    @cached_property
    def github(self):
        return github.GithubAPI(self.session, "", oauth_token=self.github_token)

    @cached_property
    def github_token(self):
        return os.getenv('GITHUB_TOKEN')

    @async_property(cache=True)
    async def maintainer_notifications(self):
        return (await self.tracked_prs)["maintainer_notifications"]

    @cached_property
    def maintainers(self):
        return {k: v["slack"] for k, v in self.reviewers.items() if v.get("maintainer")}

    @async_property
    async def pulls(self):
        async for pull in self.repo.getiter("pulls"):
            skip = (
                pull["draft"] or pull["user"]["login"] == "dependabot[bot]"
                or self.is_waiting(pull))
            if skip:
                self.log.notice(f"Skipping {pull['title']} {pull['url']}")
                continue
            yield pull

    @cached_property
    def repo(self):
        return self.github[ENVOY_REPO]

    @cached_property
    def reviewers(self):
        return yaml.safe_load(pathlib.Path(self.args.reviewers).read_text())

    @cached_property
    def session(self):
        return aiohttp.ClientSession()

    @async_property(cache=True)
    async def shepherd_notifications(self):
        return (await self.tracked_prs)["shepherd_notifications"]

    @property
    def should_report(self):
        return self.args.report

    @cached_property
    def slack_client(self):
        return AsyncWebClient(token=self.slack_bot_token)

    @cached_property
    def slack_bot_token(self):
        return os.getenv('SLACK_BOT_TOKEN')

    @cached_property
    def slo_max(self):
        """on Monday, allow for 24h + 48h."""
        hours = (72 if datetime.date.today().weekday() == 0 else 24)
        return datetime.timedelta(hours=hours)

    @async_property(cache=True)
    async def stalled_prs(self):
        return (await self.tracked_prs)["stalled_prs"]

    @async_property(cache=True)
    async def oncall_string(self):
        now = datetime.datetime.now()
        sunday = now - datetime.timedelta(days=now.weekday() + 1)
        monday = now - datetime.timedelta(days=now.weekday())
        priorweek = now - datetime.timedelta(14)

        # Handle the event being created before today.
        date = priorweek.strftime("%Y%m%d")
        response = await self.session.get(f"{CALENDAR}?getdate={date}")
        content = await response.read()
        parsed_calendar = icalendar.Calendar.from_ical(content)

        for component in parsed_calendar.walk():
            if component.name == "VEVENT":
                if (sunday.date() == component.decoded("dtstart").date()):
                    return component.get("summary")
                if (monday.date() == component.decoded("dtstart").date()):
                    return component.get("summary")
        print("unable to find this week's oncall")
        return "unable to find this week's oncall"

    @async_property
    async def oncall_slack_handle(self):
        opsgenie_string = await self.oncall_string
        # Snag the first name from the "oncall transitioning to" entry.
        opsgenie_name = opsgenie_string.split(' ', 1)[0]
        # Check that the name is in the OPSGENIE_TO_SLACK list, else cc alyssa.
        if not (uid := self.opsgenie_to_slack.get(opsgenie_name)):
            print("could not find", opsgenie_name)
            return self.opsgenie_to_slack.get('Alyssa')
        return uid

    @cached_property
    def opsgenie_to_slack(self):
        return {v["opsgenie"]: v["slack"] for v in self.reviewers.values() if "opsgenie" in v}

    @async_property(cache=True)
    async def tracked_prs(self):
        # A dict of maintainer : outstanding_pr_string to be sent to slack
        # A placeholder for unassigned PRs, to be sent to #maintainers eventually
        maintainers_and_prs = dict(unassigned=[])
        # A dict of shepherd : outstanding_pr_string to be sent to slack
        api_review = {}
        # Out-SLO PRs to be sent to #envoy-maintainer-oncall
        stalled_prs = []

        # TODO: pre-filter these
        async for pull in self.pulls:
            updated_at = dt.fromisoformat(pull["updated_at"].replace('Z', '+00:00'))
            age = dt.now(datetime.timezone.utc) - dt.fromisoformat(
                pull["updated_at"].replace('Z', '+00:00'))
            message = self.pr_message(age, pull)

            if await self.needs_api_review(pull):
                for assignee in self.get_assignees(pull, self.api_reviewers):
                    api_review[assignee["login"]] = api_review.get(
                        assignee["login"],
                        [f"Hello, {assignee['login']}, here are your PR reminders for the day"])
                    api_review[assignee["login"]].append(message)

            # If the PR has been out-SLO for over a day, inform on-call
            if age > self.slo_max + datetime.timedelta(hours=36):
                stalled_prs.append(message)

            has_maintainer = False
            assignees = self.get_assignees(pull, {**self.maintainers, **self.first_pass_reviewers})
            for assignee in assignees:
                if self.maintainers.get(assignee["login"]):
                    has_maintainer = True
                maintainers_and_prs[assignee["login"]] = maintainers_and_prs.get(
                    assignee["login"], [])
                maintainers_and_prs[assignee["login"]].append(message)

            # If there was no maintainer, track it as unassigned.
            if not has_maintainer and not self.is_contrib(pull):
                maintainers_and_prs['unassigned'].append(message)

        return dict(
            maintainer_notifications=maintainers_and_prs,
            shepherd_notifications=api_review,
            stalled_prs=stalled_prs)

    @async_property(cache=True)
    async def unassigned_prs(self):
        return (await self.maintainer_notifications)["unassigned"]

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument('reviewers', help="YAML reviewer config")
        parser.add_argument(
            '--dry_run',
            action="store_true",
            help="Dont post slack messages, just show what would be posted")
        parser.add_argument('--report', action="store_true", help="Print a report of current state")

    def get_assignees(self, pull, assignees):
        for assignee in pull["assignees"]:
            if assignee["login"] in assignees:
                yield assignee

    def is_contrib(self, pr):
        for label in pr["labels"]:
            if label["name"] == "contrib":
                return True
        return False

    def is_waiting(self, pr):
        for label in pr["labels"]:
            if label["name"].startswith("waiting"):
                return True
        return False

    async def needs_api_review(self, pull):
        """Returns true if the PR needs an LGTM from an API shepherd."""
        if "api" not in [label["name"] for label in pull["labels"]]:
            return False
        # repokitten tags each commit as pending unless there has been an API LGTM
        # since the latest API changes. If this PR is tagged pendding it needs an
        # API review, otherwise it's set.
        status = (await self.repo.getitem(f"commits/{pull['head']['sha']}/status"))
        return status["state"] == "pending" if status["total_count"] else False

    async def notify(self):
        await self.post_to_oncall()
        await self.post_to_assignees()

    async def post_to_assignees(self):
        review_notifications = ((self.api_reviewers, await self.shepherd_notifications),
                                (self.maintainers, await self.maintainer_notifications),
                                (self.first_pass_reviewers, await self.maintainer_notifications))
        for assignees, messages in review_notifications:
            await self._post_to_assignees(assignees, messages)

    async def post_to_oncall(self):
        try:
            unassigned = "\n".join(await self.unassigned_prs)
            stalled = "\n".join(await self.stalled_prs)
            oncall_handle = await self.oncall_slack_handle
            # On Monday, post the new oncall.
            if datetime.date.today().weekday() == 0:
                oncall = await self.oncall_string
                await self.send_message(channel='#envoy-maintainer-oncall', text=(f"{oncall}"))
                await self.send_message(channel='#general', text=(f"{oncall}"))
            await self.send_message(
                channel='#envoy-maintainer-oncall', text=(f"Oncall now <@{oncall_handle}>"))
            await self.send_message(
                channel='#envoy-maintainer-oncall',
                text=(f"*'Unassigned' PRs* (PRs with no maintainer assigned)\n{unassigned}"))
            await self.send_message(
                channel='#envoy-maintainer-oncall',
                text=(f"*Stalled PRs* (PRs with review out-SLO, please address)\n{stalled}"))
            num_issues = await self.track_open_issues()
            await self.send_message(
                channel='#envoy-maintainer-oncall',
                text=(
                    f"*{num_issues} Untriaged Issues* (please tag and cc area experts)\n<{ISSUE_LINK}|{ISSUE_LINK}>"
                ))
            await self.send_message(
                channel='#envoy-ci',
                text=(
                    f"<@{oncall_handle}> please triage flakes per <{CI_TRIAGE_LINK} | instructions>"
                ))
        except SlackApiError as e:
            self.log.error(f"Unexpected error {e.response['error']}")

    def pr_message(self, age, pull):
        """Generate a pr message, bolding the time if it's out-SLO."""
        days = age.days
        hours = age.seconds // 3600
        markup = ("*" if age > self.slo_max else "")
        return (
            f"<{pull['html_url']}|{html.escape(pull['title'])}> has been waiting "
            f"{markup}{days} days {hours} hours{markup}")

    async def track_open_issues(self):
        response = await self.session.get(
            "https://api.github.com/repos/envoyproxy/envoy/issues?labels=triage")
        return len(await response.json())

    async def run(self):
        if not self.github_token:
            self.log.error("Missing GITHUB_TOKEN: please check github workflow configuration")
            return 1

        if not self.slack_bot_token and not self.dry_run and not self.report:
            self.log.error(
                "Missing SLACK_BOT_TOKEN: please export token from "
                f"{SLACK_EXPORT_URL}")
            return 1
        return await (self.report() if self.should_report else self.notify())

    async def report(self):
        report = dict(maintainers={}, shepherds={}, stalled=[])
        for maintainer, messages in (await self.maintainer_notifications).items():
            report["maintainers"][maintainer] = messages

        for shepherd, messages in (await self.shepherd_notifications).items():
            report["shepherds"][shepherd] = messages

        if stalled_pr_info := await self.stalled_prs:
            report["stalled"].append(stalled_pr_info)

        print(json.dumps(report))

    async def send_message(self, channel, text):
        self.log.notice(f"Slack message ({channel}):\n{text}")
        if self.dry_run:
            return
        await self.slack_client.chat_postMessage(channel=channel, text=text)

    async def _post_to_assignees(self, assignees, messages):
        for name, text in messages.items():
            # Only send texts if we have the slack UID
            if not (uid := assignees.get(name)):
                continue
            message = "\n".join(text)
            self.log.notice(f"Slack message ({name}):\n{message}")
            if self.dry_run:
                continue
            # Ship texts off to slack.
            try:
                response = await self.slack_client.conversations_open(users=uid, text="hello")
                channel_id = response["channel"]["id"]
                await self.slack_client.chat_postMessage(channel=channel_id, text=message)
            except SlackApiError as e:
                print(f"Unexpected error {e.response['error']}")


def main(*args):
    return RepoNotifier(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
