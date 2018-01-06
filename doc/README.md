Merit
=============

Setup
---------------------

For the moment setting up of merit core requires obtaining code, compiling and installing it.
Binary distribution will be released soon.

To compile and run merit follow instructions linked in #Building section of the doc.

### Need Help?

* See the documentation at the [Merit Wiki](https://github.com/meritlabs/merit/wiki)
for help and more information.
* Ask for help on [#merit](http://webchat.freenode.net?channels=merit) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net?channels=merit).

Building
---------------------
The following are developer notes on how to build Merit on your native platform.
They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

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

Test Net
-------------------
You should create a long RPCpassword as instructed here: https://github.com/meritlabs/merit/blob/master/doc/build-osx.md#running

You will want a merit.conf that looks like. You can find the merit.conf file under $HOME/.merit/merit.conf

```
rpcuser=meritrpc
rpcpassword=<randomLongPassword>

#--- Network
addnode=13.90.86.37
addnode=13.90.85.234
addnode=13.82.88.148

testnet=1
server=1
rpcapplowip=127.0.0.1
whitelist=127.0.0.1

zmqpubrawtx=tcp://127.0.0.1:28332
zmqpubhashblock=tcp://127.0.0.1:28332
zmqpubrawblock=tcp://127.0.0.1:28332
zmqpubhashtx=tcp://127.0.0.1:28332
```

Development
---------------------

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)
- [Legacy Bitcoin Improvement Proposals](bips.md)

### Resources
* Discuss general aspects of Merit on #merit on Freenode. [webchat here](http://webchat.freenode.net/?channels=merit).
* Discuss general Merit development on #merit-dev on Freenode. [webchat here](http://webchat.freenode.net/?channels=merit-dev).

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
