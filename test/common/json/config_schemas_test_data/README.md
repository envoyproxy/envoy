# Schema Testing

Schema testing works by using Python to generate JSON files as input for a parameterized test in the C++ framework. This was done since it is far simpler to manipulate JSON objects in Python than it is in C++.

On each test run, Bazel will execute `generate_test_data.py`. This will write a JSON file per test. Each file contains the name of the schema to test against, the blob of data to validate, and whether or not the validation should throw an error.

Each schema gets its own Python file in `test_data/`. The file must be named `test_*.py` for it to be executed. It must contain the function `def test(writer)`.

If the schema you want to test does not have a file, please create one. See other files for the boilerplate of writing a suite of tests.
