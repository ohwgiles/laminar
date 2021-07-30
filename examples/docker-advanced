#!/bin/bash -eu

# Any failing command in a pipe will cause an error, instead
# of just an error in the last command in the pipe
set -o pipefail

# Log commands executed
set -x

# Simple way of getting the docker build tag:
tag=$(docker build -q - <<\EOF
	FROM debian:bullseye
	RUN apt-get update && apt-get install -y build-essential
EOF
)

# But -q suppresses the log output. If you want to keep it,
# you could use the following fancier way:

exec {pfd}<><(:) # get a new pipe
docker build - <<\EOF |
	FROM debian:bullseye
	RUN apt-get update && apt-get install -y build-essential
EOF
tee >(awk '/Successfully built/{print $3}' >&$pfd) # parse output to pipe
read tag <&$pfd # read tag back from pipe
exec {pfd}<&- # close pipe

# Alternatively, you can use the -t option to docker build
# to give the built image a name to refer to later. But then
# you need to ensure that it does not conflict with any other
# images, and handle cases where multiple instances of the
# job attempt to update the tagged image.

# If you want the image to be cleaned up on exit:
trap "docker rmi $tag" EXIT

# Now use the image to build something:
docker run -i --rm \
	-v "$PWD:$PWD" \
	-w "$PWD" \
	-u $(id -u):$(id -g) \
	$tag /bin/bash -eux \
<<EOF
	# The passed options mean we keep our current working
	# directory and user, so no permission problems on the
	# artifacts produced within the container.
	echo 'main(){puts("hello world");}' | gcc -x c -static -o hello -
EOF

# Test the result
./hello

