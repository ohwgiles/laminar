#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]})/..)

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty)-1~upstream-debian9

DOCKER_TAG=$(docker build -q - <<EOS
FROM debian:9-slim
RUN dpkg --add-architecture armhf && apt-get update && apt-get install -y wget cmake crossbuild-essential-armhf libsqlite3-dev:armhf libboost-dev:armhf zlib1g-dev:armhf
EOS
)

docker run --rm -i -v $SOURCE_DIR:/laminar:ro -v $OUTPUT_DIR:/output $DOCKER_TAG bash -xe <<EOS

mkdir /build
cd /build

cat > toolchain.cmake <<EOF
SET(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
SET(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)
EOF

wget -O capnproto.tar.gz https://github.com/capnproto/capnproto/archive/v0.7.0.tar.gz
wget -O rapidjson.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz
md5sum -c <<EOF
a9de5f042f4cf05515c2d7dfc7f5df21  capnproto.tar.gz
badd12c511e081fec6c89c43a7027bce  rapidjson.tar.gz
EOF

tar xzf capnproto.tar.gz
tar xzf rapidjson.tar.gz

mkdir capnproto-host
cd capnproto-host
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=off /build/capnproto-0.7.0/c++/
make -j4
make install

cd /build/capnproto-0.7.0/c++/
cmake -DCMAKE_TOOLCHAIN_FILE=../../toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr/arm-linux-gnueabihf/ .
make -j4
make install

cd /build/rapidjson-1.1.0/
cmake -DRAPIDJSON_BUILD_EXAMPLES=off -DCMAKE_INSTALL_PREFIX=/usr/arm-linux-gnueabihf/ .
make install

cd /build
cmake \
	-DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
	-DCMAKE_LINKER=/usr/bin/arm-linux-gnueabihf-ld \
	-DCMAKE_OBJCOPY=/usr/bin/arm-linux-gnueabihf-objcopy \
	-DCMAKE_STRIP=/usr/bin/arm-linux-gnueabihf-strip \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/ \
	-DZSH_COMPLETIONS_DIR=/usr/share/zsh/functions/Completion/Unix \
	/laminar
make -j4
mkdir laminar
make DESTDIR=laminar install/strip

mkdir laminar/DEBIAN
cat <<EOF > laminar/DEBIAN/control
Package: laminar
Version: $VERSION
Section: 
Priority: optional
Architecture: armhf
Maintainer: Oliver Giles <web ohwg net>
Depends: libsqlite3-0, zlib1g
Description: Lightweight Continuous Integration Service
EOF
echo /etc/laminar.conf > laminar/DEBIAN/conffiles
cat <<EOF > laminar/DEBIAN/postinst
#!/bin/bash
echo Creating laminar user with home in /var/lib/laminar
useradd -r -d /var/lib/laminar -s /usr/sbin/nologin laminar
mkdir -p /var/lib/laminar/cfg/{jobs,nodes,scripts}
chown -R laminar: /var/lib/laminar
EOF
chmod +x laminar/DEBIAN/postinst

dpkg-deb --build laminar
mv laminar.deb /output/laminar_${VERSION}_armhf.deb
EOS
