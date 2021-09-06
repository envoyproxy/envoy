from envoy_repo.api.bazel import repository_locations


def main(data):
    data.update(repository_locations.data.copy())
    return data
