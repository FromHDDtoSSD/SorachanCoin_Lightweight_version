// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "irc.h"
#include "db.h"
#include "net.h"
#include "init.h"
#include "addrman.h"
#include "ui_interface.h"
#include "miner.h"
#include "ntp.h"

#ifdef WIN32
#include <string.h>
#endif

#ifdef USE_UPNP
#include "miniwget.h"
#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"
#endif

CCriticalSection net_node::cs_vNodes;
CCriticalSection shot::cs_vOneShots;
std::deque<std::string> shot::vOneShots;
CCriticalSection net_node::cs_vAddedNodes;
std::vector<std::string> net_node::vAddedNodes;
CCriticalSection ext_ip::cs_mapLocalHost;
std::map<CNetAddr, ext_ip::LocalServiceInfo> ext_ip::mapLocalHost;
bool ext_ip::vfLimited[netbase::NET_MAX] = {};
bool ext_ip::vfReachable[netbase::NET_MAX] = {};
uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;
std::list<CNode *> net_basis::vNodesDisconnected;
std::map<CNetAddr, int64_t> CNode::setBanned;
CCriticalSection CNode::cs_setBanned;
CNode *net_node::pnodeLocalHost = NULL;
CNode *net_node::pnodeSync = NULL;
std::vector<SOCKET> bitsocket::vhListenSocket;
CAddress bitsocket::addrSeenByPeer(CService(net_basis::strIpZero, net_basis::nPortZero), net_node::nLocalServices);
uint64_t bitsocket::nLocalHostNonce = 0;
boost::array<int, THREAD_MAX> net_node::vnThreadsRunning;
CAddrMan net_node::addrman;
std::vector<CNode *> net_node::vNodes;
std::map<CInv, CDataStream> net_node::mapRelay;
CCriticalSection net_node::cs_mapRelay;
std::deque<std::pair<int64_t, CInv> > net_node::vRelayExpiration;
std::map<CInv, int64_t> net_node::mapAlreadyAskedFor;
std::set<CNetAddr> net_node::setservAddNodeAddresses;
CCriticalSection net_node::cs_setservAddNodeAddresses;
CSemaphore *shot::semOutbound = NULL;
uint64_t net_node::nLocalServices = (args_bool::fClient ? 0 : protocol::NODE_NETWORK);
const char *const net_basis::strIpZero = tcp_ip::strIpZero;
const char *const net_basis::strLocal = tcp_ip::strLocal;
const char *const net_basis::strSeedMaster = tcp_ip::strSeedMaster;
const char *const dns_seed::strDNSSeed[5][2] = {
    {tcp_domain::strMain, tcp_domain::strSub},
    {tcp_domain::dnsList[1], tcp_domain::dnsList[0]},
    {tcp_domain::dnsList[3], tcp_domain::dnsList[2]},
    {tcp_domain::dnsList[5], tcp_domain::dnsList[4]},
    {tcp_domain::dnsList[7], tcp_domain::dnsList[6]},
};
const uint32_t net_node::pnSeed[1] = {
    ::inet_addr(net_basis::strSeedMaster),
};
const char *const net_node::pchTorSeed[1] = {    // .onion
    ("dummy_" + coin_param::strCoinName + ".onion").c_str(),
};
bool_arg args_bool::fDiscover(true);
bool_arg args_bool::fUseUPnP(false);
bool_arg args_bool::fClient(false);

//
// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
//
#ifdef WIN32
 #ifndef PROTECTION_LEVEL_UNRESTRICTED
  #define PROTECTION_LEVEL_UNRESTRICTED 10
 #endif
 #ifndef IPV6_PROTECTION_LEVEL
  #define IPV6_PROTECTION_LEVEL 23
 #endif
#endif

unsigned short net_basis::GetListenPort()
{
    return (unsigned short)(map_arg::GetArg("-port", net_basis::GetDefaultPort()));
}

void CNode::PushGetBlocks(CBlockIndex *pindexBegin, uint256 hashEnd)
{
    //
    // Filter out duplicate requests
    //
    if (pindexBegin == pindexLastGetBlocksBegin && hashEnd == hashLastGetBlocksEnd) {
        return;
    }

    pindexLastGetBlocksBegin = pindexBegin;
    hashLastGetBlocksEnd = hashEnd;

    PushMessage("getblocks", CBlockLocator(pindexBegin), hashEnd);
}

//
// find 'best' local address for a particular peer
//
bool ext_ip::GetLocal(CService &addr, const CNetAddr *paddrPeer/* = NULL */)
{
    if (args_bool::fNoListen) {
        return false;
    }

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(ext_ip::cs_mapLocalHost);
        for (std::map<CNetAddr, ext_ip::LocalServiceInfo>::iterator it = ext_ip::mapLocalHost.begin(); it != ext_ip::mapLocalHost.end(); it++)
        {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore)) {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

// get best local address for a particular peer as a CAddress
CAddress ext_ip::GetLocalAddress(const CNetAddr *paddrPeer/* = NULL */)
{
    CAddress ret(CService(net_basis::strIpZero, net_basis::nPortZero), 0);

    CService addr;
    if (ext_ip::GetLocal(addr, paddrPeer)) {
        ret = CAddress(addr, net_node::nLocalServices, bitsystem::GetAdjustedTime());
        // ret.nServices = net_node::nLocalServices;
        // ret.nTime = bitsystem::GetAdjustedTime();
    }
    return ret;
}

bool net_basis::RecvLine(SOCKET hSocket, std::string &strLine)
{
    strLine.clear();
    for ( ; ; )
    {
        char c;
        int nBytes = ::recv(hSocket, &c, 1, 0);
        if (nBytes > 0) {
            if (c == '\n') {
                continue;
            }
            if (c == '\r') {
                return true;
            }
            strLine += c;
            if (strLine.size() >= 9000) {
                return true;
            }
        } else if (nBytes <= 0) {
            if (args_bool::fShutdown) {
                return false;
            }
            if (nBytes < 0) {
                int nErr = WSAGetLastError();
                if (nErr == WSAEMSGSIZE) {
                    continue;
                }
                if (nErr == WSAEWOULDBLOCK || nErr == WSAEINTR || nErr == WSAEINPROGRESS) {
                    util::Sleep(10);
                    continue;
                }
            }
            if (! strLine.empty()) {
                return true;
            }
            if (nBytes == 0) {
                // socket closed
                printf("socket closed\n");
                return false;
            } else {
                // socket error
                int nErr = WSAGetLastError();
                printf("recv failed: %d\n", nErr);
                return false;
            }
        }
    }
}

//
// used when scores of local addresses may have changed
// pushes better local address to peers
//
void ext_ip::AdvertizeLocal()
{
    LOCK(net_node::cs_vNodes);
    BOOST_FOREACH(CNode *pnode, net_node::vNodes)
    {
        if (pnode->fSuccessfullyConnected) {
            CAddress addrLocal = ext_ip::GetLocalAddress(&pnode->addr);
            if (addrLocal.IsRoutable() && (CService)addrLocal != pnode->addrLocal) {
                pnode->PushAddress(addrLocal);
                pnode->addrLocal = addrLocal;
            }
        }
    }
}

void ext_ip::SetReachable(enum netbase::Network net, bool fFlag/* = true */)
{
    LOCK(ext_ip::cs_mapLocalHost);
    ext_ip::vfReachable[net] = fFlag;
    if (net == netbase::NET_IPV6 && fFlag) {
        ext_ip::vfReachable[netbase::NET_IPV4] = true;
    }
}

int ext_ip::GetnScore(const CService &addr)
{
    LOCK(ext_ip::cs_mapLocalHost);
    if (ext_ip::mapLocalHost.count(addr) == LOCAL_NONE) {
        return 0;
    }
    return ext_ip::mapLocalHost[addr].nScore;
}


// Is our peer's addrLocal potentially useful as an external IP source?
bool ext_ip::IsPeerAddrLocalGood(CNode *pnode)
{
    return args_bool::fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
           !ext_ip::IsLimited(pnode->addrLocal.GetNetwork());
}

//
// pushes our own address to a peer
//
void ext_ip::AdvertiseLocal(CNode *pnode)
{
    if (!args_bool::fNoListen && pnode->fSuccessfullyConnected) {
        CAddress addrLocal = ext_ip::GetLocalAddress(&pnode->addr);
        //
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        //
        if (ext_ip::IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() || bitsystem::GetRand((ext_ip::GetnScore(addrLocal) > LOCAL_MANUAL) ? 8:2) == 0)) {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable()) {
            printf("ext_ip::AdvertiseLocal: advertising address %s\n", addrLocal.ToString().c_str());
            pnode->PushAddress(addrLocal);
        }
    }
}

