set -e

git clone https://github.com/bombela/backward-cpp.git
cd backward-cpp
# v1.3 release
git reset --hard cd1c4bd9e48afe812a0e996d335298c455afcd92
cp backward.hpp $THIRDPARTY_BUILD/include
