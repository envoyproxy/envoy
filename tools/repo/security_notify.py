#!/usr/bin/env python3

# Script for notifying about security violations via Slack
# Sends alerts when unauthorized workflow triggers are detected

import json
import os
import sys
from functools import cached_property

from slack_sdk.web.async_client import AsyncWebClient
from slack_sdk.errors import SlackApiError

from aio.run import runner


class SecurityNotifier(runner.Runner):

    @cached_property
    def slack_client(self):
        return AsyncWebClient(token=self.slack_bot_token)

    @cached_property
    def slack_bot_token(self):
        return os.getenv('SLACK_BOT_TOKEN')

    @cached_property
    def violation_data(self):
        with open(self.args.violation_file, 'r') as f:
            return json.load(f)

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            '--violation_file', required=True, help="JSON file containing violation data")
        parser.add_argument(
            '--channel', default='#envoy-maintainer-oncall', help="Slack channel to notify")
        parser.add_argument(
            '--dry_run',
            action="store_true",
            help="Don't post slack messages, just show what would be posted")

    async def notify(self):
        violation = self.violation_data

        message_blocks = [{
            "type": "section",
            "text": {
                "type": "mrkdwn",
                "text": "🚨 *SECURITY VIOLATION DETECTED* 🚨"
            }
        }, {
            "type": "section",
            "text": {
                "type": "mrkdwn",
                "text": "*Unauthorized workflow trigger attempt*"
            }
        }, {
            "type":
                "section",
            "fields": [{
                "type": "mrkdwn",
                "text": f"*Actor:*\n{violation['actor']}"
            }, {
                "type": "mrkdwn",
                "text": f"*Repository:*\n{violation['repository']}"
            }, {
                "type": "mrkdwn",
                "text": f"*Event Type:*\n{violation['event_type']}"
            }, {
                "type": "mrkdwn",
                "text": f"*Workflow Run ID:*\n{violation['workflow_run_id']}"
            }]
        }]

        if violation.get('pr_number'):
            message_blocks.append({
                "type": "section",
                "text": {
                    "type": "mrkdwn",
                    "text": f"*Associated PR:* #{violation['pr_number']}"
                }
            })

        message_blocks.append({
            "type": "section",
            "text": {
                "type": "mrkdwn",
                "text": f"<{violation['workflow_run_url']}|View workflow run>"
            }
        })

        message_blocks.append({
            "type": "section",
            "text": {
                "type": "mrkdwn",
                "text": "<!channel> This security violation requires immediate attention."
            }
        })

        # Send to multiple channels
        channels = ['#envoy-maintainers', '#envoy-security-team']
        errors = []

        for channel in channels:
            self.log.notice(f"Slack message ({channel}):\n{json.dumps(message_blocks, indent=2)}")
            if self.args.dry_run:
                continue
            try:
                await self.slack_client.chat_postMessage(
                    channel=channel,
                    text="Security Violation Detected - Unauthorized workflow trigger",
                    blocks=message_blocks)
                self.log.notice(f"Security violation notification sent to {channel}")
            except SlackApiError as e:
                self.log.error(
                    f"Failed to send Slack notification to {channel}: {e.response['error']}")
                errors.append(channel)

        return 1 if errors else 0

    async def run(self):
        if not self.slack_bot_token and not self.args.dry_run:
            self.log.error("Missing SLACK_BOT_TOKEN")
            return 1

        if not os.path.exists(self.args.violation_file):
            self.log.error(f"Violation file not found: {self.args.violation_file}")
            return 1

        return await self.notify()


def main(*args):
    return SecurityNotifier(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