//
// learn a new local address
//
bool ext_ip::AddLocal(const CService& addr, int nScore/* =LOCAL_NONE */)
{
    if (! addr.IsRoutable()) {
        return false;
    }
    if (!args_bool::fDiscover && nScore < LOCAL_MANUAL) {
        return false;
    }
    if (ext_ip::IsLimited(addr)) {
        return false;
    }

    printf("ext_ip::AddLocal(%s,%i)\n", addr.ToString().c_str(), nScore);

    {
        LOCK(ext_ip::cs_mapLocalHost);
        bool fAlready = ext_ip::mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = ext_ip::mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore) {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
        ext_ip::SetReachable(addr.GetNetwork());
    }

    ext_ip::AdvertizeLocal();

    return true;
}

bool ext_ip::AddLocal(const CNetAddr &addr, int nScore/* =LOCAL_NONE */)
{
    return ext_ip::AddLocal(CService(addr, net_basis::GetListenPort()), nScore);
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void ext_ip::SetLimited(enum netbase::Network net, bool fLimited/* = true */)
{
    if (net == netbase::NET_UNROUTABLE) {
        return;
    }

    LOCK(ext_ip::cs_mapLocalHost);
    ext_ip::vfLimited[net] = fLimited;
}

bool ext_ip::IsLimited(enum netbase::Network net)
{
    LOCK(ext_ip::cs_mapLocalHost);
    return ext_ip::vfLimited[net];
}

bool ext_ip::IsLimited(const CNetAddr &addr)
{
    return ext_ip::IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool ext_ip::SeenLocal(const CService& addr)
{
    {
        LOCK(ext_ip::cs_mapLocalHost);
        if (ext_ip::mapLocalHost.count(addr) == 0) {
            return false;
        }
        ext_ip::mapLocalHost[addr].nScore++;
    }

    ext_ip::AdvertizeLocal();
    return true;
}

/** check whether a given address is potentially local */
bool ext_ip::IsLocal(const CService &addr)
{
    LOCK(ext_ip::cs_mapLocalHost);
    return ext_ip::mapLocalHost.count(addr) > 0;
}

/** check whether a given address is in a network we can probably connect to */
bool ext_ip::IsReachable(const CNetAddr &addr)
{
    LOCK(ext_ip::cs_mapLocalHost);
    enum netbase::Network net = addr.GetNetwork();
    return ext_ip::vfReachable[net] && !ext_ip::vfLimited[net];
}

//
// We now get our external IP from the IRC server first and only use this as a backup
//
bool ext_ip::GetMyExternalIP(CNetAddr &ipRet)
{
    struct sockaddr_in mapped;
    uint64_t rnd = std::numeric_limits<uint64_t>::max();
    const char *srv;
    int rc = stun_ext::GetExternalIPbySTUN(rnd, &mapped, &srv);

    if(rc >= 0) {
        ipRet = CNetAddr(mapped.sin_addr);
        printf("GetExternalIPbySTUN(%" PRIu64 ") returned %s in attempt %d; Server=%s\n", rnd, ipRet.ToStringIP().c_str(), rc, srv);
        return true;
    }
    return false;
}

void ext_ip::ThreadGetMyExternalIP(void *parg)
{
    // Make this thread recognisable as the external IP detection thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-ext-ip").c_str());

    CNetAddr addrLocalHost;
    if (ext_ip::GetMyExternalIP(addrLocalHost)) {
        printf("ext_ip::GetMyExternalIP() returned %s\n", addrLocalHost.ToStringIP().c_str());
        ext_ip::AddLocal(addrLocalHost, LOCAL_HTTP);
    }
}

void net_node::AddressCurrentlyConnected(const CService &addr)
{
    net_node::addrman.Connected(addr);
}

CNode *net_node::FindNode(const CNetAddr &ip)
{
    LOCK(net_node::cs_vNodes);
    BOOST_FOREACH(CNode *pnode, vNodes)
    {
        if ((CNetAddr)pnode->addr == ip) {
            return (pnode);
        }
    }
    return NULL;
}

CNode *net_node::FindNode(std::string addrName)
{
    LOCK(net_node::cs_vNodes);
    BOOST_FOREACH(CNode *pnode, vNodes)
    {
        if (pnode->addrName == addrName) {
            return (pnode);
        }
    }
    return NULL;
}

CNode *net_node::FindNode(const CService &addr)
{
    LOCK(net_node::cs_vNodes);
    BOOST_FOREACH(CNode *pnode, vNodes)
    {
        if ((CService)pnode->addr == addr) {
            return (pnode);
        }
    }
    return NULL;
}

CNode *net_node::ConnectNode(CAddress addrConnect, const char *pszDest/*=NULL*/, int64_t nTimeout/*=0*/)
{
    if (pszDest == NULL) {
        if (ext_ip::IsLocal(addrConnect)) {
            return NULL;
        }

        // Look for an existing connection
        CNode *pnode = FindNode((CService)addrConnect);
        if (pnode) {
            if (nTimeout != 0) {
                pnode->AddRef(nTimeout);
            } else {
                pnode->AddRef();
            }
            return pnode;
        }
    }

    /// debug print
    printf("trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString().c_str(),
        pszDest ? 0 : (double)(bitsystem::GetAdjustedTime() - addrConnect.get_nTime()) / 3600.0);

    //
    // Connect
    //
    SOCKET hSocket;
    if (pszDest ? netbase::manage::ConnectSocketByName(addrConnect, hSocket, pszDest, net_basis::GetDefaultPort()) : netbase::manage::ConnectSocket(addrConnect, hSocket)) {
        net_node::addrman.Attempt(addrConnect);

        /// debug print
        printf("connected %s\n", pszDest ? pszDest : addrConnect.ToString().c_str());

        // Set to non-blocking
#ifdef WIN32
        u_long nOne = 1;
        if (::ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR) {
            printf("ConnectSocket() : ioctlsocket non-blocking setting failed, error %d\n", WSAGetLastError());
        }
#else
        if (::fcntl(hSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR) {
            printf("ConnectSocket() : fcntl non-blocking setting failed, error %d\n", errno);
        }
#endif

        // Add node
        CNode *pnode = new(std::nothrow) CNode(hSocket, addrConnect, pszDest ? pszDest : "", false);
        if(! pnode) {
            printf("ConnectSocket() : CNode memory allocate failure, error\n");
            return NULL;
        }
        if (nTimeout != 0) {
            pnode->AddRef(nTimeout);
        } else {
            pnode->AddRef();
        }

        {
            LOCK(net_node::cs_vNodes);
            net_node::vNodes.push_back(pnode);
        }

        pnode->nTimeConnected = bitsystem::GetTime();
        return pnode;
    } else {
        return NULL;
    }
}

void CNode::CloseSocketDisconnect()
{
    fDisconnect = true;
    if (hSocket != INVALID_SOCKET) {
        printf("disconnecting node %s\n", addrName.c_str());
        netbase::manage::CloseSocket(hSocket);
        vRecv.clear();
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecv, lockRecv);
    if (lockRecv) {
        vRecv.clear();
    }

    // if this was the sync node, we'll need a new one
    // if (this == pnodeSync) {
    if(net_node::Is_pnodeSync(this)) {
        // pnodeSync = NULL;
        net_node::setnull_pnodeSync();
    }
}

void CNode::Cleanup() {}

void CNode::PushVersion()
{
    int64_t nTime = bitsystem::GetAdjustedTime();
    CAddress addrYou, addrMe;

    bool fHidden = false;
    if (addr.IsTor()) {
        if (map_arg::GetMapArgsCount("-torname")) {
            // Our hidden service address
            CService addrTorName(map_arg::GetMapArgsString("-torname"), net_basis::GetListenPort());

            if (addrTorName.IsValid()) {
                addrYou = addr;
                addrMe = CAddress(addrTorName);
                fHidden = true;
            }
        }
    }

    if (! fHidden) {
        addrYou = (addr.IsRoutable() && !netbase::manage::IsProxy(addr) ? addr : CAddress(CService(net_basis::strIpZero, net_basis::nPortZero)));
        addrMe = ext_ip::GetLocalAddress(&addr);
    }

    RAND_bytes((unsigned char *)&bitsocket::nLocalHostNonce, sizeof(bitsocket::nLocalHostNonce));
    printf("send version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n", version::PROTOCOL_VERSION, block_info::nBestHeight, addrMe.ToString().c_str(), addrYou.ToString().c_str(), addr.ToString().c_str());
    PushMessage("version", version::PROTOCOL_VERSION, net_node::nLocalServices, nTime, addrYou, addrMe, bitsocket::nLocalHostNonce, format_version::FormatSubVersion(version::CLIENT_NAME, version::CLIENT_VERSION, std::vector<std::string>()), block_info::nBestHeight);
}

void CNode::ClearBanned()
{
    setBanned.clear();
}

bool CNode::IsBanned(CNetAddr ip)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        std::map<CNetAddr, int64_t>::iterator i = setBanned.find(ip);
        if (i != setBanned.end())
        {
            int64_t t = (*i).second;
            if (bitsystem::GetTime() < t) {
                fResult = true;
            }
        }
    }
    return fResult;
}

bool CNode::Misbehaving(int howmuch)
{
    if (addr.IsLocal()) {
        printf("Warning: Local node %s misbehaving (delta: %d)!\n", addrName.c_str(), howmuch);
        return false;
    }

    nMisbehavior += howmuch;
    if (nMisbehavior >= map_arg::GetArgInt("-banscore", 100)) {
        int64_t banTime = bitsystem::GetTime()+map_arg::GetArg("-bantime", util::nOneDay);  // Default 24-hour ban
        printf("Misbehaving: %s (%d -> %d) DISCONNECTING\n", addr.ToString().c_str(), nMisbehavior - howmuch, nMisbehavior);
        {
            LOCK(cs_setBanned);
            if (setBanned[this->addr] < banTime) {
                setBanned[this->addr] = banTime;
            }
        }
        CloseSocketDisconnect();
        return true;
    } else {
        printf("Misbehaving: %s (%d -> %d)\n", addr.ToString().c_str(), nMisbehavior - howmuch, nMisbehavior);
    }
    
    return false;
}

#undef X
#define X(name) stats.name = this->name
void CNode::copyStats(CNodeStats &stats)
{
    X(nServices);
    X(nLastSend);
    X(nLastRecv);
    X(nTimeConnected);
    X(addrName);
    X(nVersion);
    X(strSubVer);
    X(fInbound);
    X(nReleaseTime);
    X(nStartingHeight);
    X(nMisbehavior);
    X(nSendBytes);
    X(nRecvBytes);
    // stats.fSyncNode = (this == pnodeSync);
    stats.fSyncNode = net_node::Is_pnodeSync(this);
}
#undef X

void net_node::ThreadSocketHandler(void *parg)
{
    // Make this thread recognisable as the networking thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-net").c_str());

    try
    {
        net_node::vnThreadsRunning[THREAD_SOCKETHANDLER]++;
        net_node::ThreadSocketHandler2(parg);
        net_node::vnThreadsRunning[THREAD_SOCKETHANDLER]--;
    } catch (const std::exception &e) {
        net_node::vnThreadsRunning[THREAD_SOCKETHANDLER]--;
        excep::PrintException(&e, "net_node::ThreadSocketHandler()");
    } catch (...) {
        net_node::vnThreadsRunning[THREAD_SOCKETHANDLER]--;
        throw; // support pthread_cancel()
    }
    printf("net_node::ThreadSocketHandler exited\n");
}

// static std::list<CNode *> vNodesDisconnected;
void net_node::ThreadSocketHandler2(void *parg)
{
    auto Release = [](CNode *node) {
        node->Release();
    };

    printf("net_node::ThreadSocketHandler started\n");

    size_t nPrevNodeCount = 0;
    for ( ; ; )
    {
        //
        // Disconnect nodes
        //
        {
            LOCK(net_node::cs_vNodes);
            // Disconnect unused nodes
            std::vector<CNode *> vNodesCopy = net_node::vNodes;
            BOOST_FOREACH(CNode *pnode, vNodesCopy)
            {
                if (pnode->fDisconnect ||
                   (pnode->GetRefCount() <= 0 && pnode->vRecv.empty() && pnode->vSend.empty())) {
                    
                    // remove from vNodes
                    net_node::vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();

                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();
                    pnode->Cleanup();

                    // hold in disconnected pool until all refs are released
                    pnode->nReleaseTime = std::max(pnode->nReleaseTime, bitsystem::GetTime() + 15 * 60);
                    if (pnode->fNetworkNode || pnode->fInbound) {
                        pnode->Release();
                    }
                    
                    vNodesDisconnected.push_back(pnode);
                }
            }

            // Delete disconnected nodes
            std::list<CNode *> vNodesDisconnectedCopy = vNodesDisconnected;
            BOOST_FOREACH(CNode* pnode, vNodesDisconnectedCopy)
            {
                // wait until threads are done using it
                if (pnode->GetRefCount() <= 0) {
                    bool fDelete = false;
                    // check cs_vSend, cs_vRecv, cs_mapRequests, cs_inventory
                    {
                        TRY_LOCK(pnode->cs_vSend, lockSend);
                        if (lockSend) {
                            TRY_LOCK(pnode->cs_vRecv, lockRecv);
                            if (lockRecv) {
                                TRY_LOCK(pnode->cs_mapRequests, lockReq);
                                if (lockReq) {
                                    TRY_LOCK(pnode->cs_inventory, lockInv);
                                    if (lockInv) {
                                        fDelete = true;
                                    }
                                }
                            }
                        }
                    }
                    if (fDelete) {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }
        if (net_node::vNodes.size() != nPrevNodeCount) {
            nPrevNodeCount = net_node::vNodes.size();
            CClientUIInterface::uiInterface.NotifyNumConnectionsChanged(net_node::vNodes.size());
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        BOOST_FOREACH(SOCKET hListenSocket, bitsocket::vhListenSocket)
        {
            FD_SET(hListenSocket, &fdsetRecv);
            hSocketMax = std::max(hSocketMax, hListenSocket);
            have_fds = true;
        }
        {
            LOCK(net_node::cs_vNodes);
            BOOST_FOREACH(CNode* pnode, net_node::vNodes)
            {
                if (pnode->hSocket == INVALID_SOCKET) {
                    continue;
                }
                
                FD_SET(pnode->hSocket, &fdsetRecv);
                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = std::max(hSocketMax, pnode->hSocket);
                have_fds = true;
                {
                    TRY_LOCK(pnode->cs_vSend, lockSend);
                    if (lockSend && !pnode->vSend.empty()) {
                        FD_SET(pnode->hSocket, &fdsetSend);
                    }
                }
            }
        }

        net_node::vnThreadsRunning[THREAD_SOCKETHANDLER]--;
        int nSelect = ::select(have_fds ? hSocketMax + 1: 0, &fdsetRecv, &fdsetSend, &fdsetError, &timeout);

        net_node::vnThreadsRunning[THREAD_SOCKETHANDLER]++;
        if (args_bool::fShutdown) {
            return;
        }
        if (nSelect == SOCKET_ERROR) {
            if (have_fds) {
                int nErr = WSAGetLastError();
                printf("socket select error %d\n", nErr);
                for (unsigned int i = 0; i <= hSocketMax; ++i)
                {
                    FD_SET(i, &fdsetRecv);
                }
            }

            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            util::Sleep(timeout.tv_usec / 1000);
        }

        //
        // Accept new connections
        //
        BOOST_FOREACH(SOCKET hListenSocket, bitsocket::vhListenSocket)
        {
            if (hListenSocket != INVALID_SOCKET && FD_ISSET(hListenSocket, &fdsetRecv)) {
#ifdef USE_IPV6
                struct sockaddr_storage sockaddr;
#else
                struct sockaddr sockaddr;
#endif
                socklen_t len = sizeof(sockaddr);
                SOCKET hSocket = ::accept(hListenSocket, (struct sockaddr *)&sockaddr, &len);
                CAddress addr;
                int nInbound = 0;

                if (hSocket != INVALID_SOCKET) {
                    if (! addr.SetSockAddr((const struct sockaddr*)&sockaddr)) {
                        printf("Warning: Unknown socket family\n");
                    }
                }

                {
                    LOCK(net_node::cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, net_node::vNodes)
                    {
                        if (pnode->fInbound) {
                            nInbound++;
                        }
                    }
                }

                if (hSocket == INVALID_SOCKET) {
                    int nErr = WSAGetLastError();
                    if (nErr != WSAEWOULDBLOCK) {
                        printf("socket error accept failed: %d\n", nErr);
                    }
                } else if (nInbound >= map_arg::GetArgInt("-maxconnections", 125) - MAX_OUTBOUND_CONNECTIONS) {
                    {
                        LOCK(net_node::cs_setservAddNodeAddresses);
                        if (! net_node::setservAddNodeAddresses.count(addr)) {
                            netbase::manage::CloseSocket(hSocket);
                        }
                    }
                } else if (CNode::IsBanned(addr)) {
                    printf("connection from %s dropped (banned)\n", addr.ToString().c_str());
                    netbase::manage::CloseSocket(hSocket);
                } else {
                    printf("accepted connection %s\n", addr.ToString().c_str());
                    CNode *pnode = new CNode(hSocket, addr, "", true);
                    pnode->AddRef();
                    {
                        LOCK(net_node::cs_vNodes);
                        net_node::vNodes.push_back(pnode);
                    }
                }
            }
        }

        //
        // Service each socket
        //
        std::vector<CNode *> vNodesCopy;
        {
            LOCK(net_node::cs_vNodes);
            vNodesCopy = net_node::vNodes;
            BOOST_FOREACH(CNode *pnode, vNodesCopy)
            {
                pnode->AddRef();
            }
        }

        BOOST_FOREACH(CNode *pnode, vNodesCopy)
        {
            if (args_bool::fShutdown) {
                return;
            }

            //
            // Receive
            //
            if (pnode->hSocket == INVALID_SOCKET) {
                continue;
            }
            if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError)) {
                TRY_LOCK(pnode->cs_vRecv, lockRecv);
                if (lockRecv) {
                    CDataStream &vRecv = pnode->vRecv;
                    uint64_t nPos = vRecv.size();

                    if (nPos > net_node::ReceiveBufferSize()) {
                        if (! pnode->fDisconnect) {
                            printf("socket recv flood control disconnect (%" PRIszu " bytes)\n", vRecv.size());
                        }
                        pnode->CloseSocketDisconnect();
                    } else {
                        // typical socket buffer is 8K-64K
                        char pchBuf[0x10000];
                        int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                        if (nBytes > 0) {
                            vRecv.resize(nPos + nBytes);
                            ::memcpy(&vRecv[nPos], pchBuf, nBytes);
                            pnode->nLastRecv = bitsystem::GetTime();
                            pnode->nRecvBytes += nBytes;
                            pnode->RecordBytesRecv(nBytes);
                        } else if (nBytes == 0) {
                            // socket closed gracefully
                            if (! pnode->fDisconnect) {
                                printf("socket closed\n");
                            }
                            pnode->CloseSocketDisconnect();
                        } else if (nBytes < 0) {
                            // error
                            int nErr = WSAGetLastError();
                            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
                                if (! pnode->fDisconnect) {
                                    printf("socket recv error %d\n", nErr);
                                }
                                pnode->CloseSocketDisconnect();
                            }
                        }
                    }
                }
            }

            //
            // Send
            //
            if (pnode->hSocket == INVALID_SOCKET) {
                continue;
            }
            if (FD_ISSET(pnode->hSocket, &fdsetSend)) {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend) {
                    CDataStream &vSend = pnode->vSend;
                    if (! vSend.empty()) {
                        int nBytes = send(pnode->hSocket, &vSend[0], vSend.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
                        if (nBytes > 0) {
                            vSend.erase(vSend.begin(), vSend.begin() + nBytes);
                            pnode->nLastSend = bitsystem::GetTime();
                            pnode->nSendBytes += nBytes;
                            pnode->RecordBytesSent(nBytes);
                        } else if (nBytes < 0) {
                            // error
                            int nErr = WSAGetLastError();
                            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
                                printf("socket send error %d\n", nErr);
                                pnode->CloseSocketDisconnect();
                            }
                        }
                    }
                }
            }

            //
            // Inactivity checking
            //
            if (pnode->vSend.empty()) {
                pnode->nLastSendEmpty = bitsystem::GetTime();
            }
            if (bitsystem::GetTime() - pnode->nTimeConnected > 60) {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0) {
                    printf("socket no message in first 60 seconds, %d %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0);
                    pnode->fDisconnect = true;
                } else if (bitsystem::GetTime() - pnode->nLastSend > 90 * 60 && bitsystem::GetTime() - pnode->nLastSendEmpty > 90 * 60) {
                    printf("socket not sending\n");
                    pnode->fDisconnect = true;
                } else if (bitsystem::GetTime() - pnode->nLastRecv > 90 * 60) {
                    printf("socket inactivity timeout\n");
                    pnode->fDisconnect = true;
                }
            }
        } // BOOST_FOREACH(CNode *pnode, vNodesCopy)

        {
            LOCK(net_node::cs_vNodes);
            std::for_each(vNodesCopy.begin(), vNodesCopy.end(), Release);
        }

        util::Sleep(10);
    }
}

#ifdef USE_UPNP
void upnp::ThreadMapPort(void *parg)
{
    // Make this thread recognisable as the UPnP thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-UPnP").c_str());

    try {
        net_node::vnThreadsRunning[THREAD_UPNP]++;
        ThreadMapPort2(parg);
        net_node::vnThreadsRunning[THREAD_UPNP]--;
    } catch (const std::exception &e) {
        net_node::vnThreadsRunning[THREAD_UPNP]--;
        excep::PrintException(&e, "upnp::ThreadMapPort()");
    } catch (...) {
        net_node::vnThreadsRunning[THREAD_UPNP]--;
        excep::PrintException(NULL, "upnp::ThreadMapPort()");
    }

    printf("upnp::ThreadMapPort() exited\n");
}

void upnp::ThreadMapPort2(void *parg)
{
    printf("upup::ThreadMapPort started\n");

    std::string port = strprintf("%u", net_basis::GetListenPort());
    const char *multicastif = 0;
    const char *minissdpdpath = 0;
    char lanaddr[64] = { 0 };

#ifndef UPNPDISCOVER_SUCCESS
    /* miniupnpc 1.5 */
    struct UPNPDev *devlist = ::upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
    /* miniupnpc 1.6 */
    int error = 0;
    struct UPNPDev *devlist = ::upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    /* miniupnpc 2.1 */
    int error = 0;
    struct UPNPDev *devlist = ::upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls = { 0 };
    struct IGDdatas data = { 0 };
    int r = ::UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1) {
        if (args_bool::fDiscover) {
            char externalIPAddress[40];
            r = ::UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if(r != UPNPCOMMAND_SUCCESS) {
                printf("UPnP: upnp::GetExternalIPAddress() returned %d\n", r);
            } else {
                if(externalIPAddress[0]) {
                    printf("UPnP: upnp::ExternalIPAddress = %s\n", externalIPAddress);
                    ext_ip::AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
                } else {
                    printf("UPnP: GetExternalIPAddress failed.\n");
                }
            }
        }

        std::string strDesc = (coin_param::strCoinName + " " + format_version::FormatFullVersion()).c_str();
#ifndef UPNPDISCOVER_SUCCESS
        /* miniupnpc 1.5 */
        r = ::UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                  port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0);
#else
        /* miniupnpc 1.6 */
        r = ::UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                  port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");
#endif

        if(r!=UPNPCOMMAND_SUCCESS) {
            printf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n", port.c_str(), port.c_str(), lanaddr, r, strupnperror(r));
        } else {
            printf("UPnP Port Mapping successful.\n");
        }

        int i = 1;
        while (true)
        {
            if (args_bool::fShutdown || !args_bool::fUseUPnP) {
                r = ::UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
                printf("upnp::UPNP_DeletePortMapping() returned : %d\n", r);
                
                ::freeUPNPDevlist(devlist); devlist = 0;
                ::FreeUPNPUrls(&urls);
                return;
            }
            if (i % 600 == 0) { // Refresh every 20 minutes
#ifndef UPNPDISCOVER_SUCCESS
                /* miniupnpc 1.5 */
                r = ::UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                          port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0);
#else
                /* miniupnpc 1.6 */
                r = ::UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                          port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");
#endif

                if(r!=UPNPCOMMAND_SUCCESS) {
                    printf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n", port.c_str(), port.c_str(), lanaddr, r, strupnperror(r));
                } else {
                    printf("UPnP Port Mapping successful.\n");
                }
            }
            util::MilliSleep(2000);
            ++i;
        }
    } else {
        printf("No valid UPnP IGDs found\n");
        ::freeUPNPDevlist(devlist); devlist = 0;
        if (r != 0) {
            ::FreeUPNPUrls(&urls);
        }

        while (true)
        {
            if (args_bool::fShutdown || !args_bool::fUseUPnP) {
                return;
            }
            util::MilliSleep(2000);
        }
    }
}

