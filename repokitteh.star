pin("github.com/repokitteh/modules", "4ee2ed0c3622aad7fcddc04cb5dc866e44a541e6")

use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")
use("github.com/repokitteh/modules/circleci.star", secret_token=get_secret('circle_token'))
use("github.com/envoyproxy/envoy/ci/repokitteh/modules/azure_pipelines.star", secret_token=get_secret('azp_token'))
use(
  "github.com/envoyproxy/envoy/ci/repokitteh/modules/ownerscheck.star",
  paths=[
    {
      "owner": "envoyproxy/api-shepherds!",
      "path":
      "(api/envoy[\w/]*/(v1alpha\d?|v1|v2alpha\d?|v2))|(api/envoy/type/(matcher/)?\w+.proto)",
      "label": "v2-freeze",
      "allow_global_approval": False,
      "github_status_label": "v2 freeze violations",
    },
    {
      "owner": "envoyproxy/api-shepherds!",
      "path": "api/envoy/",
      "label": "api",
      "github_status_label": "any API change",
    },
    {
      "owner": "envoyproxy/maintainers!",
      "do_not_tag": True,
      "path": "api/|source/",
      "exclude_path": "docs/root/version_history/current.rst",
      "label": "pr-check/release-notes",
      "allow_global_approval": False,
      "github_status_label": "release note validation",
    },
    {
      "owner": "envoyproxy/maintainers!",
      "do_not_tag": True,
      "path": "source/",
      "exclude_path": "docs/root/",
      "label": "pr-check/docs",
      "allow_global_approval": False,
      "github_status_label": "doc changes validation",
    },
    {
      "owner": "envoyproxy/maintainers!",
      "do_not_tag": True,
      "path": "source/",
      "exclude_path": "test/",
      "label": "pr-check/test",
      "allow_global_approval": False,
      "github_status_label": "testing validation",
    },
    {
      "owner": "envoyproxy/api-watchers",
      "path": "api/envoy/",
    },
    {
      "owner": "envoyproxy/dependency-watchers",
      "path":
      "(bazel/repository_locations\.bzl)|(api/bazel/repository_locations\.bzl)|(.*/requirements\.txt)",
    },
  ],
)

alias('retest-circle', 'retry-circle')
alias('retest', 'retry-azp')

def _backport():
  github.issue_label('backport/review')

handlers.command(name='backport', func=_backport)
