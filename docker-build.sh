#!/bin/bash -e

$PROJECT_DIR=`dirname ${BASH_SOURCE[0]}`

docker build -t laminar${1:-latest} -f $PROJECT_DIR/docker/Dockerfile $PROJECT_DIR