void upnp::MapPort()
{
    if (args_bool::fUseUPnP && net_node::vnThreadsRunning[THREAD_UPNP] < 1) {
        if (! bitthread::manage::NewThread(ThreadMapPort, NULL)) {
            printf("Error: ThreadMapPort(ThreadMapPort) failed\n");
        }
    }
}
#else
void upnp::MapPort()
{
    // Intentionally left blank.
}
#endif

void dns_seed::ThreadDNSAddressSeed(void *parg)
{
    // Make this thread recognisable as the DNS seeding thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-dnsseed").c_str());

    try {
        net_node::vnThreadsRunning[THREAD_DNSSEED]++;
        dns_seed::ThreadDNSAddressSeed2(parg);
        net_node::vnThreadsRunning[THREAD_DNSSEED]--;
    } catch (const std::exception &e) {
        net_node::vnThreadsRunning[THREAD_DNSSEED]--;
        excep::PrintException(&e, "dns_seed::ThreadDNSAddressSeed()");
    } catch (...) {
        net_node::vnThreadsRunning[THREAD_DNSSEED]--;
        throw; // support pthread_cancel()
    }
    printf("dns_seed::ThreadDNSAddressSeed exited\n");
}

void dns_seed::ThreadDNSAddressSeed2(void *parg)
{
    printf("dns_seed::ThreadDNSAddressSeed started\n");
    int found = 0;

    //if (! args_bool::fTestNet) {
        printf("Loading addresses from DNS seeds (could take a while)\n");

        for (unsigned int seed_idx = 0; seed_idx < ARRAYLEN(dns_seed::strDNSSeed); ++seed_idx)
        {
            if (netbase::manage::HaveNameProxy()) {
                shot::AddOneShot(dns_seed::strDNSSeed[seed_idx][1]);
            } else {
                std::vector<CNetAddr> vaddr;
                std::vector<CAddress> vAdd;
                if (netbase::manage::LookupHost(dns_seed::strDNSSeed[seed_idx][1], vaddr)) {
                    BOOST_FOREACH(CNetAddr &ip, vaddr)
                    {
                        CAddress addr = CAddress(CService(ip, net_basis::GetDefaultPort()));
                        addr.set_nTime( bitsystem::GetTime() - 3 * util::nOneDay - bitsystem::GetRand(4 * util::nOneDay) ); // use a random age between 3 and 7 days old
                        vAdd.push_back(addr);
                        found++;
                    }
                }
                net_node::addrman.Add(vAdd, CNetAddr(dns_seed::strDNSSeed[seed_idx][0], true));
            }
        }
    //}

    printf("%d addresses found from DNS seeds\n", found);
}

