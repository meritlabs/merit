Merit Core
=====================================

www.merit.me

What is Merit?
----------------

Merit is a digital currency that focuses on community, easy of use, safety and
scalability. Merit Core is a distributed peer-to-peer server that maintains a
ledger using a global blockchain. You can also use Merit Core to mine on your
machine.

License
-------

Merit Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Community Resources
------------------

* Join the Merit [Mailing List](https://groups.google.com/forum/#!forum/meritlabs)
* Discuss Merit on IRC channel #merit on Freenode. [webchat here](http://webchat.freenode.net/?channels=merit).

Getting Started
-------------------

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
If you don't know anyone using Merit, you can use the Meritlab's address 

    ST2HYE5KMszAdBcGo3kw7Qsb9u1nRQhac4

Note, whoever you choose as your referrer, they will get a boost in their
Aggregate Network Value (ANV) via your mining efforts and therefore your mining
efforts help them earn mining rewards.

Before you can unlock your wallet you must start the **meritd** daemon.

    meritd --daemon

Meritd will then start up and automatically connect to the main network and start
syncing the blockchain.

You can then unlock your wallet by running the **unlockwallet** command.

    merit-cli unlockwallet ST2HYE5KMszAdBcGo3kw7Qsb9u1nRQhac4

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

