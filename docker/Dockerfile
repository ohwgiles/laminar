FROM alpine:edge

EXPOSE 8080

LABEL org.label-schema.name="laminar" \
      org.label-schema.description="Fast and lightweight Continuous Integration" \
      org.label-schema.usage="/usr/doc/UserManual.md" \
      org.label-schema.url="https://laminar.ohwg.net" \
      org.label-schema.vcs-url="https://github.com/ohwgiles/laminar" \
      org.label-schema.schema-version="1.0" \
      org.label-schema.docker.cmd="docker run -d -p 8080:8080 laminar"

RUN apk add --no-cache -X http://dl-3.alpinelinux.org/alpine/edge/testing/ \
        sqlite-dev \
        zlib \
        capnproto \
        tini

ADD UserManual.md /usr/doc/

ADD . /build/laminar

RUN apk add --no-cache --virtual .build -X http://dl-3.alpinelinux.org/alpine/edge/testing/ \
        build-base \
        cmake \
        capnproto-dev \
        boost-dev \
        zlib-dev \
        rapidjson-dev && \
    cd /build/laminar && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr && \
    make -j4 && \
    make install && \
    apk del .build && \
    rm -rf /build

# Create laminar system user in "users" group
RUN adduser -SDh /var/lib/laminar -g 'Laminar' -G users laminar
# Set the working directory to the laminar user's home
WORKDIR /var/lib/laminar
# Run the preceeding as the user laminar
USER laminar

ENTRYPOINT [ "/sbin/tini", "--" ]
CMD [ "laminard" ]
