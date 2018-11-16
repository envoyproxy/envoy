use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")
use("github.com/repokitteh/modules/circleci.star#itayd-patch-4", secret_token=get_secret('circle_token'))

alias('retest', 'retry-circle')

enable('cancel-circle')
