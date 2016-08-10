#!/bin/bash

SHA=`git -C $1 rev-parse HEAD`
grep -q $SHA $2
if [[ $? -ne 0 ]]; then
  echo -e "#include \"common/common/version.h\"\n\n" > $2
  echo -e "const std::string VersionInfo::GIT_SHA(\"$SHA\");" >> $2
fi
