load("github.com/repokitteh/modules/lib/utils.star", "react")

_azp_context_prefix = "ci/azp: "
_azp_organization = "cncf"

def _retry_azp(project, build_id, token):
    """Makes an Azure Pipelines Build API request with retry"""

    url = "https://dev.azure.com/{organization}/{project}/_apis/build/builds/{buildId}?retry=true&api-version=5.1".format(organization = _azp_organization, project = project, buildId = build_id)
    return http(url = url, method = "PATCH", secret_headers = {
        "authorization": "Basic " + token,
        "content-type": "application/json;odata=verbose",
    })

def _get_azp_checks():
    github_checks = github.check_list_runs()["check_runs"]

    check_ids = []
    checks = []
    for check in github_checks:
        # Filter out job level GitHub check, which is not individually retriable.
        if check["app"]["slug"] == "azure-pipelines" and check["external_id"] not in check_ids:
            check_ids.append(check["external_id"])
        if check["app"]["slug"] == "azure-pipelines" and check["name"].endswith(")"):
            checks.append(check)

    return (check_ids, checks)

def _get_azp_link(check_id):
    _, build_id, project = check_id.split("|")
    return "https://dev.azure.com/{organization}/{project}/_build/results?buildId={buildId}&view=results".format(organization = _azp_organization, project = project, buildId = build_id)

def _retry(config, comment_id, command):
    if len(command.parts) > 1 and command.parts[1] == "mobile":
        return
    check_ids, checks = _get_azp_checks()

    retried_checks = []
    reaction = "confused"
    for check_id in check_ids:
        subchecks = [c for c in checks if c["external_id"] == check_id]
        if len(subchecks) == 0:
            continue

        name_with_link = "[{}]({})".format(subchecks[0]["name"].split(" ")[0], _get_azp_link(check_id))

        has_running = False
        has_failure = False
        for check in subchecks:
            if check["conclusion"] != None and check["conclusion"] != "success":
                has_failure = True
            if check["status"] == "in_progress":
                has_running = True

        if has_failure:
            _, build_id, project = check_id.split("|")
            _retry_azp(project, build_id, config["token"])
            retried_checks.append(name_with_link)

    if len(retried_checks) != 0:
        reaction = "+1"

    github.issue_create_comment_reaction(comment_id, reaction)


handlers.command(name = "retry-azp", func = _retry)
