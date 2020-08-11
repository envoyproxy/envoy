load("github.com/repokitteh/modules/lib/utils.star", "react")

_azp_context_prefix = "ci/azp: "

def _retry_azp(organization, project, build_id, token):
    """Makes an Azure Pipelines Build API request with retry"""

    url = "https://dev.azure.com/{organization}/{project}/_apis/build/builds/{buildId}?retry=true&api-version=5.1".format(organization = organization, project = project, buildId = build_id)
    return http(url = url, method = "PATCH", headers = {
        "authorization": "Basic " + token,
        "content-type": "application/json;odata=verbose",
    })

def _get_azp_checks():
    github_checks = github.check_list_runs()["check_runs"]

    check_ids = []
    checks = []
    for check in github_checks:
        # Filter out job level GitHub check, which is not individually retriable.
        if check["app"]["slug"] == "azure-pipelines" and "jobId" not in check["details_url"] and check["external_id"] not in check_ids:
            check_ids.append(check["external_id"])
            checks.append(check)

    return checks

def _retry(config, comment_id, command):
    msgs = "Retrying Azure Pipelines, to retry CircleCI checks, use `/retest-circle`.\n"
    checks = _get_azp_checks()

    retried_checks = []
    for check in checks:
        name_with_link = "[{}]({})".format(check["name"], check["details_url"])
        if check["status"] != "completed":
            msgs += "Cannot retry non-completed check: {}, please wait.\n".format(name_with_link)
        elif check["conclusion"] != "failure":
            msgs += "Check {} didn't fail.\n".format(name_with_link)
        else:
            _, build_id, project = check["external_id"].split("|")
            _retry_azp("cncf", project, build_id, config["token"])
            retried_checks.append(name_with_link)

    if len(retried_checks) == 0:
        react(comment_id, msgs)
    else:
        react(comment_id, None)
        msgs += "Retried failed jobs in: {}".format(", ".join(retried_checks))
        github.issue_create_comment(msgs)

handlers.command(name = "retry-azp", func = _retry)