void net_node::DumpAddresses()
{
    int64_t nStart = util::GetTimeMillis();

    CAddrDB adb;
    adb.Write(net_node::addrman);

    printf("Flushed %d addresses to peers.dat  %" PRId64 "ms\n", net_node::addrman.size(), util::GetTimeMillis() - nStart);
}

void net_node::ThreadDumpAddress2(void *parg)
{
    printf("net_node::ThreadDumpAddress started\n");

    net_node::vnThreadsRunning[THREAD_DUMPADDRESS]++;
    while (! args_bool::fShutdown)
    {
        net_node::DumpAddresses();
        net_node::vnThreadsRunning[THREAD_DUMPADDRESS]--;
        util::Sleep(600000);
        net_node::vnThreadsRunning[THREAD_DUMPADDRESS]++;
    }
    net_node::vnThreadsRunning[THREAD_DUMPADDRESS]--;
}

void net_node::ThreadDumpAddress(void *parg)
{
    // Make this thread recognisable as the address dumping thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-adrdump").c_str());

    try {
        net_node::ThreadDumpAddress2(parg);
    } catch (std::exception &e) {
        excep::PrintException(&e, "ThreadDumpAddress()");
    }
    printf("ThreadDumpAddress exited\n");
}

void net_node::ThreadOpenConnections(void *parg)
{
    // Make this thread recognisable as the connection opening thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-opencon").c_str());

    try {
        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
        net_node::ThreadOpenConnections2(parg);
        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
    } catch (const std::exception &e) {
        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        excep::PrintException(&e, "net_node::ThreadOpenConnections()");
    } catch (...) {
        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        excep::PrintException(NULL, "net_node::ThreadOpenConnections()");
    }
    printf("net_node::ThreadOpenConnections exited\n");
}

