# Laminar CI [![status](https://ci.ohwg.net/badge/laminar.svg)](https://ci.ohwg.net/jobs/laminar)

Laminar (https://laminar.ohwg.net) is a lightweight and modular Continuous Integration service for Linux. It is self-hosted and developer-friendly, eschewing a configuration UI in favour of simple version-controllable configuration files and scripts.

Laminar encourages the use of existing GNU/Linux tools such as `bash` and `cron` instead of reinventing them.

Although the status and progress front-end is very user-friendly, administering a Laminar instance requires writing shell scripts and manually editing configuration files. That being said, there is nothing esoteric here and the [guide](http://laminar.ohwg.net/docs.html) should be straightforward for anyone with even very basic Linux server administration experience.

See [the website](https://laminar.ohwg.net) and the [documentation](https://laminar.ohwg.net/docs.html) for more information.

## Building from source

First install development packages for `capnproto (version 0.7.0 or newer)`, `rapidjson`, `sqlite` and `boost` (for the header-only `multi_index_container` library) from your distribution's repository or other source.

On Debian Buster, this can be done with:

```bash
sudo apt install \
		 capnproto cmake g++ libboost-dev libcapnp-dev libsqlite-dev libsqlite3-dev make rapidjson-dev zlib1g-dev
```

Then compile and install laminar with:

```bash
git clone https://github.com/ohwgiles/laminar.git
cd laminar
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j4
# Warning: the following will overwrite an existing /etc/laminar.conf
sudo make install
```

`make install` includes a systemd unit file. If you intend to use it, consider creating a new user `laminar` or modifying the user specified in the unit file.

## Packaging for distributions

The `pkg` directory contains shell scripts which use docker to build native packages (deb,rpm) for common Linux distributions. Note that these are very simple packages which may not completely conform to the distribution's packaging guidelines, however they may serve as a starting point for creating an official package, or may be useful if the official package lags.

## Contributing

Issues and pull requests via GitHub are most welcome. All pull requests must adhere to the [Developer Certificate of Origin](https://developercertificate.org/).
