
Welcome to SORA L1 Blockchain - Core lightweight version<br>
A hybrid scrypt PoW/PoS based cryptocurrency

![SorachanCoin](https://raw.githubusercontent.com/FromHDDtoSSD/SorachanCoin-qt/master/src/qt/res/images/splash2.png)

What is SORA L1 L2 Blockchain?
===========================
SORA L1 Blockchain - Core wallet official sites:<br>
https://www.junkhdd.com/<br>
https://sora.junkhdd.com/<br>
https://de.junkhdd.com/<br>
https://us.junkhdd.com/

Bitcointalk:<br>
https://bitcointalk.org/index.php?topic=5184483

Discord:<br>
https://discord.gg/ThMeemM

X:<br>
https://x.com/DataRescueCoin

How to build?
===========================
./sora_builder.sh<br>
generated under ./src, "soracoind" "soracoin-cli"

**Automatically and statically links the necessary libraries.<br>
therefore, by simply executing sora_builder.sh, <br>
"soracoind" and "soracoin-cli" will be reliably built.**

Development tools necessary for building
===========================
CentOS / AlmaLinux<br>
dnf groupinstall "Development Tools"<br>
dnf install git<br>
dnf install wget

Ubuntu / Debian<br>
sudo apt install build-essential<br>
sudo apt install git<br>
sudo apt install zlib1g-dev<br>
sudo apt install autoconf automake

soracoin-cli
===========================
e.g.<br>
soracoin-cli help<br>
soracoin-cli getinfo<br>
soracoin-cli listaccounts<br>
soracoin-cli getnewaddress ""<br>
soracoin-cli getblocktemplate '{"mode":"template"}'<br>
soracoin-cli sendfrom "user" SkNtsZ8CuYAbFKHGoNqDXvPQYF9WDa4h7W 30

e.g. using -datadir<br>
soracoin-cli -datadir=/coins/sora getinfo

e.g. using -testnet<br>
soracoin-cli -testnet getinfo

Block Explorer
===========================
https://us.junkhdd.com:7350/<br>
https://de.junkhdd.com:7350/<br>
https://www.junkhdd.com:7350/
