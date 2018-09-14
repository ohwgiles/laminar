# Laminar CI

Laminar (http://laminar.ohwg.net) is a lightweight and modular Continuous Integration service for Linux. It is self-hosted and developer-friendly, eschewing a configuration UI in favor of simple version-controllable configuration files and scripts.

Laminar encourages the use of existing GNU/Linux tools such as `bash` and `cron` instead of reinventing them.

Although the status and progress front-end is very user-friendly, administering a Laminar instance requires writing shell scripts and manually editing configuration files. That being said, there is nothing esoteric here and the [guide](http://laminar.ohwg.net/docs.html) should be straightforward for anyone with even very basic Linux server administration experience.

See [the website](http://laminar.ohwg.net) and the [documentation](http://laminar.ohwg.net/docs.html) for more information.

## Building from source

First install development packages for `capnproto (0.7.0)`, `rapidjson`, `sqlite` and `boost-filesystem` from your distribution's repository or other source. Then:

```bash
git clone https://github.com/ohwgiles/laminar.git
cd laminar
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/
make -j4
sudo make install
```

`make install` includes a systemd unit file. If you intend to use it, consider creating a new user `laminar` or modifying the user specified in the unit file.

