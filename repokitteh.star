use("github.com/repokitteh/modules/22520d03464dd9503e036c7fa365c427723c4aaf/assign.star")
use("github.com/repokitteh/modules/22520d03464dd9503e036c7fa365c427723c4aaf/review.star")
use("github.com/repokitteh/modules/22520d03464dd9503e036c7fa365c427723c4aaf/wait.star")
use("github.com/repokitteh/modules/22520d03464dd9503e036c7fa365c427723c4aaf/circleci.star", secret_token=get_secret('circle_token'))
use(
  "github.com/repokitteh/modules/22520d03464dd9503e036c7fa365c427723c4aaf/ownerscheck.star",
  paths=[
    {
      "owner": "envoyproxy/api-shepherds!",
      "path": "api/",
      "label": "api",
    },
  ],
)

alias('retest', 'retry-circle')
