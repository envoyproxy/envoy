pin("github.com/repokitteh/modules", "4ee2ed0c3622aad7fcddc04cb5dc866e44a541e6")

use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")
use("github.com/envoyproxy/envoy/ci/repokitteh/modules/azure_pipelines.star", secret_token=get_secret('azp_token'))
use("github.com/envoyproxy/envoy/ci/repokitteh/modules/coverage.star")
use("github.com/envoyproxy/envoy/ci/repokitteh/modules/docs.star")
use("github.com/envoyproxy/envoy/ci/repokitteh/modules/newpr.star")
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
       "owner": "envoyproxy/coverage-shephards",
       "path": "(test/per_file_coverage.sh)",
       "github_status_label": "changes to Envoy coverage scripts",
       "auto_assign": True,
    },
    {
       "owner": "envoyproxy/runtime-guard-changes",
       "path": "(source/common/runtime/runtime_features.cc)",
       "github_status_label": "changes to Envoy runtime guards",
    },
    {
      "owner": "envoyproxy/api-shepherds!",
      "path": "(api/envoy/|docs/root/api-docs/)",
      "label": "api",
      "github_status_label": "any API change",
      "auto_assign": True,
    },
    {
      "owner": "envoyproxy/api-watchers",
      "path": "(api/envoy/|docs/root/api-docs/)",
    },
    {
      "owner": "envoyproxy/dependency-shepherds!",
      "path":
      "(bazel/.*repos.*\.bzl)|(bazel/dependency_imports\.bzl)|(api/bazel/.*\.bzl)|(.*/requirements\.txt)|(.*\.patch)",
      "label": "deps",
      "github_status_label": "any dependency change",
      "auto_assign": True,
    },
  ],
)
use("github.com/envoyproxy/envoy/ci/repokitteh/modules/versionchange.star")

def _backport():
  github.issue_label('backport/review')

handlers.command(name='backport', func=_backport)

def _milestone():
  github.issue_label('milestone/review')

handlers.command(name='milestone', func=_milestone)

def _nostalebot():
  github.issue_label('no stalebot')

handlers.command(name='nostalebot', func=_nostalebot)
