#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]})/..)

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty | tr - .)~upstream_centos8

DOCKER_TAG=$(docker build -q - <<EOS
FROM centos:8
RUN dnf -y install rpm-build cmake make gcc-c++ wget sqlite-devel boost-devel zlib-devel
EOS
)

docker run --rm -i -v $SOURCE_DIR:/root/rpmbuild/SOURCES/laminar-$VERSION:ro -v $OUTPUT_DIR:/output $DOCKER_TAG bash -xe <<EOS
mkdir /build
cd /build

wget -O capnproto.tar.gz https://github.com/capnproto/capnproto/archive/v0.7.0.tar.gz
wget -O rapidjson.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz
md5sum -c <<EOF
a9de5f042f4cf05515c2d7dfc7f5df21  capnproto.tar.gz
badd12c511e081fec6c89c43a7027bce  rapidjson.tar.gz
EOF

tar xzf capnproto.tar.gz
tar xzf rapidjson.tar.gz

cd /build/capnproto-0.7.0/c++/
cmake3 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=off .
make -j4
make install

cd /build/rapidjson-1.1.0/
cmake -DRAPIDJSON_BUILD_EXAMPLES=off .
make install

cd
cat <<EOF > laminar.spec
Summary: Lightweight Continuous Integration Service
Name: laminar
Version: $VERSION
Release: 1
License: GPL
BuildRequires: systemd-units
Requires: sqlite-libs zlib

%description
Lightweight Continuous Integration Service

%prep

%build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ -DSYSTEMD_UNITDIR=%{_unitdir} %{_sourcedir}/laminar-$VERSION
pwd
make

%install
%make_install

%files
%{_bindir}/laminarc
%{_sbindir}/laminard
%{_unitdir}/laminar.service
%config(noreplace) %{_sysconfdir}/laminar.conf
%{_datarootdir}/bash-completion/completions/laminarc
%{_datarootdir}/zsh/site-functions/_laminarc

%post
echo Creating laminar user with home in %{_sharedstatedir}/laminar
useradd -r -d %{_sharedstatedir}/laminar -s %{_sbindir}/nologin laminar
mkdir -p %{_sharedstatedir}/laminar/cfg/{jobs,contexts,scripts}
chown -R laminar: %{_sharedstatedir}/laminar
EOF

rpmbuild -ba laminar.spec
mv rpmbuild/RPMS/x86_64/laminar-$VERSION-1.x86_64.rpm /output/
EOS
