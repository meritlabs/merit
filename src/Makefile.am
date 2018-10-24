# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS) $(MINGW_LDFLAGS)
AM_CXXFLAGS = $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if EMBEDDED_UNIVALUE
LIBUNIVALUE = univalue/libunivalue.la

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)
else
LIBUNIVALUE = $(UNIVALUE_LIBS)
endif

MERIT_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

MERIT_INCLUDES += -I$(srcdir)/secp256k1/include
MERIT_INCLUDES += $(UNIVALUE_CFLAGS)

LIBMERIT_SERVER=libmerit_server.a
LIBMERIT_COMMON=libmerit_common.a
LIBMERIT_CONSENSUS=libmerit_consensus.a
LIBMERIT_CLI=libmerit_cli.a
LIBMERIT_UTIL=libmerit_util.a
LIBMERIT_CRYPTO=crypto/libmerit_crypto.a
LIBMERITQT=qt/libmeritqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBMERIT_ZMQ=libmerit_zmq.a
endif
if BUILD_MERIT_LIBS
LIBMERITCONSENSUS=libmeritconsensus.la
LIBMERITCRYPTO=libmeritcrypto.la
LIBMERITUTIL=libmeritutil.la
endif
if ENABLE_WALLET
LIBMERIT_WALLET=libmerit_wallet.a
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBMERIT_CRYPTO) \
  $(LIBMERIT_UTIL) \
  $(LIBMERIT_COMMON) \
  $(LIBMERIT_CONSENSUS) \
  $(LIBMERIT_SERVER) \
  $(LIBMERIT_CONSENSUS) \
  $(LIBMERIT_CLI) \
  $(LIBMERIT_WALLET) \
  $(LIBMERIT_CONSENSUS) \
  $(LIBMERIT_ZMQ)

lib_LTLIBRARIES = $(LIBMERITCRYPTO) $(LIBMERITUTIL) $(LIBMERITCONSENSUS)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_MERITD
  bin_PROGRAMS += meritd
endif

if BUILD_MERIT_UTILS
  bin_PROGRAMS += merit-cli merit-tx
endif

.PHONY: FORCE check-symbols check-security
# merit core #
MERIT_CORE_H = \
  addrdb.h \
  addressindex.h \
  addrman.h \
  base58.h \
  blockencodings.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coins.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/tx_verify.h \
  consensus/ref_verify.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  indirectmap.h \
  init.h \
  key.h \
  keystore.h \
  limitedmap.h \
  memusage.h \
  merkleblock.h \
  miner.h \
  cuckoo/cuckoo.h \
  cuckoo/miner.h \
  cuckoo/mean_cuckoo.h \
  mempool.h \
  net.h \
  net_processing.h \
  netaddress.h \
  netbase.h \
  netmessagemaker.h \
  noui.h \
  policy/feerate.h \
  policy/fees.h \
  policy/policy.h \
  policy/rbf.h \
  pow.h \
  pog/anv.h \
  pog/select.h \
  pog/wrs.h \
  pog/invitebuffer.h \
  pog/reward.h \
  pog2/cgs.h \
  pog2/select.h \
  pog2/reward.h \
  pog3/cgs.h \
  pog3/select.h \
  pog3/reward.h \
  protocol.h \
  random.h \
  refdb.h \
  referrals.h \
  reverse_iterator.h \
  reverselock.h \
  rpc/blockchain.h \
  spentindex.h \
  timestampindex.h \
  refmempool.h \
  rpc/client.h \
  rpc/mining.h \
  rpc/protocol.h \
  rpc/safemode.h \
  rpc/server.h \
  rpc/register.h \
  scheduler.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  script/ismine.h \
  streams.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  threadsafety.h \
  threadinterrupt.h \
  timedata.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  undo.h \
  util.h \
  utilmoneystr.h \
  utiltime.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  wallet/coincontrol.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/feebumper.h \
  wallet/fees.h \
  wallet/init.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/vault.h \
  wallet/walletdb.h \
  warnings.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h


obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libmerit_util_a-clientversion.$(OBJEXT): obj/build.h

