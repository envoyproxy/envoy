use("github.com/repokitteh/modules/assign.star#22520d03464dd9503e036c7fa365c427723c4aaf")
use("github.com/repokitteh/modules/review.star#22520d03464dd9503e036c7fa365c427723c4aaf")
use("github.com/repokitteh/modules/wait.star#22520d03464dd9503e036c7fa365c427723c4aaf")
use("github.com/repokitteh/modules/circleci.star#22520d03464dd9503e036c7fa365c427723c4aaf", secret_token=get_secret('circle_token'))
use(
  "github.com/repokitteh/modules/ownerscheck.star#22520d03464dd9503e036c7fa365c427723c4aaf",
  paths=[
    {
      "owner": "envoyproxy/api-shepherds!",
      "path": "api/",
      "label": "api",
    },
  ],
)

alias('retest', 'retry-circle')
