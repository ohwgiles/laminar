#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]})/..)

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty | tr - .)~upstream_rocky10

DOCKER_TAG=$(docker build -q - <<EOS
FROM rockylinux/rockylinux:10
RUN dnf -y update && dnf -y install rpm-build cmake make gcc-c++ wget sqlite-devel boost-devel zlib-devel systemd-units
RUN mkdir /rapidjson && pushd /rapidjson && \
	wget -O rapidjson.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz && \
	echo "badd12c511e081fec6c89c43a7027bce  rapidjson.tar.gz" | md5sum -c && \
	tar xzf rapidjson.tar.gz && \
	cd rapidjson-1.1.0 && \
	cmake -DRAPIDJSON_BUILD_EXAMPLES=off . && \
	make install && \
	popd && rm -rf /rapidjson
RUN mkdir /capnproto && pushd /capnproto && \
	wget -O capnproto.tar.gz https://github.com/capnproto/capnproto/archive/v1.5.0.tar.gz && \
	echo "cd64102577a89714b9d58494a7e34cee  capnproto.tar.gz" | md5sum -c && \
	tar xzf capnproto.tar.gz && \
	cd capnproto-1.5.0/c++ && \
	cmake -DCMAKE_CXX_FLAGS=-fPIE -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=off . && \
	make -j4 && \
	make install && \
	popd && rm -rf /capnproto
EOS
)

docker run --rm -i -v $SOURCE_DIR:/root/rpmbuild/SOURCES/laminar-$VERSION:ro -v $OUTPUT_DIR:/output $DOCKER_TAG bash -xe <<EOS
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
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DLAMINAR_VERSION=$VERSION -DSYSTEMD_UNITDIR=%{_unitdir} %{_sourcedir}/laminar-$VERSION
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
%{_mandir}/man8/laminard.8.gz
%{_mandir}/man1/laminarc.1.gz

%post
echo Creating laminar user with home in %{_sharedstatedir}/laminar
useradd -r -d %{_sharedstatedir}/laminar -s %{_sbindir}/nologin laminar
mkdir -p %{_sharedstatedir}/laminar/cfg/{jobs,contexts,scripts}
chown -R laminar: %{_sharedstatedir}/laminar
EOF

rpmbuild -ba laminar.spec
mv rpmbuild/RPMS/x86_64/laminar-$VERSION-1.x86_64.rpm /output/
EOS
