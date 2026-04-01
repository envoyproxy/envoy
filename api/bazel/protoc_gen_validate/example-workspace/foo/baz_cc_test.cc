#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <ostream>

#include "foo/subdir_without_package/baz.pb.h"
#include "foo/subdir_without_package/baz.pb.validate.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

int main(int nargs, char** args) {
  pgv::example::foo::Baz baz;
  pgv::ValidationMsg msg;
  Validate(baz, &msg);

  return EXIT_SUCCESS;
}
