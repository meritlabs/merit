Merit Core   
=====================================

### Merit aims to be the world's most adopted digital currency. 
You can learn more about the Merit Vision at: www.merit.me

What is Merit?
----------------

Merit is a digital currency that focuses on community, simplicity, and safety. It introduces significant innovation to previous digital currencies.  Namely, it is: 
* Merit is **invite-only**. 
* Merit introduces the notion of **ambassador mining** in addition to classic _security mining._
* Merit features **decentralized vaults** on the blockchain to protect and provide peace-of-mind. 
* Merit can be sent to **anyone** whether they have a Merit Wallet yet or not.

Merit Core is a distributed peer-to-peer server that maintains a ledger using a global blockchain. For the convenience of all users, especially non-technical ones, you can also use Merit Core to mine on your machine with a couple of clicks.


Community Resources
------------------

Merit aims to be present wherever users of the community are.  To that end, you can find members of the core team on:

![Slack Badge](https://slackin.merit.me/badge.svg) 
* [Slack](https://slackin.merit.me) 
* [Telegram](https://t.me/meritworld)
* [Discord](https://discord.gg/X3v3n3b)
* [Merit Forum](https://forum.merit.me/) @ www.merit.me
* Merit [Mailing List](https://groups.google.com/forum/#!forum/meritlabs)
* IRC: Channel #merit on Freenode. [Webchat Here](http://webchat.freenode.net/?channels=merit).

Getting Started
-------------------

### Get the Latest Release

You can find the latest releases of Merit [here](https://github.com/meritlabs/merit/releases).

### Build from Source

Clone this repo.

    git clone https://github.com/meritlabs/merit.git

Merit Core uses Autotools and can be built on Linux, Mac OS, and any many other
UNIX systems. You will need a C++ compiler that supports the C++11 standard
such as any recent GCC or Clang version.

    ./autogen.sh
    ./configure
    cd src/
    make obj/build.h
    cd ../
    make 
    make install


Start Mining
---------------

Before you can use Merit and mine you must unlock your wallet by giving it a parent key. 
Merit is currently invite-only and typically the parent key is owned by someone
who is willing to invite you to merit. Once you unlock the wallet you will have
to wait for the other person to confirm the wallet before you can mine.

Note, whoever invites will get a boost in their Aggregate Network Value (ANV)
via your mining efforts and therefore your mining efforts help them earn mining rewards.

Before you can unlock your wallet you must start the **meritd** daemon.

    meritd --daemon

Meritd will then start up and automatically connect to the main network and start
syncing the blockchain.

You can then unlock your wallet by running the **unlockwallet** command.

    merit-cli unlockwallet <address or alias of person inviting you> <optional global alias>

This will setup your wallet and notify the network. It will return your
wallet information including your primary address. Share this address with
others so they can help boost your ANV.

To start mining you simply run...

    merit-cli setmining true

If you want to stop mining, you run...

    merit-cli setmining false

Developer Resources
-------------------
Take a look at our [docs](doc/README.md)

License
-------

Merit Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.


