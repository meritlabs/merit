Merit Core integration/staging tree
=====================================


https://merit.me

What is Merit?
----------------

Merit is an experimental digital currency that enables instant payments to
anyone, anywhere in the world. Merit uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. Merit Core is the name of open source
software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the Merit Core software, see https://Merit.org/en/download, or read the
[original whitepaper](https://Meritcore.org/Merit.pdf).

License
-------

Merit Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

TestNet
-------------------
You should create a long RPCpassword as instructed here: https://github.com/bitcoin/bitcoin/blob/master/doc/build-osx.md#running

You will want a bitcoin.conf that looks like:
```
rpcuser=bitcoinrpc
rpcpassword=<randomLongPassword>

#--- Network
addnode=13.90.86.37
addnode=13.90.85.234
addnode=13.82.88.148

regtest=1
txindex=1
addressindex=1
#reindex=1
spentindex=1
timestampindex=1
server=1
rpcapplowip=127.0.0.1
whitelist=127.0.0.1
#daemon=1

#zmqpubrawtx=tcp://127.0.0.1:28332
#zmqpubhashblock=tcp://127.0.0.1:28332
zmqpubrawtx=tcp://127.0.0.1:28332
zmqpubhashblock=tcp://127.0.0.1:28332
zmqpubrawblock=tcp://127.0.0.1:28332
zmqpubhashtx=tcp://127.0.0.1:28332
```