# server: shared between meritd and merit-qt
libmerit_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libmerit_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_server_a_SOURCES = \
  addrdb.cpp \
  addrman.cpp \
  blockencodings.cpp \
  bloom.cpp \
  chain.cpp \
  checkpoints.cpp \
  consensus/ref_verify.cpp \
  consensus/tx_verify.cpp \
  dbwrapper.cpp \
  httprpc.cpp \
  httpserver.cpp \
  init.cpp \
  merkleblock.cpp \
  miner.cpp \
  cuckoo/cuckoo.cpp \
  cuckoo/miner.cpp \
  cuckoo/mean_cuckoo.cpp \
  net.cpp \
  net_processing.cpp \
  noui.cpp \
  pog/anv.cpp \
  pog/reward.cpp \
  pog/select.cpp \
  pog/wrs.cpp \
  pog/invitebuffer.cpp \
  pog2/cgs.cpp \
  pog2/reward.cpp \
  pog2/select.cpp \
  pog3/cgs.cpp \
  pog3/reward.cpp \
  pog3/select.cpp \
  policy/fees.cpp \
  policy/policy.cpp \
  policy/rbf.cpp \
  pow.cpp \
  refdb.cpp \
  refmempool.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/rawreferral.cpp \
  rpc/safemode.cpp \
  rpc/server.cpp \
  script/ismine.cpp \
  script/sigcache.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  ui_interface.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  $(MERIT_CORE_H)

if ENABLE_ZMQ
libmerit_zmq_a_CPPFLAGS = $(MERIT_INCLUDES) $(ZMQ_CFLAGS)
libmerit_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif


# wallet: shared between meritd and merit-qt, but only linked
# when wallet enabled
libmerit_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
libmerit_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_wallet_a_SOURCES = \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/feebumper.cpp \
  wallet/fees.cpp \
  wallet/init.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/vault.cpp \
  wallet/walletdb.cpp \
  $(MERIT_CORE_H)

# crypto primitives library
crypto_libmerit_crypto_a_CPPFLAGS = $(AM_CPPFLAGS) $(PIC_FLAGS)
crypto_libmerit_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libmerit_crypto_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/mnemonic/dictionary.cpp \
  crypto/mnemonic/dictionary.h \
  crypto/mnemonic/mnemonic.cpp \
  crypto/mnemonic/mnemonic.h \
  crypto/pkcs5_pbkdf2.cpp \
  crypto/pkcs5_pbkdf2.h \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h \
  crypto/blake2/blake2b-ref.c \
  crypto/blake2/blake2.h \
  crypto/blake2/blake2-impl.h \
  crypto/siphashxN.h

if USE_ASM
crypto_libmerit_crypto_a_SOURCES += crypto/sha256_sse4.cpp
endif

# consensus: shared between all executables that validate any consensus rules.
libmerit_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
libmerit_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_consensus_a_SOURCES = \
  amount.h \
  arith_uint256.cpp \
  arith_uint256.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/validation.h \
  hash.cpp \
  hash.h \
  prevector.h \
  primitives/block.cpp \
  primitives/block.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  primitives/referral.cpp \
  primitives/referral.h \
  pubkey.cpp \
  pubkey.h \
  script/meritconsensus.cpp \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  script/standard.cpp \
  script/standard.h \
  serialize.h \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  utilstrencodings.cpp \
  utilstrencodings.h

