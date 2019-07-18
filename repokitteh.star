use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")
use("github.com/repokitteh/modules/circleci.star", secret_token=get_secret('circle_token'))

use(
  "github.com/repokitteh/modules/owners.star#owners",
  paths=[
    ('itayd!', 'api/'),
  ],
)

alias('retest', 'retry-circle')
