set -e

wget -O tclap-1.2.1.tar.gz https://sourceforge.net/projects/tclap/files/tclap-1.2.1.tar.gz/download
tar xf tclap-1.2.1.tar.gz
rsync -av tclap-1.2.1 $THIRDPARTY_SRC
