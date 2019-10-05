# Introduction

[Laminar](http://laminar.ohwg.net) is a lightweight and modular Continuous Integration service for Linux. It is self-hosted and developer-friendly, eschewing a configuration web UI in favor of simple version-controllable configuration files and scripts.

Laminar encourages the use of existing GNU/Linux tools such as `bash` and `cron` instead of reinventing them.

Although the status and progress front-end is very user-friendly, administering a Laminar instance requires writing shell scripts and manually editing configuration files. That being said, there is nothing esoteric here and the tutorial below should be straightforward for anyone with even very basic Linux server administration experience.

Throughout this document, the fixed base path `/var/lib/laminar` is used. This is the default path and can be changed by setting `LAMINAR_HOME` in `/etc/laminar.conf` as desired.

## Terminology

- *job*: a task, identified by a name, comprising of one or more executable scripts.
- *run*: a numbered execution of a *job*

---

# Installing Laminar

Pre-built packages are available for Debian 9 (Stretch) and CentOS 7 on x86_64. Alternatively, Laminar may be built from source for any Linux distribution.

## Installation from binaries

Alternatively to the source-based approach shown above, precompiled packages are supplied for x86_64 Debian 9 (Stretch) and CentOS 7

Under Debian:

```bash
wget https://github.com/ohwgiles/laminar/releases/download/0.6/laminar-0.6-1-amd64.deb
sudo apt install laminar-0.6-1-amd64.deb
```

Under CentOS:

```bash
wget https://github.com/ohwgiles/laminar/releases/download/0.5/laminar-0.6-1.x86_64.rpm
sudo yum install laminar-0.6-1.x86_64.rpm
```

Both install packages will create a new `laminar` user and install (but not activate) a systemd service for launching the laminar daemon.

## Building from source

See the [development README](https://github.com/ohwgiles/laminar) for instructions for installing from source.

## Building for Docker

You can build an image that runs `laminard` by default, and contains `laminarc` for use based on `alpine:edge` using the `Dockerfile` in the `docker/` directory.

```bash
# from the repository root:
docker build [-t image:tag] -f docker/Dockerfile .
```

Keep in mind that this is meant to be used as a base image to build from, so it contains only the minimum packages required to run laminar. The only shell available by default is sh and it does not even have ssh or git. You can use this image to run a basic build server, but it is recommended that you build a custom image from this base to better suit your needs.

The container will execute `laminard` by default. To start a laminar server with docker you can simply run the image as a daemon.

```bash
docker run -d --name laminar_server -p 8080:8080 [-v laminardir|laminar.conf] laminar:latest
```

You can customize laminar and persist your data by mounting your laminar directory to `/var/lib/laminar` and/or mounting a custom configuration file to `/etc/laminar.conf`.

Executing `laminarc` may be done in any of the usual ways, for example:

```bash
docker exec -i laminar_server laminarc queue example_task
```

Alternatively, you might [use an external `laminarc`](#Triggering-on-a-remote-laminar-instance).

---

# Service configuration

Use `systemctl start laminar` to start the laminar system service and `systemctl enable laminar` to launch it automatically on system boot.

After starting the service, an empty laminar dashboard should be available at http://localhost:8080

Laminar's configuration file may be found at `/etc/laminar.conf`. Laminar will start with reasonable defaults if no configuration can be found.

## Running on a different HTTP port or Unix socket

Edit `/etc/laminar.conf` and change `LAMINAR_BIND_HTTP` to `IPADDR:PORT`, `unix:PATH/TO/SOCKET` or `unix-abstract:SOCKETNAME`. `IPADDR` may be `*` to bind on all interfaces. The default is `*:8080`.

Do not attempt to run laminar on port 80. This requires running as `root`, and Laminar will not drop privileges when executing job scripts! For a more complete integrated solution (including SSL), run laminar as a reverse proxy behind a regular webserver.

## Running behind a reverse proxy

A reverse proxy is required if you want Laminar to share a port with other web services. It is also recommended to improve performance by serving artefacts directly or providing a caching layer for static assets.

If you use [artefacts](#Archiving-artefacts), note that Laminar is not designed as a file server, and better performance will be achieved by allowing the frontend web server to serve the archive directory directly (e.g. using a `Location` directive).

Laminar uses Sever Sent Events to provide a responsive, auto-updating display without polling. Most frontend webservers should handle this without any extra configuration.

If you use a reverse proxy to host Laminar at a subfolder instead of a subdomain root, the `<base href>` needs to be updated to ensure all links point to their proper targets. This can be done by setting `LAMINAR_BASE_URL` in `/etc/laminar.conf`.

## Set the page title

Change `LAMINAR_TITLE` in `/etc/laminar.conf` to your preferred page title. For further WebUI customization, consider using a [custom style sheet](#Customizing-the-WebUI).

## More configuration options

See the [reference section](#Service-configuration-file)

---

# Defining a job

To create a job that downloads and compiles [GNU Hello](https://www.gnu.org/software/hello/), create the file `/var/lib/laminar/cfg/jobs/hello.run` with the following content:

```bash
#!/bin/bash -ex
wget ftp://ftp.gnu.org/gnu/hello/hello-2.10.tar.gz
tar xzf hello-2.10.tar.gz
cd hello-2.10
./configure
make
```

Laminar uses your script's exit code to determine whether to mark the run as successful or failed. If your script is written in bash, the [`-e` option](http://tldp.org/LDP/abs/html/options.html) is helpful for this. See also [Exit and Exit Status](http://tldp.org/LDP/abs/html/exit-status.html).

Don't forget to mark the script executable:

```bash
chmod +x /var/lib/laminar/cfg/jobs/hello.run
```

---

# Triggering a run

When triggering a run, the job is first added to a queue of upcoming tasks. If the server is busy, the job may wait in this queue for a while. It will only be assigned a job number when it leaves this queue and starts executing. The job number may be useful to the client that triggers the run, so there are a few ways to trigger a run.

To add the `hello` job to the queue ("fire-and-forget"), execute

```bash
laminarc queue hello
```

In this case, laminarc returns immediately, with its error code indicating whether adding the job to the queue was sucessful.

To queue the job and wait until it leaves the queue and starts executing, use

```bash
laminarc start hello
```

In this case, laminarc blocks until the job starts executing, or returns immediately if queueing failed. The run number will be printed to standard output.

To launch and run the `hello` job to completion, execute

```bash
laminarc run hello
```

In all cases, a started run means the `/var/lib/laminar/cfg/jobs/hello.run` script will be executed, with a working directory of `/var/lib/laminar/run/hello/1` (or current run number)

The result and log output should be visible in the Web UI at http://localhost:8080/jobs/hello/1

Also note that all the above commands can simultaneously trigger multiple different jobs:

```bash
laminarc queue test-host test-target
```

## Isn't there a "Build Now" button I can click?

This is against the design principles of Laminar and was deliberately excluded. Laminar's web UI is strictly read-only, making it simple to deploy in mixed-permission or public environments without an authentication layer. Furthermore, Laminar tries to encourage ideal continuous integration, where manual triggering is an anti-pattern. Want to make a release? Push a git tag and implement a post-receive hook. Want to re-run a build due to sporadic failure/flaky tests? Fix the tests locally and push a patch. Experience shows that a manual trigger such as a "Build Now" button is often used as a crutch to avoid doing the correct thing, negatively impacting traceability and quality.

## Listing jobs from the command line

`laminarc` may be used to inspect the server state:

- `laminarc show-jobs`: Lists all files matching `/var/lib/laminar/cfg/jobs/*.run` on the server side.
- `laminarc show-running`: Lists all currently running jobs and their run numbers.
- `laminarc show-queued`: Lists all jobs waiting in the queue.

## Triggering a job at a certain time

This is what `cron` is for. To trigger a build of `hello` every day at 0300, add

```
0 3 * * * LAMINAR_REASON="Nightly build" laminarc queue hello
```

to `laminar`'s crontab. For more information about `cron`, see `man crontab`.

`LAMINAR_REASON` is an optional human-readable string that will be displayed in the web UI as the cause of the build.

## Triggering on a git commit

This is what [git hooks](https://git-scm.com/book/gr/v2/Customizing-Git-Git-Hooks) are for. To create a hook that triggers the `example-build` job when a push is made to the `example` repository, create the file `hooks/post-receive` in the `example.git` bare repository.

```bash
#!/bin/bash
LAMINAR_REASON="Push to git repository" laminarc queue example-build
```

What if your git server is not the same machine as the laminar instance?

## Triggering on a remote laminar instance

`laminarc` and `laminard` communicate by default over an [abstract unix socket](http://man7.org/linux/man-pages/man7/unix.7.html). This means that any user **on the same machine** can send commands to the laminar service.

On a trusted network, you might want `laminard` to listen for commands on a TCP port instead. To achieve this, in `/etc/laminar.conf`, set

```
LAMINAR_BIND_RPC=*:9997
```

or any interface/port combination you like. This option uses the same syntax as `LAMINAR_BIND_HTTP`.

Then, point `laminarc` to the new location using an environment variable:

```bash
LAMINAR_HOST=192.168.1.1:9997 laminarc queue example
```

If you need more flexibility, consider running the communication channel as a regular unix socket and applying user and group permissions to the file. To achieve this, set

```
LAMINAR_BIND_RPC=unix:/var/run/laminar.sock
```

or similar path in `/etc/laminar.conf`.

This can be securely and flexibly combined with remote triggering using `ssh`. There is no need to allow the client full shell access to the server machine, the ssh server can restrict certain users to certain commands (in this case `laminarc`). See [the authorized_keys section of the sshd man page](https://man.openbsd.org/sshd#AUTHORIZED_KEYS_FILE_FORMAT) for further information.

## Triggering on a push to GitHub

Consider using [webhook](https://github.com/adnanh/webhook) or a similar application to call `laminarc`.

## Viewing job logs

A job's console output can be viewed on the Web UI at http://localhost:8080/jobs/$NAME/$NUMBER.

Additionally, the raw log output may be fetched over a plain HTTP request to http://localhost:8080/log/$NAME/$NUMBER. The response will be chunked, allowing this mechanism to also be used for in-progress jobs. Furthermore, the special endpoint http://localhost:8080/log/$NAME/latest will redirect to the most recent log output. Be aware that the use of this endpoint may be subject to races when new jobs start.

---

# Job chains

A typical pipeline may involve several steps, such as build, test and deploy. Depending on the project, these may be broken up into seperate laminar jobs for maximal flexibility.

The preferred way to accomplish this in Laminar is to use the same method as [regular run triggering](#Triggering-a-run), that is, calling `laminarc` directly in your `example.run` scripts.

```bash
#!/bin/bash -xe

# simultaneously starts example-test-qemu and example-test-target
# and returns a non-zero error code if either of them fail
laminarc run example-test-qemu example-test-target
```

An advantage to using this `laminarc` approach from bash or other scripting language is that it enables highly dynamic pipelines, since you can execute commands like

```bash
if [ ... ]; then
  laminarc run example-downstream-special
else
  laminarc run example-downstream-regular
fi

laminarc run example-test-$TARGET_PLATFORM
```

`laminarc` reads the `$JOB` and `$RUN` variables set by `laminard` and passes them as part of the queue/start/run request so the dependency chain can always be traced back.

---

# Parameterized runs

Any argument passed to `laminarc` of the form `var=value` will be exposed as an environment variable in the corresponding build scripts. For example:

```bash
laminarc queue example foo=bar
```

In `/var/lib/laminar/cfg/jobs/example.run`:

```bash
#!/bin/bash
if [ "$foo" == "bar" ]; then
   ...
else
   ...
fi
```

---

# Pre- and post-build actions

If the script `/var/lib/laminar/cfg/jobs/example.before` exists, it will be executed as part of the `example` job, before the primary `/var/lib/laminar/cfg/jobs/example.run` script.

Similarly, if the script `/var/lib/laminar/cfg/jobs/example.after` script exists, it will be executed as part of the `example` job, after the primary `var/lib/laminar/cfg/jobs/example.run` script. In this script, the `$RESULT` variable will be `success`, `failed`, or `aborted` according to the result of `example.run`.

See also [script execution order](#Script-execution-order)


## Conditionally trigger a downstream job

Often, you may wish to only trigger the `example-test` job if the `example-build` job completed successfully. `example-build.after` might look like this:

```bash
#!/bin/bash -xe
if [ "$RESULT" == "success" ]; then
  laminarc queue example-test
fi
```

## Passing data between scripts

Any script can set environment variables that will stay exposed for subsequent scripts of the same run using `laminarc set`. In `example.before`:

```bash
#!/bin/bash
laminarc set foo=bar
```

Then in `example.run`

```bash
#!/bin/bash
echo $foo            # prints "bar"
```

This works because laminarc reads `$JOB` and `$NUM` and passes them to the laminar daemon as part of the `set` request. (It is thus possible to set environment variables on other jobs by overriding these variables, but this is not very useful).

---

# Archiving artefacts

Laminar's default behaviour is to remove the run directory `/var/lib/laminar/run/JOB/RUN` after its completion. This prevents the typical CI disk usage explosion and encourages the user to judiciously select artefacts for archive.

Laminar provides an archive directory `/var/lib/laminar/archive/JOB/RUN` and exposes its path in `$ARCHIVE`. `example-build.after` might look like this:

```bash
#!/bin/bash -xe
cp example.out $ARCHIVE/
```

This folder structure has been chosen to make it easy for system administrators to host the archive on a separate partition or network drive.


## Accessing artefacts from an upstream build

Rather than implementing a separate mechanism for this, the path of the upstream's archive should be passed to the downstream run as a parameter. See [Parameterized runs](#Parameterized-runs).

---

# Email and IM Notifications

As well as per-job `.after` scripts, a common use case is to send a notification for every job completion. If the global `after` script at `/var/lib/laminar/cfg/after` exists, it will be executed after every job. One way to use this might be:

```bash
#!/bin/bash -xe
if [ "$RESULT" != "$LAST_RESULT" ]; then
  sendmail -t <<EOF
To: engineering@company.com
Subject: Laminar $JOB #$RUN: $RESULT
From: laminar-ci@company.com

Laminar $JOB #$RUN: $RESULT
EOF
fi
```

Of course, you can make this as pretty as you like. A [helper script](#Helper-scripts) can be a good choice here.

If you want to send to different addresses dependending on the job, replace `engineering@company.com` above with a variable, e.g. `$RECIPIENTS`, and set `RECIPIENTS=nora@company.com,joe@company.com` in `/var/lib/laminar/cfg/jobs/JOB.env`. See [Environment variables](#Environment-variables).

You could also update the `$RECIPIENTS` variable dynamically based on the build itself. For example, if your run script accepts a parameter `$rev` which is a git commit id, as part of your job's `.after` script you could do the following:

```bash
author_email=$(git show -s --format='%ae' $rev)
laminarc set RECIPIENTS $author_email
```

---

# Helper scripts

The directory `/var/lib/laminar/cfg/scripts` is automatically prepended to the `PATH` of all runs. It is a convenient place to drop executables or scripts to help keep individual job scripts clean and concise. A simple example might be `/var/lib/laminar/cfg/scripts/success_trigger`:

```bash
#!/bin/bash -e
if [ "$RESULT" == "success" ]; then
  laminarc queue "$@"
fi
```

With this in place, any `.after` script can conditionally trigger a downstream job more succinctly:

```bash
success_trigger example-test
```

Another excellent candidate for helper scripts is automatically sending notifications on job status change.

---

# Data sharing and Workspaces

Often, a job will require a (relatively) large block of (relatively) unchanging data. Examples are a git repository with a long history, or static asset files. Instead of fetching everything from scratch for every run, a job may make use a *workspace*, a per-job folder that is reused between builds.

For example, the following script creates a tarball containing both compiled output and some static asset files from the workspace:

```bash
#!/bin/bash -ex
git clone /path/to/sources .
make
# Use a hardlink so the arguments to tar will be relative to the CWD
ln $WORKSPACE/StaticAsset.bin ./
tar zc a.out StaticAsset.bin > MyProject.tar.gz
# Archive the artefact (consider moving this to the .after script)
mv MyProject.tar.gz $ARCHIVE/
```

For a project with a large git history, it can be more efficient to store the sources in the workspace:

```bash
#!/bin/bash -ex
cd $WORKSPACE/myproject
git pull
cd -

cmake $WORKSPACE/myproject
make -j4
```

Laminar will automatically create the workspace for a job if it doesn't exist when a job is executed. In this case, the `/var/lib/laminar/cfg/jobs/JOBNAME.init` will be executed if it exists. This is an excellent place to prepare the workspace to a state where subsequent builds can rely on its content:

```bash
#!/bin/bash -e
echo Initializing workspace
git clone git@example.com:company/project.git .
```

**CAUTION**: By default, laminar permits multiple simultaneous runs of the same job. If a job can **modify** the workspace, this might result in inconsistent builds when simultaneous runs access the same content. This is unlikely to be an issue for nightly builds, but for SCM-triggered builds it will be. To solve this, use [nodes](#Nodes-and-Tags) to restrict simultaneous execution of jobs, or consider [flock](https://linux.die.net/man/1/flock).

The following example uses [flock](https://linux.die.net/man/1/flock) to efficiently share a git repository workspace between multiple simultaneous builds:

```bash
#!/bin/bash -xe

# This script expects to be passed the parameter 'rev' which
# should refer to a specific git commit in its source repository.
# The commit ids could have been read from a server-side
# post-commit git hook, where many commits could have been pushed
# at once, but we want to check them all individually. This means
# this job can be executed several times (with different values
# for $rev) simultaneously.

# Locked subshell for modifying the workspace
(
  flock 200
  cd $WORKSPACE
  # Download all the latest commits
  git fetch
  git checkout $rev
  cd -
  # Fast copy (hard-link) the source from the specific checkout
  # to the build dir. This relies on the fact that git unlinks
  # during checkout, effectively implementing copy-on-write.
  cp -al $WORKSPACE/src src
) 200>$WORKSPACE

# run the (much longer) regular build process
make -C src
```

---

# Aborting running jobs

## After a timeout

To configure a maximum execution time in seconds for a job, add a line to `/var/lib/laminar/cfg/jobs/JOBNAME.conf`:

```
TIMEOUT=120
```

## Manually

`laminarc abort $JOBNAME $NUMBER`

---

# Nodes and Tags

In Laminar, a *node* is an abstract concept allowing more fine-grained control over job execution scheduling. Each node can be defined to support an integer number of *executors*, which defines how many runs can be executed simultaneously.

A typical example would be to allow only a few concurrent CPU-intensive jobs (such as compilation), while simultaneously allowing many more less-intensive jobs (such as monitoring or remote jobs). To create a node named `build` with 3 executors, create the file `/var/lib/laminar/cfg/nodes/build.conf` with the following content:

```
EXECUTORS=3
```

To associate jobs with nodes, laminar uses *tags*. Tags may be applied to nodes and jobs. If a node has tags, only jobs with a matching tag will be executed on it. If a node has no tags, it will accept any job. To tag a node, add them to `/var/lib/laminar/cfg/nodes/NODENAME.conf`:

```
EXECUTORS=3
TAGS=tag1,tag2
```

To add a tag to a job, add the following to `/var/lib/laminar/cfg/jobs/JOBNAME.conf`:

```
TAGS=tag2
```

If Laminar cannot find any node configuration, it will assume a single node with 6 executors and no tags.

## Grouping jobs with tags

Tags are also used to group jobs in the web UI. Each tag will presented as a tab in the "Jobs" page.

## Node scripts

If `/var/lib/laminar/cfg/nodes/NODENAME.before` exists, it will be executed before the run script of a job scheduled to that node. Similarly, if `/var/lib/laminar/cfg/nodes/NODENAME.after` exists, it will be executed after the run script of a job scheduled to that node.

## Node environment

If `/var/lib/laminar/cfg/nodes/NODENAME.env` exists and can be parsed as a list of `KEY=VALUE` pairs, these variables will be exposed as part of the run's environment.

# Remote jobs

Laminar provides no specific support, `bash`, `ssh` and possibly NFS are all you need. For example, consider two identical target devices on which test jobs can be run in parallel. You might create a [node](#Nodes-and-Tags) for each, `/var/lib/laminar/cfg/nodes/target{1,2}.conf` with a common tag:

```
EXECUTORS=1
TAGS=remote-target
```

In each node's `.env` file, set the individual device's IP address:

```
TARGET_IP=192.168.0.123
```

And tag the job accordingly in `/var/lib/laminar/cfg/jobs/myproject-test.conf`:

```
TAGS=remote-target
```

This means the job script `/var/lib/laminar/cfg/jobs/myproject-test.run` can be generic:

```bash
#!/bin/bash -e

ssh root@$TARGET_IP /bin/bash -xe <<"EOF"
  uname -a
  ...
EOF
scp root@$TARGET_IP:result.xml "$ARCHIVE/"
```

Don't forget to add the `laminar` user's public ssh key to the remote's `authorized_keys`.

---

# Docker container jobs

Laminar provides no specific support, but just like [remote jobs](#Remote-jobs) these are easily implementable in plain bash:

```bash
#!/bin/bash

docker run --rm -ti -v $PWD:/root ubuntu /bin/bash -xe <<EOF
  git clone http://...
  ...
EOF
```

---

# Customizing the WebUI

If it exists, the file `/var/lib/laminar/custom/style.css` will be served by laminar and may be used to change the appearance of Laminar's WebUI.

This directory is also a good place to add any extra assets needed for this customization, but note that in this case you will need to serve this directory directly from your [HTTP reverse proxy](#Service-configuration) (highly recommended).

---

# Badges

Laminar will serve a job's current status as a pretty badge at the url `/badge/JOBNAME.svg`. This can be used as a link to your server instance from your Github README.md file or cat blog:

```
<a href="https://my-example-laminar-server.com/jobs/my-project">
  <img src="https://my-example-laminar-server.com/badge/my-project.svg">
</a>
```

---

# Reference

## Service configuration file

`laminard` reads the following variables from the environment, which are expected to be sourced by `systemd` from `/etc/laminar.conf`:

- `LAMINAR_HOME`: The directory in which `laminard` should find job configuration and create run directories. Default `/var/lib/laminar`
- `LAMINAR_BIND_HTTP`: The interface/port or unix socket on which `laminard` should listen for incoming connections to the web frontend. Default `*:8080`
- `LAMINAR_BIND_RPC`: The interface/port or unix socket on which `laminard` should listen for incoming commands such as build triggers. Default `unix-abstract:laminar`
- `LAMINAR_TITLE`: The page title to show in the web frontend.
- `LAMINAR_KEEP_RUNDIRS`: Set to an integer defining how many rundirs to keep per job. The lowest-numbered ones will be deleted. The default is 0, meaning all run dirs will be immediately deleted.
- `LAMINAR_ARCHIVE_URL`: If set, the web frontend served by `laminard` will use this URL to form links to artefacts archived jobs. Must be synchronized with web server configuration.

## Script execution order

When `$JOB` is triggered on `$NODE`, the following scripts (relative to `$LAMINAR_HOME/cfg`) may be executed:

- `jobs/$JOB.init` if the [workspace](#Data-sharing-and-Workspaces) did not exist
- `before`
- `nodes/$NODE.before`
- `jobs/$JOB.before`
- `jobs/$JOB.run`
- `jobs/$JOB.after`
- `nodes/$NODE.after`
- `after`

## Environment variables

The following variables are available in run scripts:

- `RUN` integer number of this *run*
- `JOB` string name of this *job*
- `RESULT` string run status: "success", "failed", etc.
- `LAST_RESULT` string previous run status
- `WORKSPACE` path to this job's workspace
- `ARCHIVE` path to this run's archive

In addition, `$LAMINAR_HOME/cfg/scripts` is prepended to `$PATH`. See [helper scripts](#Helper-scripts).

Laminar will also export variables in the form `KEY=VALUE` found in these files:

- `env`
- `nodes/$NODE.env`
- `jobs/$JOB.env`

Finally, variables supplied on the command-line call to `laminarc queue`, `laminarc start` or `laminarc run` will be available. See [parameterized runs](#Parameterized-runs)

## laminarc

`laminarc` commands are:

- `queue [JOB [PARAMS...]]...` adds one or more jobs to the queue with optional parameters, returning immediately.
- `start [JOB [PARAMS...]]...` starts one or more jobs with optional parameters, returning when the jobs begin execution.
- `run [JOB [PARAMS...]]...` triggers one or more jobs with optional parameters and waits for the completion of all jobs.
- `set [VARIABLE=VALUE]...` sets one or more variables to be exported in subsequent scripts for the run identified by the `$JOB` and `$RUN` environment variables
- `show-jobs` shows the known jobs on the server (`$LAMINAR_HOME/cfg/jobs/*.run`).
- `show-running` shows the currently running jobs with their numbers.
- `show-queued` shows the names of the jobs waiting in the queue.
- `abort JOB NUMBER` manually aborts a currently running job by name and number.

`laminarc` connects to `laminard` using the address supplied by the `LAMINAR_HOST` environment variable. If it is not set, `laminarc` will first attempt to use `LAMINAR_BIND_RPC`, which will be available if `laminarc` is executed from a script within `laminard`. If neither `LAMINAR_HOST` nor `LAMINAR_BIND_RPC` is set, `laminarc` will assume a default host of `unix-abstract:laminar`.

All commands return zero on success or a non-zero code if the command could not be executed. `laminarc run` will return a non-zero exit status if any executed job failed.
