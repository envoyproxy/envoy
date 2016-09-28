How to publish new docs:

* The docs are contained in the gh-pages branch in the repo.
* Clone a fresh copy of the repo into a parallel envoy-docs directory.
* Run: `make publish_docs`
* Run: `cd ../envoy-docs`
* Verify the latest commit looks OK
* Run: `git push origin gh-pages:gh-pages`
