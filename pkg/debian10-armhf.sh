#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]})/..)

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty)-1~upstream-debian10

DOCKER_TAG=$(docker build -q - <<EOS
FROM debian:10-slim
RUN dpkg --add-architecture armhf && apt-get update && apt-get install -y wget cmake crossbuild-essential-armhf capnproto libcapnp-dev:armhf rapidjson-dev libsqlite3-dev:armhf libboost-dev:armhf zlib1g-dev:armhf
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
Depends: libcapnp-0.7.0, libsqlite3-0, zlib1g
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
