# Script for keeping track of setec issues.
#
# bazel run //tools/repo:notify-setec [-- --dry-run]
#
# The tool can be used in `--dry_run` mode and show what it would post to slack

import datetime
import html
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

ENVOY_REPO = "envoyproxy/envoy-setec"

SLACK_EXPORT_URL = "https://api.slack.com/apps/A023NPQQ33K/oauth?"


class RepoNotifier(runner.Runner):

    @property
    def dry_run(self):
        return self.args.dry_run

    @cached_property
    def github(self):
        return github.GithubAPI(
            self.session,
            "",
            oauth_token=self.github_token)

    @cached_property
    def github_token(self):
        return os.getenv('GITHUB_TOKEN')

    @async_property
    async def issues(self):
        async for issue in self.repo.getiter("issues"):
            skip = "issue" not in issue["html_url"]
            if skip:
                continue
            yield issue

    @cached_property
    def maintainers(self):
        return {
            k: v["slack"]
            for k, v
            in self.reviewers.items()
            if v.get("maintainer")}

    @async_property
    async def pulls(self):
        async for pull in self.repo.getiter("pulls"):
            skip = "pull" not in pull["html_url"]
            if skip:
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
    def weekend_offset(self, time):
        """on Monday, allow for an extra 48h."""
        hours = time + (48 if datetime.date.today().weekday() == 0 else 0)
        return datetime.timedelta(hours=hours)

    @async_property(cache=True)
    async def stalled_issues(self):
        return (await self.tracked_issues)["stalled_issues"]

    @async_property(cache=True)
    async def stalled_cve_issues(self):
        return (await self.tracked_issues)["stalled_cve_issues"]

    async def get_pr(self, issue_num):
        self.log.notice(f"LOOKING FOR OR FOR {issue_num}")
        async for pr in self.pulls:
            if pr["body"] and str(issue_num) in pr["body"]:
                self.log.notice(f"FOUND {issue_num}")
                return pr
        return None

    @async_property(cache=True)
    async def tracked_issues(self):
        # A dict of assignee : outstanding_issue to be sent to slack
        # A placeholder for unassigned issuess, to be sent to #assignee
        # eventually
        assignee_and_issues = dict(unassigned=[])
        # Out-SLO issues to be sent to #envoy-setec
        stalled_issues = []
        stalled_cve_issues = []

        async for issue in self.issues:
            age = dt.now(datetime.timezone.utc) - dt.fromisoformat(
                issue["updated_at"].replace('Z', '+00:00'))
            message = self.pr_message(age, issue)

            is_approved = (
                "patch:approved"
                in [label["name"] for label in issue["labels"]])

            is_cve = (
                "cve/next"
                in [label["name"] for label in issue["labels"]])
            # If an CVE issue/PR hasn't been updated in a day, notify.
            if is_cve and not is_approved:
                pr = await self.get_pr(issue["number"])
                # If there's a pull associated with this CVE, check that for
                # updates instead of the issue.
                if pr:
                    age = dt.now(datetime.timezone.utc) - dt.fromisoformat(
                      pr["updated_at"].replace('Z', '+00:00'))
                if age > self.weekend_offset(24):
                    # If there's no PR, poll for issue updates.
                    stalled_cve_issues.append(message)

            should_ignore = (
                "notifier:ignore"
                in [label["name"] for label in issue["labels"]])
            # If an non-CVE issue hasn't been updated in a week, notify.
            if (not is_cve and age > self.weekend_offset(167) and
               issue["assignees"]) and not should_ignore:
                stalled_issues.append(message)

            has_assignee = False
            for assignee in issue["assignees"]:
                has_assignee = True
                assignee_and_issues[assignee["login"]] = (
                    assignee_and_issues.get(assignee["login"], []))
                assignee_and_issues[assignee["login"]].append(message)

            # If there was no assignee, track it as unassigned.
            if not has_assignee:
                assignee_and_issues['unassigned'].append(message)

        return dict(
            assignee_and_issues=assignee_and_issues,
            stalled_issues=stalled_issues,
            stalled_cve_issues=stalled_cve_issues)

    @async_property(cache=True)
    async def unassigned_issues(self):
        return (await self.assignee_and_issues)["unassigned"]

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument('reviewers', help="YAML reviewer config")
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
            stalled_cve = "\n".join(await self.stalled_cve_issues)
            if unassigned:
                await self.send_message(
                    channel='#envoy-security-team',
                    text=(
                        "*'Unassigned' Issues* "
                        "(Issues with no one assigned)\n"
                        f"{unassigned}"))
            if stalled:
                await self.send_message(
                    channel='#envoy-security-team',
                    text=(
                        f"*Stalled Issues* "
                        "(Issues with review out-SLO, please address)\n"
                        f"{stalled}"))
            if stalled_cve:
                await self.send_message(
                    channel='#envoy-security-team',
                    text=(
                        f"*Stalled CVE Issues* "
                        "(Issues with review out-SLO, please address)\n"
                        f"{stalled_cve}"))
        except SlackApiError as e:
            self.log.error(f"Unexpected error {e.response['error']}")

    def pr_message(self, age, issue):
        """Generate a pr message, bolding the time if it's out-SLO."""
        assignee_string = ""
        for assignee in issue["assignees"]:
            github_login = assignee["login"]
            handle = self.maintainers.get(github_login)
            if handle:
                assignee_string += f"<@{handle}> "
            else:
                assignee_string += "github_login "

        days = age.days
        hours = age.seconds // 3600
        return (
            f"<{issue['html_url']}|{html.escape(issue['title'])}> has been "
            f"waiting {days} days {hours} hours Assignees [{assignee_string}]")

    async def run(self):
        if not self.github_token:
            self.log.error(
                "Missing GITHUB_TOKEN: "
                "please check github workflow configuration")
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