void shot::AddOneShot(std::string strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

void shot::ProcessOneShot()
{
    std::string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty()) {
            return;
        }

        strDest = vOneShots.front();
        vOneShots.pop_front();
    }

    CAddress addr;
    CSemaphoreGrant grant(*shot::semOutbound, true);
    if (grant) {
        if (! net_node::OpenNetworkConnection(addr, &grant, strDest.c_str(), true)) {
            shot::AddOneShot(strDest);
        }
    }
}

void net_node::ThreadOpenConnections2(void *parg)
{
    printf("net_node::ThreadOpenConnections started\n");

    //
    // Connect to specific addresses
    //
    if (map_arg::GetMapArgsCount("-connect") && map_arg::GetMapMultiArgsString("-connect").size() > 0) {
        for (int64_t nLoop = 0;; ++nLoop)
        {
            shot::ProcessOneShot();
            BOOST_FOREACH(std::string strAddr, map_arg::GetMapMultiArgsString("-connect"))
            {
                CAddress addr;
                net_node::OpenNetworkConnection(addr, NULL, strAddr.c_str());
                for (int i = 0; i < 10 && i < nLoop; ++i)
                {
                    util::Sleep(500);
                    if (args_bool::fShutdown) {
                        return;
                    }
                }
            }
            util::Sleep(500);
        }
    }

    //
    // Initiate network connections
    //
    int64_t nStart = bitsystem::GetTime();
    for ( ; ; )
    {
        shot::ProcessOneShot();

        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        util::Sleep(500);
        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
        if (args_bool::fShutdown) {
            return;
        }

        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        CSemaphoreGrant grant(*shot::semOutbound);
        net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
        if (args_bool::fShutdown) {
            return;
        }

        // Add seed nodes if IRC isn't working
        if (!ext_ip::IsLimited(netbase::NET_IPV4) && net_node::addrman.size()==0 && (bitsystem::GetTime() - nStart > 60) && !args_bool::fTestNet) {
            std::vector<CAddress> vAdd;
            for (unsigned int i = 0; i < ARRAYLEN(pnSeed); i++)
            {
                // It'll only connect to one or two seed nodes because once it connects,
                // it'll get a pile of addresses with newer timestamps.
                // Seed nodes are given a random 'last seen time' of between one and two
                // weeks ago.
                struct in_addr ip;
                ::memcpy(&ip, &pnSeed[i], sizeof(ip));
                CAddress addr(CService(ip, net_basis::GetDefaultPort()));
                addr.set_nTime( bitsystem::GetTime() - bitsystem::GetRand(util::nOneWeek) - util::nOneWeek );
                vAdd.push_back(addr);
            }
            net_node::addrman.Add(vAdd, CNetAddr(net_basis::strLocal));
        }

        // Add Tor nodes if we have connection with onion router
        if (map_arg::GetMapArgsCount("-tor")) {
            std::vector<CAddress> vAdd;
            for (unsigned int i = 0; i < ARRAYLEN(pchTorSeed); ++i)
            {
                CAddress addr(CService(pchTorSeed[i], net_basis::GetDefaultPort()));
                addr.set_nTime( bitsystem::GetTime() - bitsystem::GetRand(util::nOneWeek) - util::nOneWeek );
                vAdd.push_back(addr);
            }
            net_node::addrman.Add(vAdd, CNetAddr(("dummy_" + coin_param::strCoinName + ".onion").c_str()));
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
        int nOutbound = 0;
        std::set<std::vector<unsigned char> > setConnected;
        {
            LOCK(net_node::cs_vNodes);
            BOOST_FOREACH(CNode *pnode, net_node::vNodes)
            {
                if (! pnode->fInbound) {
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;
                }
            }
        }

        int64_t nANow = bitsystem::GetAdjustedTime();

        int nTries = 0;
        for ( ; ; )
        {
            // use an nUnkBias between 10 (no outgoing connections) and 90 (8 outgoing connections)
            CAddress addr = net_node::addrman.Select(10 + std::min(nOutbound,8)*10);

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || ext_ip::IsLocal(addr)) {
                break;
            }

            // If we didn't find an appropriate destination after trying 100 addresses fetched from net_node::addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new net_node::addrman addresses.
            nTries++;
            if (nTries > 100) {
                break;
            }

            if (ext_ip::IsLimited(addr)) {
                continue;
            }

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.get_nLastTry() < 600 && nTries < 30) {
                continue;
            }

            // do not allow non-default ports, unless after 50 invalid addresses selected already
            if (addr.GetPort() != net_basis::GetDefaultPort() && nTries < 50) {
                continue;
            }

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid()) {
            net_node::OpenNetworkConnection(addrConnect, &grant);
        }
    }
}

