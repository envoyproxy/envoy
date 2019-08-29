use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")
use("github.com/repokitteh/modules/circleci.star", secret_token=get_secret('circle_token'))
use(
  "github.com/repokitteh/modules/ownerscheck.star#itay/s",
  paths=[
    ("rktest1", "api/"),
    ("itayd", ""),
  ],
)

alias('retest', 'retry-circle')