# common: shared between meritd, and merit-qt and non-server tools
libmerit_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
libmerit_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_common_a_SOURCES = \
  base58.cpp \
  chainparams.cpp \
  coins.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  key.cpp \
  keystore.cpp \
  netaddress.cpp \
  netbase.cpp \
  policy/feerate.cpp \
  protocol.cpp \
  referrals.cpp \
  scheduler.cpp \
  script/sign.cpp \
  script/standard.cpp \
  warnings.cpp \
  $(MERIT_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libmerit_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
libmerit_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_util_a_SOURCES = \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  fs.cpp \
  random.cpp \
  rpc/protocol.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  util.cpp \
  utilmoneystr.cpp \
  utilstrencodings.cpp \
  utiltime.cpp \
  $(MERIT_CORE_H)

if GLIBC_BACK_COMPAT
libmerit_util_a_SOURCES += compat/glibc_compat.cpp
endif

# cli: shared between merit-cli and merit-qt
libmerit_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
libmerit_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libmerit_cli_a_SOURCES = \
  rpc/client.cpp \
  $(MERIT_CORE_H)

nodist_libmerit_util_a_SOURCES = $(srcdir)/obj/build.h
#

# meritd binary #
meritd_SOURCES = meritd.cpp
meritd_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
meritd_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
meritd_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
meritd_SOURCES += meritd-res.rc
endif

meritd_LDADD = \
  $(LIBMERIT_SERVER) \
  $(LIBMERIT_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBMERIT_UTIL) \
  $(LIBMERIT_WALLET) \
  $(LIBMERIT_ZMQ) \
  $(LIBMERIT_CONSENSUS) \
  $(LIBMERIT_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

meritd_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS)

# merit-cli binary #
merit_cli_SOURCES = merit-cli.cpp
merit_cli_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES) $(EVENT_CFLAGS)
merit_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
merit_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
merit_cli_SOURCES += merit-cli-res.rc
endif

merit_cli_LDADD = \
  $(LIBMERIT_CLI) \
  $(LIBUNIVALUE) \
  $(LIBMERIT_UTIL) \
  $(LIBMERIT_CONSENSUS) \
  $(LIBMERIT_CRYPTO)

merit_cli_LDADD += $(BOOST_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(EVENT_LIBS)
#

# merit-tx binary #
merit_tx_SOURCES = merit-tx.cpp
merit_tx_CPPFLAGS = $(AM_CPPFLAGS) $(MERIT_INCLUDES)
merit_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
merit_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
merit_tx_SOURCES += merit-tx-res.rc
endif

merit_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBMERIT_COMMON) \
  $(LIBMERIT_SERVER) \
  $(LIBMERIT_UTIL) \
  $(LIBMERIT_CONSENSUS) \
  $(LIBMERIT_CRYPTO) \
  $(LIBSECP256K1)

merit_tx_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS)
#

# meritconsensus library #
if BUILD_MERIT_LIBS
include_HEADERS = script/meritconsensus.h
libmeritcrypto_la_SOURCES = $(crypto_libmerit_crypto_a_SOURCES)
libmeritcrypto_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libmeritcrypto_la_LIBADD = $(LIBUNIVALUE) $(CRYPTO_LIBS) $(SSL_LIBS) $(BOOST_LIBS)
libmeritcrypto_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj $(UNIVALUE_CFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)
libmeritcrypto_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)


libmeritutil_la_SOURCES = $(libmerit_util_a_SOURCES)
libmeritutil_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libmeritutil_la_LIBADD = $(LIBMERITCRYPTO) $(LIBUNIVALUE) $(CRYPTO_LIBS) $(SSL_LIBS) $(BOOST_LIBS)
libmeritutil_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj $(UNIVALUE_CFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)
libmeritutil_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

libmeritconsensus_la_SOURCES = $(libmerit_consensus_a_SOURCES)

if GLIBC_BACK_COMPAT
  libmeritconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libmeritconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libmeritconsensus_la_LIBADD = $(LIBSECP256K1) $(LIBMERITCRYPTO) $(LIBMERITUTIL) $(BOOST_LIBS)
libmeritconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_MERIT_INTERNAL
libmeritconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno

DISTCLEANFILES = obj/build.h

EXTRA_DIST = $(CTAES_DIST)


config/merit-config.h: config/stamp-h1
	@$(MAKE) -C $(top_builddir) $(subdir)/$(@)
config/stamp-h1: $(top_srcdir)/$(subdir)/config/merit-config.h.in $(top_builddir)/config.status
	$(AM_V_at)$(MAKE) -C $(top_builddir) $(subdir)/$(@)
$(top_srcdir)/$(subdir)/config/merit-config.h.in:  $(am__configure_deps)
	$(AM_V_at)$(MAKE) -C $(top_srcdir) $(subdir)/config/merit-config.h.in

clean-local:
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C univalue clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(QT_INCLUDES) $(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) -c -o $@ $<

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py < $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py < $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<

if EMBEDDED_LEVELDB
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif

if ENABLE_QT
include Makefile.qt.include
endif

if ENABLE_QT_TESTS
include Makefile.qttest.include
endif
