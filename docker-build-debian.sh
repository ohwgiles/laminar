#!/bin/bash

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]}))

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty)

docker run --rm -i -v $SOURCE_DIR:/laminar:ro -v $OUTPUT_DIR:/output debian:9-slim bash -xe <<EOS

apt-get update
apt-get install -y wget cmake g++ libsqlite3-dev libboost-filesystem1.62-dev zlib1g-dev

mkdir /build
cd /build

wget -O capnproto.tar.gz https://github.com/capnproto/capnproto/archive/3079784bfaf3ba05edacfc63d6d494b76a85a3a5.tar.gz
wget -O websocketpp.tar.gz https://github.com/zaphoyd/websocketpp/archive/0.7.0.tar.gz
wget -O rapidjson.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz
md5sum -c <<EOF
c5c04c1892a381e30bd032a6bceef111  capnproto.tar.gz
5027c20cde76fdaef83a74acfcf98e23  websocketpp.tar.gz
badd12c511e081fec6c89c43a7027bce  rapidjson.tar.gz
EOF

tar xzf capnproto.tar.gz
tar xzf websocketpp.tar.gz
tar xzf rapidjson.tar.gz

cd /build/capnproto-3079784bfaf3ba05edacfc63d6d494b76a85a3a5/c++/
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=off .
make -j4
make install

cd /build/websocketpp-0.7.0/
cmake .
make install

cd /build/rapidjson-1.1.0/
cmake -DRAPIDJSON_BUILD_EXAMPLES=off .
make install

cd /build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ /laminar
make -j4
mkdir laminar
make DESTDIR=laminar install/strip

mkdir laminar/DEBIAN
cat <<EOF > laminar/DEBIAN/control
Package: laminar
Version: $VERSION
Section: 
Priority: optional
Architecture: amd64
Maintainer: Oliver Giles <web ohwg net>
Depends: libsqlite3-0, libboost-filesystem1.62.0, zlib1g
Description: Lightweight Continuous Integration Service
EOF
cat <<EOF > laminar/DEBIAN/postinst
#!/bin/bash
echo Creating laminar user with home in /var/lib/laminar
useradd -r -d /var/lib/laminar -s /usr/sbin/nologin laminar
mkdir -p /var/lib/laminar/cfg/{jobs,nodes,scripts}
chown -R laminar: /var/lib/laminar
EOF
chmod +x laminar/DEBIAN/postinst

dpkg-deb --build laminar
mv laminar.deb /output/laminar-$VERSION-1-amd64.deb
EOS
