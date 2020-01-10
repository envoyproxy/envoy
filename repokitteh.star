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

def _test(delivery_id):
    print("called")
    store_put('pinged', 'x')
    print(store_get('pinged'))
    github.issue_create_comment("test")

handlers.command(name='test', func=_test)