void net_node::ThreadOpenAddedConnections(void *parg)
{
    // Make this thread recognisable as the connection opening thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-opencon").c_str());

    try {
        net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]++;
        net_node::ThreadOpenAddedConnections2(parg);
        net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
    } catch (const std::exception &e) {
        net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
        excep::PrintException(&e, "net_node::ThreadOpenAddedConnections()");
    } catch (...) {
        net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
        excep::PrintException(NULL, "net_node::ThreadOpenAddedConnections()");
    }
    printf("net_node::ThreadOpenAddedConnections exited\n");
}

void net_node::ThreadOpenAddedConnections2(void *parg)
{
    printf("net_node::ThreadOpenAddedConnections started\n");

    {
        LOCK(net_node::cs_vAddedNodes);
        net_node::vAddedNodes = map_arg::GetMapMultiArgsString("-addnode");
    }

    if (netbase::manage::HaveNameProxy()) {
        while(! args_bool::fShutdown)
        {
            std::list<std::string> lAddresses(0);
            {
                LOCK(net_node::cs_vAddedNodes);
                BOOST_FOREACH(std::string &strAddNode, net_node::vAddedNodes)
                {
                    lAddresses.push_back(strAddNode);
                }
            }
            BOOST_FOREACH(std::string &strAddNode, lAddresses)
            {
                CAddress addr;
                CSemaphoreGrant grant(*shot::semOutbound);
                net_node::OpenNetworkConnection(addr, &grant, strAddNode.c_str());
                util::Sleep(500);
            }
            net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
            util::Sleep(120000); // Retry every 2 minutes
            net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]++;
        }
        return;
    }

    for (uint32_t i = 0; true; i++)
    {
        std::list<std::string> lAddresses(0);
        {
            LOCK(net_node::cs_vAddedNodes);
            BOOST_FOREACH(std::string &strAddNode, net_node::vAddedNodes)
            {
                lAddresses.push_back(strAddNode);
            }
        }

        std::list<std::vector<CService> > lservAddressesToAdd(0);
        BOOST_FOREACH(std::string &strAddNode, lAddresses)
        {
            std::vector<CService> vservNode(0);
            if (netbase::manage::Lookup(strAddNode.c_str(), vservNode, net_basis::GetDefaultPort(), netbase::fNameLookup, 0))
            {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(net_node::cs_setservAddNodeAddresses);
                    BOOST_FOREACH(CService &serv, vservNode)
                    {
                        net_node::setservAddNodeAddresses.insert(serv);
                    }
                }
            }
        }

        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(net_node::cs_vNodes);
            BOOST_FOREACH(CNode* pnode, net_node::vNodes)
            {
                for (std::list<std::vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                {
                    BOOST_FOREACH(CService& addrNode, *(it))
                    {
                        if (pnode->addr == addrNode) {
                            it = lservAddressesToAdd.erase(it);
                            if(it != lservAddressesToAdd.begin()) {
                                it--;
                            }
                            break;
                        }
                    }
                    if (it == lservAddressesToAdd.end()) {
                        break;
                    }
                }
            }
        }
        BOOST_FOREACH(std::vector<CService>& vserv, lservAddressesToAdd)
        {
            if (vserv.size() == 0) {
                continue;
            }

            CSemaphoreGrant grant(*shot::semOutbound);
            net_node::OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
            util::Sleep(500);
            if (args_bool::fShutdown) {
                return;
            }
        }
        if (args_bool::fShutdown) {
            return;
        }

        net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
        util::Sleep(120000); // Retry every 2 minutes
        net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS]++;
        if (args_bool::fShutdown) {
            return;
        }
    }
}

//
// if successful, this moves the passed grant to the constructed node
//
bool net_node::OpenNetworkConnection(const CAddress &addrConnect, CSemaphoreGrant *grantOutbound/*=NULL*/, const char *strDest/*=NULL*/, bool fOneShot/*=false*/)
{
    //
    // Initiate outbound network connection
    //
    if (args_bool::fShutdown) {
        return false;
    }
    if (! strDest) {
        if (ext_ip::IsLocal(addrConnect) || FindNode((CNetAddr)addrConnect) || CNode::IsBanned(addrConnect) || FindNode(addrConnect.ToStringIPPort().c_str())) {
            return false;
        }
    }
    if (strDest && FindNode(strDest)) {
        return false;
    }

    net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
    CNode *pnode = ConnectNode(addrConnect, strDest);
    net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
    if (args_bool::fShutdown) {
        return false;
    }
    if (! pnode) {
        return false;
    }
    if (grantOutbound) {
        grantOutbound->MoveTo(pnode->grantOutbound);
    }

    pnode->fNetworkNode = true;
    if (fOneShot) {
        pnode->fOneShot = true;
    }

    return true;
}

//
// for now, use a very simple selection metric: the node from which we received
// most recently
//
/* C++ lambda
static int64_t NodeSyncScore(const CNode *pnode)
{
    return pnode->nLastRecv;
}
*/

void net_node::StartSync(const std::vector<CNode *> &__vNodes)
{
    auto NodeSyncScore = [](const CNode *pnode) {
        return pnode->nLastRecv;
    };

    CNode *pnodeNewSync = NULL;
    int64_t nBestScore = 0;

    //
    // Iterate over all nodes
    //
    BOOST_FOREACH(CNode *pnode, __vNodes)
    {
        // check preconditions for allowing a sync
        if (!pnode->fClient && !pnode->fOneShot &&
            !pnode->fDisconnect && pnode->fSuccessfullyConnected &&
            (pnode->nStartingHeight > (block_info::nBestHeight - 144)) &&
            (pnode->nVersion < version::NOBLKS_VERSION_START || pnode->nVersion >= version::NOBLKS_VERSION_END)) {
            //
            // if ok, compare node's score with the best so far
            //
            int64_t nScore = NodeSyncScore(pnode);
            if (pnodeNewSync == NULL || nScore > nBestScore) {
                pnodeNewSync = pnode;
                nBestScore = nScore;
            }
        }
    }

    //
    // if a new sync candidate was found, start sync!
    //
    if (pnodeNewSync) {
        pnodeNewSync->fStartSync = true;
        pnodeSync = pnodeNewSync;
    }
}

