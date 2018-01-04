Merit
=============

Setup
---------------------
Merit's mobile wallet is the easiest way to get started.
To download the mobile wallet, visit [merit.me](http://www.merit.me/get-started/index.html#DownloadMobile).

### Need Help?

* See the documentation at the [Merit Wiki](https://github.com/meritlabs/merit/wiki)
for help and more information.
* Ask for help on [#merit](http://webchat.freenode.net?channels=merit) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net?channels=merit).

Building
---------------------
The following are developer notes on how to build Merit on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OS X Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Running
---------------------

### Unix

To start the Merit daemon, run
- `src/meritd`

Development
---------------------
The Merit repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/merit/doxygen/)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss project-specific development on #merit-core-dev on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net/?channels=merit-core-dev).
* Discuss general Merit development on #merit-dev on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net/?channels=merit-dev).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
