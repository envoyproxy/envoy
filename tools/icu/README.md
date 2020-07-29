# Generate ICU data

This tool is used to generate ICU data using the
[ICU Data Build Tool](https://github.com/unicode-org/icu/blob/9b2092fa8921a765e27ee110f15fcc753f0c8e56/docs/userguide/icu_data/buildtool.md).
By default, the ICU data configuration file to be used is [`tools/icu/filters.json`](./filters.json).
One can edit [`tools/icu/filters.json`](./filters.json) to bundle more ICU data.

## Usage example

```
$ # Default usage:
$ ./tools/icu/generate_data.sh
```
