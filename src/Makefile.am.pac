
if CLI_MODE
bin_PROGRAMS = SorachanCoin_cli
else
bin_PROGRAMS = SorachanCoind
endif

INCLUDES = -I../library/boost_1_68_0/include -I../library/libressl-2.8.2/include -I../library/sqlite/include
SorachanCoind_CXXFLAGS = -std=c++11 -DUSE_QUANTUM -DWALLET_SQL_MODE -DBLK_SQL_MODE -DUSE_IPV6 -DBOOST_NO_CXX11_SCOPED_ENUMS -w -Wno-delete-incomplete -Wno-deprecated-declarations -Wno-placement-new
SorachanCoin_cli_CXXFLAGS = -std=c++11 -DUSE_QUANTUM -DWALLET_SQL_MODE -DBLK_SQL_MODE -DUSE_IPV6 -DBOOST_NO_CXX11_SCOPED_ENUMS -w -Wno-delete-incomplete -Wno-deprecated-declarations -Wno-placement-new
if CLI_MODE
SorachanCoin_cli_CXXFLAGS += -DCLI_MODE_ENABLE
endif

SorachanCoind_LDADD = -lpthread \
 ../library/boost_1_68_0/lib/libboost_system.a \
 ../library/boost_1_68_0/lib/libboost_filesystem.a \
 ../library/boost_1_68_0/lib/libboost_program_options.a \
 ../library/boost_1_68_0/lib/libboost_thread.a \
 ../library/boost_1_68_0/lib/libboost_chrono.a \
 ../library/libressl-2.8.2/lib/libssl.a \
 ../library/libressl-2.8.2/lib/libcrypto.a \
 ../library/sqlite/lib/libsqlite3.a \
 -lz \
 -ldl \

SorachanCoin_cli_LDADD = -lpthread \
 ../library/boost_1_68_0/lib/libboost_system.a \
 ../library/boost_1_68_0/lib/libboost_filesystem.a \
 ../library/boost_1_68_0/lib/libboost_program_options.a \
 ../library/boost_1_68_0/lib/libboost_thread.a \
 ../library/boost_1_68_0/lib/libboost_chrono.a \
 ../library/libressl-2.8.2/lib/libssl.a \
 ../library/libressl-2.8.2/lib/libcrypto.a \
 ../library/sqlite/lib/libsqlite3.a \
 -lz \
 -ldl \

SorachanCoind_SOURCES = \
 addrman.cpp \
 alert.cpp \
 base58.cpp \
 bitcoinrpc.cpp \
 checkpoints.cpp \
 crypter.cpp \
 cryptogram.cpp \
 db.cpp \
 ecies.cpp \
 init.cpp \
 ipcollector.cpp \
 irc.cpp \
 kernel.cpp \
 kernel_worker.cpp \
 kernelrecord.cpp \
 key.cpp \
 keystore.cpp \
 main.cpp \
 miner.cpp \
 net.cpp \
 netbase.cpp \
 noui.cpp \
 ntp.cpp \
 pbkdf2.cpp \
 protocol.cpp \
 rpcblockchain.cpp \
 rpccrypt.cpp \
 rpcdump.cpp \
 rpcmining.cpp \
 rpcnet.cpp \
 rpcrawtransaction.cpp \
 rpcwallet.cpp \
 script.cpp \
 scrypt.cpp \
 stun.cpp \
 sync.cpp \
 txdb-leveldb.cpp \
 util.cpp \
 version.cpp \
 wallet.cpp \
 walletdb.cpp

SorachanCoin_cli_SOURCES = \
 addrman.cpp \
 alert.cpp \
 base58.cpp \
 bitcoinrpc.cpp \
 checkpoints.cpp \
 crypter.cpp \
 cryptogram.cpp \
 db.cpp \
 ecies.cpp \
 init.cpp \
 ipcollector.cpp \
 irc.cpp \
 kernel.cpp \
 kernel_worker.cpp \
 kernelrecord.cpp \
 key.cpp \
 keystore.cpp \
 main.cpp \
 miner.cpp \
 net.cpp \
 netbase.cpp \
 noui.cpp \
 ntp.cpp \
 pbkdf2.cpp \
 protocol.cpp \
 rpcblockchain.cpp \
 rpccrypt.cpp \
 rpcdump.cpp \
 rpcmining.cpp \
 rpcnet.cpp \
 rpcrawtransaction.cpp \
 rpcwallet.cpp \
 script.cpp \
 scrypt.cpp \
 stun.cpp \
 sync.cpp \
 txdb-leveldb.cpp \
 util.cpp \
 version.cpp \
 wallet.cpp \
 walletdb.cpp