void net_node::ThreadMessageHandler(void *parg)
{
    // Make this thread recognisable as the message handling thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-msghand").c_str());

    try {
        net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER]++;
        net_node::ThreadMessageHandler2(parg);
        net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
    } catch (const std::exception &e) {
        net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
        excep::PrintException(&e, "net_node::ThreadMessageHandler()");
    } catch (...) {
        net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
        excep::PrintException(NULL, "net_node::ThreadMessageHandler()");
    }
    printf("net_node::ThreadMessageHandler exited\n");
}

void net_node::ThreadMessageHandler2(void *parg)
{
    auto Release = [](CNode *node) {
        node->Release();
    };

    printf("net_node::ThreadMessageHandler started\n");
    bitthread::manage::SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);

    while (! args_bool::fShutdown)
    {
        bool fHaveSyncNode = false;
        std::vector<CNode *> vNodesCopy;
        {
            LOCK(net_node::cs_vNodes);
            vNodesCopy = net_node::vNodes;
            BOOST_FOREACH(CNode *pnode, vNodesCopy)
            {
                pnode->AddRef();
                if (pnode == pnodeSync) {
                    fHaveSyncNode = true;
                }
            }
        }

        if (! fHaveSyncNode) {
            StartSync(vNodesCopy);
        }

        // Poll the connected nodes for messages
        BOOST_FOREACH(CNode *pnode, vNodesCopy)
        {
            // Receive messages
            {
                TRY_LOCK(pnode->cs_vRecv, lockRecv);
                if (lockRecv) {
                    block_process::manage::ProcessMessages(pnode);
                }
            }
            if (args_bool::fShutdown) {
                return;
            }

            // Send messages
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend) {
                    block_process::manage::SendMessages(pnode);
                }
            }
            if (args_bool::fShutdown) {
                return;
            }
        }

        {
            LOCK(net_node::cs_vNodes);
            std::for_each(vNodesCopy.begin(), vNodesCopy.end(), Release);
        }

        //
        // Wait and allow messages to bunch up.
        // Reduce net_node::vnThreadsRunning so StopNode has permission to exit while
        // we're sleeping, but we must always check args_bool::fShutdown after doing this.
        //
        net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
        util::Sleep(100);
        if (args_bool::fRequestShutdown) {
            entry::StartShutdown();
        }
        net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER]++;
        if (args_bool::fShutdown) {
            return;
        }
    }
}

bool entry::BindListenPort(const CService &addrBind, std::string &strError)
{
    strError.clear();
    int nOne = 1;

    // Create socket for listening for incoming connections
#ifdef USE_IPV6
    struct sockaddr_storage sockaddr;
#else
    struct sockaddr sockaddr;
#endif

    socklen_t len = sizeof(sockaddr);
    if (! addrBind.GetSockAddr((struct sockaddr *)&sockaddr, &len)) {
        strError = strprintf("Error: bind address family for %s not supported", addrBind.ToString().c_str());
        printf("%s\n", strError.c_str());
        return false;
    }

    SOCKET hListenSocket = ::socket(((struct sockaddr *)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET) {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

#ifndef WIN32
 #ifdef SO_NOSIGPIPE
     // Different way of disabling SIGPIPE on BSD
    ::setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
 #endif
     // Allow binding if the port is still in TIME_WAIT state after
     // the program was closed and restarted. Not an issue on windows!
    ::setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
 #endif

 #ifdef WIN32
    // Set to non-blocking, incoming connections will also inherit this
    if (::ioctlsocket(hListenSocket, FIONBIO, (u_long *)&nOne) == SOCKET_ERROR)
 #else
    if (::fcntl(hListenSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
 #endif
    {
        strError = strprintf("Error: Couldn't set properties on socket for incoming connections (error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

 #ifdef USE_IPV6
    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6()) {
  #ifdef IPV6_V6ONLY
   #ifdef WIN32
        ::setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
   #else
        ::setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
   #endif
  #endif
 #ifdef WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        ::setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char*)&nProtLevel, sizeof(int));
 #endif
    }
#endif

    if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR) {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE) {
            strError = strprintf(_(("Unable to bind to %s on this computer. " + coin_param::strCoinName + " is probably already running.").c_str()), addrBind.ToString().c_str());
        } else {
            strError = strprintf(_("Unable to bind to %s on this computer (bind returned error %d, %s)"), addrBind.ToString().c_str(), nErr, strerror(nErr));
        }
        
        printf("%s\n", strError.c_str());
        netbase::manage::CloseSocket(hListenSocket);
        return false;
    }
    printf("Bound to %s\n", addrBind.ToString().c_str());

    // Listen for incoming connections
    if (::listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        strError = strprintf("Error: Listening for incoming connections failed (listen returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        netbase::manage::CloseSocket(hListenSocket);
        return false;
    }

    bitsocket::vhListenSocket.push_back(hListenSocket);

    if (addrBind.IsRoutable() && args_bool::fDiscover) {
        ext_ip::AddLocal(addrBind, LOCAL_BIND);
    }

    return true;
}

void net_node::Discover()
{
    if (! args_bool::fDiscover) {
        return;
    }

#ifdef WIN32
    //
    // Get local host IP
    //
    char pszHostName[1000] = "";
    if (::gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR) {
        std::vector<CNetAddr> vaddr;
        if (netbase::manage::LookupHost(pszHostName, vaddr)) {
            BOOST_FOREACH (const CNetAddr &addr, vaddr)
            {
                ext_ip::AddLocal(addr, LOCAL_IF);
            }
        }
    }
#else
    //
    // Get local host ip
    //
    struct ifaddrs *myaddrs;
    if (::getifaddrs(&myaddrs) == 0) {
        for (struct ifaddrs *ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL) { continue; }
            if ((ifa->ifa_flags & IFF_UP) == 0) { continue; }
            if (strcmp(ifa->ifa_name, "lo") == 0) { continue; }
            if (strcmp(ifa->ifa_name, "lo0") == 0) { continue; }
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *s4 = (struct sockaddr_in *)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (ext_ip::AddLocal(addr, LOCAL_IF)) {
                    printf("IPv4 %s: %s\n", ifa->ifa_name, addr.ToString().c_str());
                }
            }
 #ifdef USE_IPV6
            else if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (ext_ip::AddLocal(addr, LOCAL_IF)) {
                    printf("IPv6 %s: %s\n", ifa->ifa_name, addr.ToString().c_str());
                }
            }
 #endif
        }
        ::freeifaddrs(myaddrs);
    }
#endif

    //
    // Don't use external IPv4 discovery, when -onlynet="IPv6"
    //
    if (! ext_ip::IsLimited(netbase::NET_IPV4)) {
        bitthread::manage::NewThread(ext_ip::ThreadGetMyExternalIP, NULL);
    }
}

void net_node::StartNode(void *parg)
{
    // Make this thread recognisable as the startup thread
    bitthread::manage::RenameThread((coin_param::strCoinName + "-start").c_str());

    if (shot::semOutbound == NULL) {
        // initialize semaphore
        int nConn = net_node::MAX_OUTBOUND_CONNECTIONS;
        int nMaxOutbound = std::min(nConn, map_arg::GetArgInt("-maxconnections", 125));
        shot::semOutbound = new CSemaphore(nMaxOutbound);
    }

    if (pnodeLocalHost == NULL) {
        pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService(net_basis::strLocal, net_basis::nPortZero), net_node::nLocalServices));
    }

    Discover();

    //
    // Start threads
    //
    if (! map_arg::GetBoolArg("-dnsseed", true)) {
        printf("DNS seeding disabled\n");
    } else {
        if (! bitthread::manage::NewThread(dns_seed::ThreadDNSAddressSeed, NULL)) {
            printf("Error: bitthread::manage::NewThread(dns_seed::ThreadDNSAddressSeed) failed\n");
        }
    }

    //
    // Map ports with UPnP
    //
    if (args_bool::fUseUPnP) {
        upnp::MapPort();
    }

    // Get addresses from IRC and advertise ours
    if (! map_arg::GetBoolArg("-irc", true)) {
        printf("IRC seeding disabled\n");
    } else {
        if (! bitthread::manage::NewThread(irc_ext::ThreadIRCSeed, NULL)) {
            printf("Error: bitthread::manage::NewThread(irc::ThreadIRCSeed) failed\n");
        }
    }

    // Send and receive from sockets, accept connections
    if (! bitthread::manage::NewThread(net_node::ThreadSocketHandler, NULL)) {
        printf("Error: bitthread::manage::NewThread(net_node::ThreadSocketHandler) failed\n");
    }

    // Initiate outbound connections from -addnode
    if (! bitthread::manage::NewThread(net_node::ThreadOpenAddedConnections, NULL)) {
        printf("Error: bitthread::manage::NewThread(net_node::ThreadOpenAddedConnections) failed\n");
    }

    // Initiate outbound connections
    if (! bitthread::manage::NewThread(net_node::ThreadOpenConnections, NULL)) {
        printf("Error: bitthread::manage::NewThread(net_node::ThreadOpenConnections) failed\n");
    }

    // Process messages
    if (! bitthread::manage::NewThread(net_node::ThreadMessageHandler, NULL)) {
        printf("Error: bitthread::manage::NewThread(net_node::ThreadMessageHandler) failed\n");
    }

    // Dump network addresses
    if (! bitthread::manage::NewThread(net_node::ThreadDumpAddress, NULL)) {
        printf("Error; bitthread::manage::NewThread(net_node::ThreadDumpAddress) failed\n");
    }

    // Mine proof-of-stake blocks in the background
    if (! bitthread::manage::NewThread(miner::ThreadStakeMiner, entry::pwalletMain)) {
        printf("Error: bitthread::manage::NewThread(miner::ThreadStakeMiner) failed\n");
    }

    // Start periodical NTP sampling thread
    ntp_ext::SetTrustedUpstream("-ntp", tcp_domain::strLocal); // Trusted NTP server, it's localhost by default.
    bitthread::manage::NewThread(ntp_ext::ThreadNtpSamples, NULL);
}

