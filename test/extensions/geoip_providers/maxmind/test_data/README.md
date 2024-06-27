# Generating custom geoip databases

For testing purposes one could generate geolocation databases with custom data by using [Maxmind test utility](https://github.com/maxmind/MaxMind-DB/blob/main/cmd/write-test-data/main.go). Assuming your enviroment has Golang installed, follow these steps:
* Create `source` directory on the same level as `main.go` utility and copy all geolocation db files from [source-data](https://github.com/maxmind/MaxMind-DB/tree/main/source-data) directory.
* Update the target geolocation db file (e.g. GeoIP2-City-Test.json) in newly created `source` directory with test data.
* Create `out-data` directory on the same level as `main.go` utility and run:
  ```
  go run main.go --source source --target out-data
  ```
* Mmdb files should be generated in the `out-data` directory after running the command in previous step.
