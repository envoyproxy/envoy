pin("github.com/repokitteh/modules", "4ee2ed0c3622aad7fcddc04cb5dc866e44a541e6")

use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")
use("github.com/repokitteh/modules/circleci.star", secret_token=get_secret('circle_token'))
use(
  "github.com/repokitteh/modules/ownerscheck.star",
  paths=[
    {
      "owner": "envoyproxy/api-shepherds!",
      "path": "api/",
      "label": "api",
    },
  ],
)

alias('retest', 'retry-circle')

def _backport():
  github.issue_label('backport/review')

handlers.command(name='backport', func=_backport)