bool net_node::StopNode()
{
    printf("net_node::StopNode()\n");

    args_bool::fShutdown = true;
    block_info::nTransactionsUpdated++;
    int64_t nStart = bitsystem::GetTime();
    {
        LOCK(block_process::cs_main);
        block_check::thread::ThreadScriptCheckQuit();
    }
    if (shot::semOutbound) {
        for (int i=0; i<MAX_OUTBOUND_CONNECTIONS; ++i)
        {
            shot::semOutbound->post();
        }
    }

    for ( ; ; )
    {
        int nThreadsRunning = 0;
        for (int n = 0; n < THREAD_MAX; ++n)
        {
            nThreadsRunning += net_node::vnThreadsRunning[n];
        }

        if (nThreadsRunning == 0) {
            break;
        }
        if (bitsystem::GetTime() - nStart > 20) {
            break;
        }
        util::Sleep(20);
    }

    if (net_node::vnThreadsRunning[THREAD_SOCKETHANDLER] > 0) { printf("net_node::ThreadSocketHandler still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_OPENCONNECTIONS] > 0) { printf("net_node::ThreadOpenConnections still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER] > 0) { printf("net_node::ThreadMessageHandler still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_RPCLISTENER] > 0) { printf("ThreadRPCListener still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_RPCHANDLER] > 0) { printf("ThreadsRPCServer still running\n"); }
#ifdef USE_UPNP
    if (net_node::vnThreadsRunning[THREAD_UPNP] > 0) { printf("ThreadMapPort still running\n"); }
#endif
    if (net_node::vnThreadsRunning[THREAD_DNSSEED] > 0) { printf("dns_seed::ThreadDNSAddressSeed still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_ADDEDCONNECTIONS] > 0) { printf("net_node::ThreadOpenAddedConnections still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_DUMPADDRESS] > 0) { printf("net_node::ThreadDumpAddresses still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_MINTER] > 0) { printf("ThreadStakeMinter still running\n"); }
    if (net_node::vnThreadsRunning[THREAD_SCRIPTCHECK] > 0) { printf("block_check::thread::ThreadScriptCheck still running\n"); }
    while (net_node::vnThreadsRunning[THREAD_MESSAGEHANDLER] > 0 || net_node::vnThreadsRunning[THREAD_RPCHANDLER] > 0 || net_node::vnThreadsRunning[THREAD_SCRIPTCHECK] > 0)
    {
        util::Sleep(20);
    }
    util::Sleep(50);
    net_node::DumpAddresses();

    return true;
}

//
// Net Cleanup
// Singleton Class
//
class CNetCleanup
{
private:
    static CNetCleanup instance_of_cnetcleanup;

    CNetCleanup() {}
    ~CNetCleanup() {
        //
        // Close sockets
        //
        BOOST_FOREACH(CNode *pnode, net_node::vNodes)
        {
            if (pnode->hSocket != INVALID_SOCKET) {
                netbase::manage::CloseSocket(pnode->hSocket);
            }
        }

        /*
        BOOST_FOREACH(SOCKET hListenSocket, bitsocket::vhListenSocket)
        {
            if (hListenSocket != INVALID_SOCKET) {
                if (! netbase::manage::CloseSocket(hListenSocket)) {
                    printf("CloseSocket(hListenSocket) failed with error %d\n", WSAGetLastError());
                }
            }
        }
        */
        bitsocket::vhListenSocket_cleanup();

        //
        // clean up some globals (to help leak detection)
        //
        BOOST_FOREACH(CNode *pnode, net_node::vNodes)
        {
            delete pnode;
        }

        /*
        BOOST_FOREACH(CNode *pnode, vNodesDisconnected)
        {
            delete pnode;
        }
        */
        net_basis::vNodeDisconnected_cleanup();

        net_node::vNodes.clear();
        // vNodesDisconnected.clear();

        // delete semOutbound;
        // semOutbound = NULL;
        shot::semOutbound_cleanup();

        // delete pnodeLocalHost;
        // pnodeLocalHost = NULL;
        net_node::nodeLocalHost_cleanup();

#ifdef WIN32
        // Shutdown Windows Sockets
        ::WSACleanup();
#endif
    }
};
CNetCleanup CNetCleanup::instance_of_cnetcleanup;

void bitrelay::RelayTransaction(const CTransaction &tx, const uint256 &hash)
{
    CDataStream ss(SER_NETWORK, version::PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransaction(tx, hash, ss);
}

void bitrelay::RelayTransaction(const CTransaction &tx, const uint256 &hash, const CDataStream &ss)
{
    CInv inv(_CINV_MSG_TYPE::MSG_TX, hash);
    {
        LOCK(net_node::cs_mapRelay);
        //
        // Expire old relay messages
        //
        while (!net_node::vRelayExpiration.empty() && net_node::vRelayExpiration.front().first < bitsystem::GetTime())
        {
            net_node::mapRelay.erase(net_node::vRelayExpiration.front().second);
            net_node::vRelayExpiration.pop_front();
        }

        //
        // Save original serialized message so newer versions are preserved
        //
        net_node::mapRelay.insert(std::make_pair(inv, ss));
        net_node::vRelayExpiration.push_back(std::make_pair(bitsystem::GetTime() + 15 * 60, inv));
    }

    bitrelay::RelayInventory(inv);
}

void CNode::RecordBytesRecv(uint64_t bytes)
{
    LOCK(CNode::cs_totalBytesRecv);
    CNode::nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes)
{
    LOCK(CNode::cs_totalBytesSent);
    CNode::nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv()
{
    LOCK(CNode::cs_totalBytesRecv);
    return CNode::nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent()
{
    LOCK(CNode::cs_totalBytesSent);
    return CNode::nTotalBytesSent;
}

//
// future time
//
int64_t future_time::PoissonNextSend(int64_t nNow, int average_interval_seconds) {
    return nNow + (int64_t)(log1p(bitsystem::GetRand(1ULL << 48) * -0.0000000000000035527136788 /* -1/2^48 */) * average_interval_seconds * -1000000.0 + 0.5);
}
