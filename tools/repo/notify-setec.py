# Script for keeping track of setec issues.
#
# bazel run //tools/repo:notify-setec
#
# The tool can be used in `--dry_run` mode and show what it would post to slack

import datetime
import html
import icalendar
import json
import os
import sys
from datetime import datetime as dt
from functools import cached_property

import aiohttp

from slack_sdk.web.async_client import AsyncWebClient
from slack_sdk.errors import SlackApiError

from aio.api import github as github
from aio.core.functional import async_property
from aio.run import runner

ENVOY_REPO = "envoyproxy/envoy-setec"

SLACK_EXPORT_URL = "https://api.slack.com/apps/A023NPQQ33K/oauth?"

class RepoNotifier(runner.Runner):

    @property
    def dry_run(self):
        return self.args.dry_run

    @cached_property
    def github(self):
        return github.GithubAPI(self.session, "", oauth_token=self.github_token)

    @cached_property
    def github_token(self):
        return os.getenv('GITHUB_TOKEN')

    @async_property
    async def issues(self):
        async for issue in self.repo.getiter("issues"):
            skip = not "issue" in issue["html_url"]
            if skip:
                self.log.notice(f"Skipping {issue['title']} {issue['url']}")
                continue
            yield issue

    @cached_property
    def repo(self):
        return self.github[ENVOY_REPO]

    @cached_property
    def session(self):
        return aiohttp.ClientSession()

    @cached_property
    def slack_client(self):
        return AsyncWebClient(token=self.slack_bot_token)

    @cached_property
    def slack_bot_token(self):
        return os.getenv('SLACK_BOT_TOKEN')

    @async_property(cache=True)
    async def assignee_and_issues(self):
        return (await self.tracked_issues)["assignee_and_issues"]

    # Allow for 1w for updates.
    # This can be tightened for cve issues near release time.
    @cached_property
    def slo_max(self):
        hours = 168
        return datetime.timedelta(hours=hours)

    @async_property(cache=True)
    async def stalled_issues(self):
        return (await self.tracked_issues)["stalled_issues"]

    @async_property(cache=True)
    async def tracked_issues(self):
        # A dict of assignee : outstanding_issue to be sent to slack
        # A placeholder for unassigned issuess, to be sent to #assignee eventually
        assignee_and_issues = dict(unassigned=[])
        # Out-SLO issues to be sent to #envoy-setec
        stalled_issues = []

        async for issue in self.issues:
            updated_at = dt.fromisoformat(issue["updated_at"].replace('Z', '+00:00'))
            age = dt.now(datetime.timezone.utc) - dt.fromisoformat(
                issue["updated_at"].replace('Z', '+00:00'))
            message = self.pr_message(age, issue)
            is_approved = "patch:approved" in [label["name"] for label in issue["labels"]];

            # If the PR has been out-SLO for over a day, inform on-call
            if age > self.slo_max + datetime.timedelta(hours=36) and not is_approved:
                stalled_issues.append(message)

            has_assignee = False
            for assignee in issue["assignees"]:
                has_assignee = True
                assignee_and_issues[assignee["login"]] = assignee_and_issues.get(
                    assignee["login"], [])
                assignee_and_issues[assignee["login"]].append(message)

            # If there was no assignee, track it as unassigned.
            if not has_assignee:
                assignee_and_issues['unassigned'].append(message)

        return dict(
            assignee_and_issues=assignee_and_issues,
            stalled_issues=stalled_issues)

    @async_property(cache=True)
    async def unassigned_issues(self):
        return (await self.assignee_and_issues)["unassigned"]

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            '--dry_run',
            action="store_true",
            help="Dont post slack messages, just show what would be posted")

    async def notify(self):
        await self.post_to_oncall()

    async def post_to_oncall(self):
        try:
            unassigned = "\n".join(await self.unassigned_issues)
            stalled = "\n".join(await self.stalled_issues)
            await self.send_message(
                channel='#envoy-security-team',
                text=(f"*'Unassigned' Issues* (Issues with no maintainer assigned)\n{unassigned}"))
            await self.send_message(
                channel='#envoy-security-team',
                text=(f"*Stalled Issues* (Issues with review out-SLO, please address)\n{stalled}"))
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

    async def run(self):
        if not self.github_token:
            self.log.error("Missing GITHUB_TOKEN: please check github workflow configuration")
            return 1

        if not self.slack_bot_token and not self.dry_run:
            self.log.error(
                "Missing SLACK_BOT_TOKEN: please export token from "
                f"{SLACK_EXPORT_URL}")
            return 1
        return await (self.notify())

    async def send_message(self, channel, text):
        self.log.notice(f"Slack message ({channel}):\n{text}")
        if self.dry_run:
            return
        await self.slack_client.chat_postMessage(channel=channel, text=text)

def main(*args):
    return RepoNotifier(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
