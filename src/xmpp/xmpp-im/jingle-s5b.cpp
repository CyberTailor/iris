﻿/*
 * jignle-s5b.cpp - Jingle SOCKS5 transport
 * Copyright (C) 2019  Sergey Ilinykh
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "jingle-s5b.h"
#include "s5b.h"
#include "xmpp/jid/jid.h"
#include "xmpp_client.h"
#include "xmpp_serverinfomanager.h"
#include "socks.h"

#include <QElapsedTimer>
#include <QTimer>

namespace XMPP {
namespace Jingle {
namespace S5B {

const QString NS(QStringLiteral("urn:xmpp:jingle:transports:s5b:1"));

static QString makeKey(const QString &sid, const Jid &j1, const Jid &j2)
{
    return QString::fromLatin1(QCryptographicHash::hash((sid +
                                                         j1.full() +
                                                         j2.full()).toUtf8(),
                                                        QCryptographicHash::Sha1)
                               .toHex());
}

class Connection : public XMPP::Jingle::Connection
{
    Q_OBJECT

    QList<NetworkDatagram> datagrams;
    SocksClient *client;
    Transport::Mode mode;
public:
    Connection(SocksClient *client, Transport::Mode mode) :
        client(client),
        mode(mode)
    {
        connect(client, &SocksClient::readyRead, this, &Connection::readyRead);
        connect(client, &SocksClient::bytesWritten, this, &Connection::bytesWritten);
        connect(client, &SocksClient::aboutToClose, this, &Connection::aboutToClose);
        if (client->isOpen()) {
            setOpenMode(client->openMode());
        } else {
            qWarning("Creating S5B Transport connection on closed SockClient connection %p", client);
        }
    }

    bool hasPendingDatagrams() const
    {
        return datagrams.size() > 0;
    }

    NetworkDatagram receiveDatagram(qint64 maxSize = -1)
    {
        Q_UNUSED(maxSize); // TODO or not?
        return datagrams.size()? datagrams.takeFirst(): NetworkDatagram();
    }

    qint64 bytesAvailable() const
    {
        if(client)
            return client->bytesAvailable();
        else
            return 0;
    }

    qint64 bytesToWrite() const
    {
        return client->bytesToWrite();
    }

    void close()
    {
        if (client) {
            client->disconnect(this);
        }
        XMPP::Jingle::Connection::close();
        client->deleteLater();
        client = nullptr;
    }

protected:
    qint64 writeData(const char *data, qint64 maxSize)
    {
        if(mode == Transport::Tcp)
            return client->write(data, maxSize);
        return 0;
    }

    qint64 readData(char *data, qint64 maxSize)
    {
        if(client)
            return client->read(data, maxSize);
        else
            return 0;
    }



private:
    friend class Transport;
    void enqueueIncomingUDP(const QByteArray &data)
    {
        datagrams.append(NetworkDatagram{data});
        emit readyRead();
    }
};

class Candidate::Private : public QObject, public QSharedData {
    Q_OBJECT
public:
    ~Private()
    {
        if (server) {
            server->unregisterKey(transport->directAddr());
        }
        delete socksClient;
    }

    Transport *transport;
    QString cid;
    QString host;
    Jid jid;
    quint16 port = 0;
    quint32 priority = 0;
    Candidate::Type type = Candidate::Direct;
    Candidate::State state = Candidate::New;

    QSharedPointer<S5BServer> server;
    SocksClient *socksClient = nullptr;

    void connectToHost(const QString &key, State successState, std::function<void(bool)> callback, bool isUdp)
    {
        socksClient = new SocksClient;
        qDebug() << "connect to host with " << cid << "candidate and socks client" << socksClient;

        connect(socksClient, &SocksClient::connected, [this, callback, successState](){
            state = successState;
            qDebug() << "socks client"  << socksClient << "is connected";
            callback(true);
        });
        connect(socksClient, &SocksClient::error, [this, callback](int error){
            Q_UNUSED(error);
            state = Candidate::Discarded;
            qDebug() << "socks client"  << socksClient << "failed to connect";
            callback(false);
        });
        //connect(&t, SIGNAL(timeout()), SLOT(trySendUDP()));

        socksClient->connectToHost(host, port, key, 0, isUdp);
    }

    void setupIncomingSocksClient()
    {
        connect(socksClient, &SocksClient::error, [this](int error){
            Q_UNUSED(error);
            state = Candidate::Discarded;
        });
    }
};

Candidate::Candidate()
{

}

Candidate::Candidate(Transport *transport, const QDomElement &el)
{
    bool ok;
    QString host(el.attribute(QStringLiteral("host")));
    Jid jid(el.attribute(QStringLiteral("jid")));
    auto portStr = el.attribute(QStringLiteral("port"));
    quint16 port = 0;
    if (!portStr.isEmpty()) {
        port = portStr.toUShort(&ok);
        if (!ok) {
            return; // make the whole candidate invalid
        }
    }
    auto priorityStr = el.attribute(QStringLiteral("priority"));
    if (priorityStr.isEmpty()) {
        return;
    }
    quint32 priority = priorityStr.toUInt(&ok);
    if (!ok) {
        return; // make the whole candidate invalid
    }
    QString cid = el.attribute(QStringLiteral("cid"));
    if (cid.isEmpty()) {
        return;
    }

    QString ct = el.attribute(QStringLiteral("type"));
    if (ct.isEmpty()) {
        ct = QStringLiteral("direct");
    }
    static QMap<QString,Type> types{{QStringLiteral("assisted"), Assisted},
                                    {QStringLiteral("direct"),   Direct},
                                    {QStringLiteral("proxy"),    Proxy},
                                    {QStringLiteral("tunnel"),   Tunnel}
                                   };
    auto candidateType = types.value(ct);
    if (ct.isEmpty() || candidateType == None) {
        return;
    }

    if ((candidateType == Proxy && !jid.isValid()) ||
            (candidateType != Proxy && (host.isEmpty() || !port)))
    {
        return;
    }

    auto d = new Private;
    d->transport = transport;
    d->cid = cid;
    d->host = host;
    d->jid = jid;
    d->port = port;
    d->priority = priority;
    d->type = candidateType;
    d->state = New;
    this->d = d;
}

Candidate::Candidate(const Candidate &other) :
    d(other.d)
{

}

Candidate::Candidate(Transport *transport, const Jid &proxy, const QString &cid, quint16 localPreference) :
    d(new Private)
{
    d->transport = transport;
    d->cid = cid;
    d->jid = proxy;
    d->priority = (ProxyPreference << 16) + localPreference;
    d->type = Proxy;
    d->state = Probing; // it's probing because it's a local side proxy and host and port are unknown
}

Candidate::Candidate(Transport *transport, const TcpPortServer::Ptr &server, const QString &cid, quint16 localPreference) :
    d(new Private)
{
    Type type = None;
    switch (server->portType()) {
    case TcpPortServer::Direct:
        type = Candidate::Direct;
        break;
    case TcpPortServer::NatAssited:
        type = Candidate::Assisted;
        break;
    case TcpPortServer::Tunneled:
        type = Candidate::Tunnel;
        break;
    case TcpPortServer::NoType:
    default:
        type = None;
    }

    if (type == None) {
        d.reset();
        return;
    }

    d->transport = transport;
    d->server = server.staticCast<S5BServer>();
    d->cid = cid;
    d->host = server->publishHost();
    d->port = server->publishPort();
    d->type = type;
    static const int priorities[] = {0, ProxyPreference, TunnelPreference, AssistedPreference, DirectPreference};
    if (type >= Type(0) && type <= Direct) {
        d->priority = (priorities[type] << 16) + localPreference;
    } else {
        d->priority = 0;
    }

    d->state = New;
}

Candidate::~Candidate()
{
}

Candidate::Type Candidate::type() const
{
    return d->type;
}

QString Candidate::cid() const
{
    return d->cid;
}

Jid Candidate::jid() const
{
    return d->jid;
}

QString Candidate::host() const
{
    return d->host;
}

void Candidate::setHost(const QString &host)
{
    d->host = host;
}

quint16 Candidate::port() const
{
    return d->port;
}

void Candidate::setPort(quint16 port)
{
    d->port = port;
}

quint16 Candidate::localPort() const
{
    return d->server ? d->server->serverPort() : 0;
}

QHostAddress Candidate::localAddress() const
{
    return d->server ? d->server->serverAddress() : QHostAddress();
}

Candidate::State Candidate::state() const
{
    return d->state;
}

void Candidate::setState(Candidate::State s)
{
    // don't close sockets here since pending events may change state machine or remote side and closed socket may break it
    d->state = s;
}

quint32 Candidate::priority() const
{
    return d->priority;
}

QDomElement Candidate::toXml(QDomDocument *doc) const
{
    auto e = doc->createElement(QStringLiteral("candidate"));
    e.setAttribute(QStringLiteral("cid"), d->cid);
    if (d->type == Proxy) {
        e.setAttribute(QStringLiteral("jid"), d->jid.full());
    }
    if (!d->host.isEmpty() && d->port) {
        e.setAttribute(QStringLiteral("host"), d->host);
        e.setAttribute(QStringLiteral("port"), d->port);
    }
    e.setAttribute(QStringLiteral("priority"), d->priority);

    static const char *types[] = {"proxy", "tunnel", "assisted"}; // same order as in enum
    if (d->type && d->type < Direct) {
        e.setAttribute(QStringLiteral("type"), QLatin1String(types[d->type - 1]));
    }
    return e;
}

void Candidate::connectToHost(const QString &key, State successState, std::function<void(bool)> callback, bool isUdp)
{
    d->connectToHost(key, successState, callback, isUdp);
}

bool Candidate::incomingConnection(SocksClient *sc)
{
    qDebug() << "incoming connection on" << d->cid << "candidate with socks client" << sc;
    if (d->socksClient) {
        return false;
    }
    d->socksClient = sc;
    d->setupIncomingSocksClient();
    return true;
}

SocksClient *Candidate::takeSocksClient()
{
    qDebug() << "taking socks client" << d->socksClient << "from " << d->cid << "candidate";
    if (!d->socksClient) {
        return nullptr;
    }
    auto c = d->socksClient;
    d->socksClient = nullptr;
    d->disconnect(c);
    return c;
}

void Candidate::deleteSocksClient()
{
    d->socksClient->disconnect();
    delete d->socksClient;
    d->socksClient = nullptr;
}

bool Candidate::operator==(const Candidate &other) const
{
    return d.data() == other.d.data();
}

class Transport::Private
{
public:
    enum PendingActions {
        NewCandidate    = 1,
        CandidateUsed   = 2,
        CandidateError  = 4,
        Activated       = 8,
        ProxyError      = 16
    };

    Transport *q = nullptr;
    Pad::Ptr pad;
    bool meCreator = true; // content created on local side
    bool connectionStarted = false; // where start() was called
    bool waitingAck = true;
    bool aborted = false;
    bool remoteReportedCandidateError = false;
    bool localReportedCandidateError = false;
    bool proxyDiscoveryInProgress = false; // if we have valid proxy requests
    quint16 pendingActions = 0;
    int proxiesInDiscoCount = 0;
    Application *application = nullptr;
    QMap<QString,Candidate> localCandidates; // cid to candidate mapping
    QMap<QString,Candidate> remoteCandidates;
    Candidate localUsedCandidate; // we received "candidate-used" for this candidate from localCandidates list
    Candidate remoteUsedCandidate; // we sent "candidate-used" for this candidate from remoteCandidates list
    QString dstaddr; // an address for xmpp proxy as it comes from remote. each side calculates it like sha1(sid + local jid + remote jid)
    QString directAddr; // like dstaddr but for direct connection. Basically it's sha1(sid + initiator jid + responder jid)
    QString sid;
    Transport::Mode mode = Transport::Tcp;
    QTimer probingTimer;
    QElapsedTimer lastConnectionStart;
    size_t blockSize = 8192;
    TcpPortDiscoverer *disco = nullptr;

    QSharedPointer<Connection> connection;

    // udp stuff
    bool udpInitialized;
    quint16 udpPort;
    QHostAddress udpAddress;


    inline Jid remoteJid() const
    {
        return pad->session()->peer();
    }

    QString generateCid() const
    {
        QString cid;
        do {
            cid = QString("%1").arg(qrand() & 0xffff, 4, 16, QChar('0'));
        } while (localCandidates.contains(cid) || remoteCandidates.contains(cid));
        return cid;
    }

    bool isDup(const Candidate &c) const
    {
        for (auto const &rc: remoteCandidates) {
            if (c.host() == rc.host() && c.port() == rc.port()) {
                return true;
            }
        }
        return false;
    }

    void tryConnectToRemoteCandidate()
    {
        if (!connectionStarted) {
            return; // will come back later
        }
        quint64 maxProbingPrio = 0;
        quint64 maxNewPrio = 0;
        Candidate maxProbing;
        QList<Candidate> maxNew;

        /*
         We have to find highest-priority already connecting candidate and highest-priority new candidate.
         If already-connecting is not found then start connecting to new if it's found.
         If both already-connecting and new are found then
            if new candidate has higher priority or the same priority then start connecting
            else ensure the new candidate starts connecting in 200ms after previous connection attempt
                 (if it's in future then reschedule this call for future)
         In all the other cases just return and wait for events.
        */

        for (auto &c: remoteCandidates) {
            if (c.state() == Candidate::New) {
                if (c.priority() > maxNewPrio) {
                    maxNew = QList<Candidate>();
                    maxNew.append(c);
                    maxNewPrio = c.priority();
                } else if (c.priority() == maxNewPrio) {
                    maxNew.append(c);
                }
            }
            if (c.state() == Candidate::Probing && c.priority() > maxProbingPrio) {
                maxProbing = c;
                maxProbingPrio = c.priority();
            }
        }
        if (maxNew.isEmpty()) {
            return; // nowhere to connect
        }

        if (maxProbing) {
            if (maxNewPrio < maxProbing.priority()) {
                if (probingTimer.isActive()) {
                    return; // we will come back here soon
                }
                qint64 msToFuture = 200 - lastConnectionStart.elapsed();
                if (msToFuture > 0) { // seems like we have to rescheduler for future
                    probingTimer.start(int(msToFuture));
                    return;
                }
            }
        }

        // now we have to connect to maxNew candidates
        for (auto &mnc: maxNew) {
            lastConnectionStart.start();
            QString key = mnc.type() == Candidate::Proxy? dstaddr : directAddr;
            mnc.setState(Candidate::Probing);
            mnc.connectToHost(key, Candidate::Pending, [this, mnc](bool success) {
                // candidate's status had to be changed by connectToHost, so we don't set it again
                if (success) {
                    // let's reject candidates which are meaningless to try
                    bool hasUnckeckedNew = false;
                    for (auto &c: remoteCandidates) {
                        if (c.state() == Candidate::New) {
                            if (c.priority() <= mnc.priority()) {
                                c.setState(Candidate::Discarded);
                            } else {
                                hasUnckeckedNew = true;
                            }
                        }
                    }
                    if (!hasUnckeckedNew) {
                        pendingActions &= ~Private::NewCandidate; // just if we had it for example after proxy discovery
                    }
                    setLocalProbingMinimalPreference(mnc.priority() >> 16);
                    updateMinimalPriority();
                }
                checkAndFinishNegotiation();
            }, mode == Transport::Udp);
        }
    }

    /**
     * @brief limitTcpDiscoByMinimalPreference take upper part of candidate preference (type preference)
     *        and drops lower priority pending local servers disco
     * @param preference
     */
    void setLocalProbingMinimalPreference(quint32 preference)
    {
        if (proxyDiscoveryInProgress && preference > Candidate::ProxyPreference) {
            proxyDiscoveryInProgress = false; // doesn't make sense anymore
        }

        // and now local ports discoverer..
        if (!disco) {
            return;
        }
        TcpPortServer::PortTypes types = TcpPortServer::Direct;
        if (preference >= Candidate::AssistedPreference) {
            types |= TcpPortServer::NatAssited;
        }
        if (preference >= Candidate::TunnelPreference) {
            types |= TcpPortServer::Tunneled;
        }
        if (!disco->setTypeMask(types)) {
            delete disco;
            disco = nullptr;
        }
    }

    bool hasUnaknowledgedLocalCandidates() const
    {
        // now ensure all local were sent to remote and no hope left
        if (proxyDiscoveryInProgress || (disco && !disco->isDepleted())) {
            return true;
        }

        // now local candidates
        for (const auto &c: localCandidates) {
            auto s = c.state();
            if (s == Candidate::Probing || s == Candidate::New || s == Candidate::Unacked) {
                return true;
            }
        }

        return false;
    }

    Candidate preferredCandidate() const
    {
        if (localUsedCandidate) {
            if (remoteUsedCandidate) {
                if (localUsedCandidate.priority() == remoteUsedCandidate.priority()) {
                    if (pad->session()->role() == Origin::Initiator) {
                        return remoteUsedCandidate;
                    }
                    return localUsedCandidate;
                }
                return localUsedCandidate.priority() > remoteUsedCandidate.priority()?
                            localUsedCandidate : remoteUsedCandidate;
            }
            return localUsedCandidate;
        }
        return remoteUsedCandidate;
    }

    void checkAndFinishNegotiation()
    {
        // Why we can't send candidate-used/error right when this happens:
        // so the situation: we discarded all remote candidates (failed to connect)
        // but we have some local candidates which are still in Probing state (upnp for example)
        // if we send candidate-error while we have unsent candidates this may trigger transport failure.
        // So for candidate-error two conditions have to be met 1) all remote failed 2) all local were sent no more
        // local candidates are expected to be discovered

        if (!connectionStarted || connection) { // if not started or already finished
            return;
        }

        // sort out already handled states or states which will bring us here a little later
        if (waitingAck || pendingActions || hasUnaknowledgedLocalCandidates())
        {
            // waitingAck some query waits for ack and in the callback this func will be called again
            // pendingActions means we reported to app we have data to send but the app didn't take this data yet,
            // but as soon as it's taken it will switch to waitingAck.
            // And with unacknowledged local candidates we can't send used/error as well as report connected()/failure()
            // until tried them all
            return;
        }

        // if we already sent used/error. In other words if we already have finished local part of negotiation
        if (localReportedCandidateError || remoteUsedCandidate) {
            // maybe it's time to report connected()/failure()
            if (remoteReportedCandidateError || localUsedCandidate) {
                // so remote seems to be finished too.
                // tell application about it and it has to change its state immediatelly
                auto c = preferredCandidate();
                if (c) {
                    if (c.state() != Candidate::Active) {
                        if (c.type() == Candidate::Proxy && c == localUsedCandidate) { // local proxy
                            // If it's proxy, first it has to be activated
                            if (localUsedCandidate) {
                                // it's our side who proposed proxy. so we have to connect to it and activate
                                auto key = makeKey(sid, pad->session()->me(), pad->session()->peer());
                                c.connectToHost(key, Candidate::Active, [this](bool success){
                                    if (success) {
                                        pendingActions |= Private::Activated;
                                    } else {
                                        pendingActions |= Private::ProxyError;
                                    }
                                    emit q->updated();
                                }, mode == Transport::Udp);
                            } // else so it's remote proxy. let's just wait for <activated> from remote
                        } else {
                            c.setState(Candidate::Active);
                        }
                    }
                    if (c.state() == Candidate::Active) {
                        handleConnected(c);
                    }
                } else { // both sides reported candidate error
                    emit q->failed();
                }
            } // else we have to wait till remote reports its status
            return;
        }

        // if we are here then neither candidate-used nor candidate-error was sent to remote,
        // but we can send it now.
        // first let's check if we can send candidate-used
        bool allRemoteDiscarded = true;
        bool hasConnectedRemoteCandidate = false;
        for (const auto &c: remoteCandidates) {
            auto s = c.state();
            if (s != Candidate::Discarded) {
                allRemoteDiscarded = false;
            }
            if (s == Candidate::Pending) { // connected but not yet sent
                hasConnectedRemoteCandidate = true;
            }
        }

        // if we have connection to remote candidate it's time to send it
        if (hasConnectedRemoteCandidate) {
            pendingActions |= Private::CandidateUsed;
            emit q->updated();
            return;
        }

        if (allRemoteDiscarded) {
            pendingActions |= Private::CandidateError;
            emit q->updated();
            return;
        }

        // apparently we haven't connected anywhere but there are more remote candidates to try
    }

    // take used-candidate with highest priority and discard all with lower. also update used candidates themselves
    void updateMinimalPriority() {
        quint32 prio = 0;
        if (localUsedCandidate && localUsedCandidate.state() != Candidate::Discarded) {
            prio = localUsedCandidate.priority();
        }
        if (remoteUsedCandidate && prio < remoteUsedCandidate.priority() && remoteUsedCandidate.state() != Candidate::Discarded) {
            prio = remoteUsedCandidate.priority();
        }

        for (auto &c: localCandidates) {
            if (c.priority() < prio && c.state() != Candidate::Discarded) {
                c.setState(Candidate::Discarded);
            }
        }
        for (auto &c: remoteCandidates) {
            if (c.priority() < prio && c.state() != Candidate::Discarded) {
                c.setState(Candidate::Discarded);
            }
        }
        prio >>= 16;
        setLocalProbingMinimalPreference(prio);
        // if we discarded "used" candidates then reset them to invalid
        if (localUsedCandidate && localUsedCandidate.state() == Candidate::Discarded) {
            localUsedCandidate = Candidate();
        }
        if (remoteUsedCandidate && remoteUsedCandidate.state() == Candidate::Discarded) {
            remoteUsedCandidate = Candidate();
        }
        if (localUsedCandidate && remoteUsedCandidate) {
            if (pad->session()->role() == Origin::Initiator) {
                // i'm initiator. see 2.4.4
                localUsedCandidate.setState(Candidate::Discarded);
                localUsedCandidate = Candidate();
                remoteReportedCandidateError = true; // as a sign of completeness even if not true
            } else {
                remoteUsedCandidate.setState(Candidate::Discarded);
                remoteUsedCandidate = Candidate();
                localReportedCandidateError = true; // as a sign of completeness even if not true
            }
        }

        // now check and reset NewCandidate pending action
        bool haveNewCandidates = false;
        for (auto &c: remoteCandidates) {
            if (c.state() == Candidate::New) {
                haveNewCandidates = true;
                break;
            }
        }
        if (!haveNewCandidates) {
            pendingActions &= ~NewCandidate;
        }
    }

    void onLocalServerDiscovered()
    {
        for (auto serv: disco->takeServers()) {
            auto s5bserv = serv.staticCast<S5BServer>();
            s5bserv->registerKey(directAddr);
            Candidate c(q, serv, generateCid());
            if (c.isValid() && !isDup(c) && c.priority()) {
                localCandidates.insert(c.cid(), c);
                pendingActions |= NewCandidate;
            }
        }
    }

    void handleConnected(Candidate &connCand)
    {
        connection.reset(new Connection(connCand.takeSocksClient(), mode));
        probingTimer.stop();
        for (auto &rc: remoteCandidates) {
            if (rc != connCand && rc.state() == Candidate::Probing) {
                rc.deleteSocksClient();
            }
        }
        QTimer::singleShot(0, q, [this](){
            localCandidates.clear();
            remoteCandidates.clear();
            emit q->connected();
        });
    }
};

Transport::Transport(const TransportManagerPad::Ptr &pad) :
    d(new Private)
{
    d->q = this;
    d->pad = pad.staticCast<Pad>();
    d->probingTimer.setSingleShot(true);
    connect(&d->probingTimer, &QTimer::timeout, [this](){ d->tryConnectToRemoteCandidate(); });
    connect(pad->manager(), &TransportManager::abortAllRequested, this, [this](){
        d->aborted = true;
        emit failed();
    });
}

Transport::Transport(const TransportManagerPad::Ptr &pad, const QDomElement &transportEl) :
    Transport::Transport(pad)
{
    d->meCreator = false;
    d->dstaddr = transportEl.attribute(QStringLiteral("dstaddr"));
    d->sid = transportEl.attribute(QStringLiteral("sid"));
    if (d->sid.isEmpty() || !update(transportEl)) {
        d.reset();
        return;
    }
}

Transport::~Transport()
{
    if (d) {
        static_cast<Manager*>(d->pad->manager())->removeKeyMapping(d->directAddr);
    }
}

TransportManagerPad::Ptr Transport::pad() const
{
    return d->pad.staticCast<TransportManagerPad>();
}

void Transport::prepare()
{
    auto m = static_cast<Manager*>(d->pad->manager());
    if (d->meCreator) {
        d->sid = d->pad->generateSid();
    }
    d->pad->registerSid(d->sid);
    d->directAddr = makeKey(d->sid, d->pad->session()->initiator(), d->pad->session()->responder());
    m->addKeyMapping(d->directAddr, this);

    auto scope = d->pad->discoScope();
    d->disco = scope->disco(); // FIXME store and handle signale. delete when not needed

    connect(d->disco, &TcpPortDiscoverer::portAvailable, this, [this](){
        d->onLocalServerDiscovered();
    });
    d->onLocalServerDiscovered();

    Jid proxy = m->userProxy();
    if (proxy.isValid()) {
        Candidate c(this, proxy, d->generateCid());
        if (!d->isDup(c)) {
            d->localCandidates.insert(c.cid(), c);
        }
    }

    d->proxyDiscoveryInProgress = true;
    QList<QSet<QString>> featureOptions = {{"http://jabber.org/protocol/bytestreams"}};
    d->pad->session()->manager()->client()->serverInfoManager()->
            queryServiceInfo(QStringLiteral("proxy"),
                             QStringLiteral("bytestreams"),
                             featureOptions,
                             QRegExp("proxy.*|socks.*|stream.*|s5b.*"),
                             ServerInfoManager::SQ_CheckAllOnNoMatch,
                             [this](const QList<DiscoItem> &items)
    {
        if (!d->proxyDiscoveryInProgress) { // check if new results are ever/still expected
            // seems like we have successful connection via higher priority channel. so nobody cares about proxy
            return;
        }
        auto m = static_cast<Manager*>(d->pad->manager());
        Jid userProxy = m->userProxy();

        // queries proxy's host/port and sends the candidate to remote
        auto queryProxy = [this](const Jid &j, const QString &cid) {
            d->proxiesInDiscoCount++;
            auto query = new JT_S5B(d->pad->session()->manager()->client()->rootTask());
            connect(query, &JT_S5B::finished, this, [this,query,cid](){
                if (!d->proxyDiscoveryInProgress) {
                    return;
                }
                bool candidateUpdated = false;
                auto c = d->localCandidates.value(cid);
                if (c && c.state() == Candidate::Probing) {
                    auto sh = query->proxyInfo();
                    if (query->success() && !sh.host().isEmpty() && sh.port()) {
                        // it can be discarded by this moment (e.g. got success on a higher priority candidate).
                        // so we have to check.
                        c.setHost(sh.host());
                        c.setPort(sh.port());
                        c.setState(Candidate::New);
                        candidateUpdated = true;
                        d->pendingActions |= Private::NewCandidate;
                    } else {
                        c.setState(Candidate::Discarded);
                    }
                }
                d->proxiesInDiscoCount--;
                if (!d->proxiesInDiscoCount) {
                    d->proxyDiscoveryInProgress = false;
                }
                if (candidateUpdated) {
                    emit updated();
                } else if (!d->proxiesInDiscoCount) {
                    // it's possible it was our last hope and probaby we have to send candidate-error now.
                    d->checkAndFinishNegotiation();
                }
            });
            query->requestProxyInfo(j);
            query->go(true);
        };

        bool userProxyFound = !userProxy.isValid();
        for (const auto i: items) {
            int localPref = 0;
            if (!userProxyFound && i.jid() == userProxy) {
                localPref = 1;
                userProxyFound = true;
            }
            Candidate c(this, i.jid(), d->generateCid(), localPref);
            d->localCandidates.insert(c.cid(), c);

            queryProxy(i.jid(), c.cid());
        }
        if (!userProxyFound) {
            Candidate c(this, userProxy, d->generateCid(), 1);
            d->localCandidates.insert(c.cid(), c);
            queryProxy(userProxy, c.cid());
        } else if (items.count() == 0) {
            // seems like we don't have any proxy
            d->proxyDiscoveryInProgress = false;
            d->checkAndFinishNegotiation();
        }
    });

    // TODO nat-assisted candidates..
    emit updated();
}

// we got content acceptance from any side and not can connect
void Transport::start()
{
    d->connectionStarted = true;
    d->tryConnectToRemoteCandidate();
    // if there is no higher priority candidates than ours but they are already connected then
    d->checkAndFinishNegotiation();
}

bool Transport::update(const QDomElement &transportEl)
{
    // we can just on type of elements in transport-info
    // so return as soon as any type handled. Though it leaves a room for  remote to send invalid transport-info
    auto bs = transportEl.attribute(QString::fromLatin1("block-size"));
    if (!bs.isEmpty()) {
        size_t bsn = bs.toULongLong();
        if (bsn && bsn <= d->blockSize) {
            d->blockSize = bsn;
        }
    }
    QString candidateTag(QStringLiteral("candidate"));
    int candidatesAdded = 0;
    for(QDomElement ce = transportEl.firstChildElement(candidateTag);
        !ce.isNull(); ce = ce.nextSiblingElement(candidateTag)) {
        Candidate c(this, ce);
        if (!c) {
            return false;
        }
        d->remoteCandidates.insert(c.cid(), c); // TODO check for collisions!
        candidatesAdded++;
    }
    if (candidatesAdded) {
        d->pendingActions &= ~Private::CandidateError;
        d->localReportedCandidateError = false;
        QTimer::singleShot(0, this, [this](){
            d->tryConnectToRemoteCandidate();
        });
        return true;
    }

    QDomElement el = transportEl.firstChildElement(QStringLiteral("candidate-used"));
    if (!el.isNull()) {
        auto cUsed = d->localCandidates.value(el.attribute(QStringLiteral("cid")));
        if (!cUsed) {
            return false;
        }
        if (cUsed.state() == Candidate::Pending) {
            cUsed.setState(Candidate::Accepted);
            d->localUsedCandidate = cUsed;
            d->updateMinimalPriority();
            QTimer::singleShot(0, this, [this](){ d->checkAndFinishNegotiation(); });
        } else {
            //seems like we already rejected the candidate and either remote side already know about it or will soon
            d->localUsedCandidate = Candidate();
            d->remoteReportedCandidateError = true; // as a sign remote has finished
        }
        return true;
    }

    el = transportEl.firstChildElement(QStringLiteral("candidate-error"));
    if (!el.isNull()) {
        d->remoteReportedCandidateError = true;
        for (auto &c: d->localCandidates) {
            if (c.state() == Candidate::Pending) {
                c.setState(Candidate::Discarded);
            }
        }
        QTimer::singleShot(0, this, [this](){ d->checkAndFinishNegotiation(); });
        return true;
    }

    el = transportEl.firstChildElement(QStringLiteral("activated"));
    if (!el.isNull()) {
        auto c = d->localCandidates.value(el.attribute(QStringLiteral("cid")));
        if (!c) {
            return false;
        }
        if (!(c.type() == Candidate::Proxy && c.state() == Candidate::Accepted && c == d->localUsedCandidate)) {
            qDebug("Received <activated> on a candidate in an inappropriate state. Ignored.");
            return true;
        }
        c.setState(Candidate::Active);
        d->handleConnected(c);
        return true;
    }

    el = transportEl.firstChildElement(QStringLiteral("proxy-error"));
    if (!el.isNull()) {
        auto c = d->localCandidates.value(el.attribute(QStringLiteral("cid")));
        if (!c) {
            return false;
        }
        if (c != d->localUsedCandidate || c.state() != Candidate::Accepted) {
            qDebug("Received <proxy-error> on a candidate in an inappropriate state. Ignored.");
            return true;
        }

        // if we got proxy-error then the transport has to be considered failed according to spec
        // so never send proxy-error while we have unaknowledged local non-proxy candidates,
        // but we have to follow the standard.

        // Discard everything
        for (auto &c: d->localCandidates) {
            c.setState(Candidate::Discarded);
        }
        for (auto &c: d->remoteCandidates) {
            c.setState(Candidate::Discarded);
        }
        d->proxyDiscoveryInProgress = false;
        delete d->disco;

        QTimer::singleShot(0, this, [this](){ emit failed(); });
        return true;
    }

    return false;
}

bool Transport::hasUpdates() const
{
    return isValid() && d->pendingActions;
}

OutgoingTransportInfoUpdate Transport::takeOutgoingUpdate()
{
    OutgoingTransportInfoUpdate upd;
    if (!isValid()) {
        return upd;
    }

    auto doc = d->pad->session()->manager()->client()->doc();

    QDomElement tel = doc->createElementNS(NS, "transport");
    tel.setAttribute(QStringLiteral("sid"), d->sid);
    if (d->meCreator && d->mode != Tcp) {
        tel.setAttribute(QStringLiteral("mode"), "udp");
    }
    tel.setAttribute(QString::fromLatin1("block-size"), qulonglong(d->blockSize));

    if (d->pendingActions & Private::NewCandidate) {
        d->pendingActions &= ~Private::NewCandidate;
        bool useProxy = false;
        QList<Candidate> candidatesToSend;
        for (auto &c: d->localCandidates) {
            if (c.state() != Candidate::New) {
                continue;
            }
            if (c.type() == Candidate::Proxy) {
                useProxy = true;
            }
            tel.appendChild(c.toXml(doc));
            candidatesToSend.append(c);
            c.setState(Candidate::Unacked);
        }
        if (useProxy) {
            QString dstaddr = makeKey(d->sid, d->pad->session()->me(), d->pad->session()->peer());
            tel.setAttribute(QStringLiteral("dstaddr"), dstaddr);
        }
        if (!candidatesToSend.isEmpty()) {
            d->waitingAck = true;
            upd = OutgoingTransportInfoUpdate{tel, [this, candidatesToSend]() mutable {
                d->waitingAck = false;
                for (auto &c: candidatesToSend) {
                    if (c.state() == Candidate::Unacked) {
                        c.setState(Candidate::Pending);
                    }
                }
                d->checkAndFinishNegotiation();
            }};
        } else {
            qWarning("Got NewCandidate pending action but no candidate to send");
        }
    } else if (d->pendingActions & Private::CandidateUsed) {
        d->pendingActions &= ~Private::CandidateUsed;
        // we should have the only remote candidate in Pending state.
        // all other has to be discarded by priority check
        for (auto &c: d->remoteCandidates) {
            if (c.state() != Candidate::Pending) {
                continue;
            }
            auto el = tel.appendChild(doc->createElement(QStringLiteral("candidate-used"))).toElement();
            el.setAttribute(QStringLiteral("cid"), c.cid());
            c.setState(Candidate::Unacked);

            d->waitingAck = true;
            upd = OutgoingTransportInfoUpdate{tel, [this, c]() mutable {
                d->waitingAck = false;
                if (c.state() == Candidate::Unacked) {
                    c.setState(Candidate::Accepted);
                    d->remoteUsedCandidate = c;
                }
                d->checkAndFinishNegotiation();
            }};

            break;
        }
        if (std::get<0>(upd).isNull()) {
            qWarning("Got CandidateUsed pending action but no pending candidates");
        }
    } else if (d->pendingActions & Private::CandidateError) {
        d->pendingActions &= ~Private::CandidateError;
        // we are here because all remote are already in Discardd state
        tel.appendChild(doc->createElement(QStringLiteral("candidate-error")));
        d->waitingAck = true;
        upd = OutgoingTransportInfoUpdate{tel, [this]() mutable {
            d->waitingAck = false;
            d->localReportedCandidateError = true;
            d->checkAndFinishNegotiation();
        }};
    } else if (d->pendingActions & Private::Activated) {
        d->pendingActions &= ~Private::Activated;
        if (d->localUsedCandidate) {
            auto cand = d->localUsedCandidate;
            auto el = tel.appendChild(doc->createElement(QStringLiteral("activated"))).toElement();
            el.setAttribute(QStringLiteral("cid"), cand.cid());
            d->waitingAck = true;
            upd = OutgoingTransportInfoUpdate{tel, [this, cand]() mutable {
                d->waitingAck = false;
                if (cand.state() != Candidate::Accepted || d->localUsedCandidate != cand) {
                    return; // seems like state was changed while we was waiting for an ack
                }
                cand.setState(Candidate::Active);
                d->checkAndFinishNegotiation();
            }};
        }
    } else if (d->pendingActions & Private::ProxyError) {
        // we send proxy error only for local proxy
        d->pendingActions &= ~Private::ProxyError;
        if (d->localUsedCandidate) {
            auto cand = d->localUsedCandidate;
            tel.appendChild(doc->createElement(QStringLiteral("proxy-error")));
            d->waitingAck = true;
            upd = OutgoingTransportInfoUpdate{tel, [this, cand]() mutable {
                d->waitingAck = false;
                if (cand.state() != Candidate::Accepted || d->localUsedCandidate != cand) {
                    return; // seems like state was changed while we was waiting for an ack
                }
                cand.setState(Candidate::Discarded);
                d->localUsedCandidate = Candidate();
                emit failed();
            }};
        } else {
            qWarning("Got ProxyError pending action but no local used candidate is not set");
        }
    }

    return upd; // TODO
}

bool Transport::isValid() const
{
    return d != nullptr;
}

Transport::Features Transport::features() const
{
    return Features(HardToConnect | Reliable | Fast);
}

QString Transport::sid() const
{
    return d->sid;
}

QString Transport::directAddr() const
{
    return d->directAddr;
}

Connection::Ptr Transport::connection() const
{
    return d->connection.staticCast<XMPP::Jingle::Connection>();
}

size_t Transport::blockSize() const
{
    return d->blockSize;
}

bool Transport::incomingConnection(SocksClient *sc)
{
    if (!d->connection) {
        auto s = sc->abstractSocket();
        for (auto &c: d->localCandidates) {
            if (s->localPort() == c.localPort() &&
                    (c.state() == Candidate::Pending || c.state() == Candidate::Unacked) &&
                    c.incomingConnection(sc))
            {

                if(d->mode == Transport::Udp)
                    sc->grantUDPAssociate("", 0);
                else
                    sc->grantConnect();
                // we can also remember the server it comes from. static_cast<S5BServer *>(sender())
                return true;
            }
        }
    }

    sc->requestDeny();
    sc->deleteLater();
    return false;
}

bool Transport::incomingUDP(bool init, const QHostAddress &addr, int port, const QString &key, const QByteArray &data)
{
    if (d->mode != Transport::Mode::Udp) {
        return false;
    }

    if(init) {
        // TODO probably we could create a Connection here and put all the params inside
        if(d->udpInitialized)
            return false; // only init once

        // lock on to this sender
        d->udpAddress = addr;
        d->udpPort = port;
        d->udpInitialized = true;

        // reply that initialization was successful
        d->pad->session()->manager()->client()->s5bManager()->jtPush()->sendUDPSuccess(d->pad->session()->peer(), key); // TODO fix ->->->
        return true;
    }

    // not initialized yet?  something went wrong
    if(!d->udpInitialized)
        return false;

    // must come from same source as when initialized
    if(addr != d->udpAddress || port != d->udpPort)
        return false;

    d->connection->enqueueIncomingUDP(data); // man_udpReady
    return true;
}

//----------------------------------------------------------------
// Manager
//----------------------------------------------------------------

class Manager::Private
{
public:
    XMPP::Jingle::Manager *jingleManager = nullptr;

    // FIMME it's reuiqred to split transports by direction otherwise we gonna hit conflicts.
    // jid,transport-sid -> transport mapping
    QSet<QPair<Jid,QString>> sids;
    QHash<QString,Transport*> key2transport;
    Jid proxy;
};

Manager::Manager(QObject *parent) :
    TransportManager(parent),
    d(new Private)
{
    // ensure S5BManager is initialized
    QTimer::singleShot(0, [this](){
        auto jt = d->jingleManager->client()->s5bManager()->jtPush();
        connect(jt, &JT_PushS5B::incomingUDPSuccess, this, [this](const Jid &from, const QString &dstaddr) {
            Q_UNUSED(from);
            auto t = d->key2transport.value(dstaddr);
            if (t) {
                // TODO return t->incomingUDPSuccess(from);
            }
        });
    });
}

Manager::~Manager()
{
    d->jingleManager->unregisterTransport(NS);
}

Transport::Features Manager::features() const
{
    return Transport::Reliable | Transport::Fast;
}

void Manager::setJingleManager(XMPP::Jingle::Manager *jm)
{
    d->jingleManager = jm;
}

QSharedPointer<XMPP::Jingle::Transport> Manager::newTransport(const TransportManagerPad::Ptr &pad)
{
    return QSharedPointer<XMPP::Jingle::Transport>(new Transport(pad));
}

QSharedPointer<XMPP::Jingle::Transport> Manager::newTransport(const TransportManagerPad::Ptr &pad, const QDomElement &transportEl)
{
    auto t = new Transport(pad, transportEl);
    QSharedPointer<XMPP::Jingle::Transport> ret(t);
    if (t->isValid()) {
        return ret;
    }
    return QSharedPointer<XMPP::Jingle::Transport>();
}

TransportManagerPad* Manager::pad(Session *session)
{
    return new Pad(this, session);
}

void Manager::closeAll()
{
    emit abortAllRequested();
}

void Manager::addKeyMapping(const QString &key, Transport *transport)
{
    d->key2transport.insert(key, transport);
}

void Manager::removeKeyMapping(const QString &key)
{
    d->key2transport.remove(key);
}

bool Manager::incomingConnection(SocksClient *client, const QString &key)
{
    auto t = d->key2transport.value(key);
    if (t) {
        return t->incomingConnection(client);
    }
    return false;
}

bool Manager::incomingUDP(bool init, const QHostAddress &addr, int port, const QString &key, const QByteArray &data)
{
    auto t = d->key2transport.value(key);
    if (t) {
        return t->incomingUDP(init, addr, port, key, data);
    }
    return false;
}

QString Manager::generateSid(const Jid &remote)
{
    QString sid;
    QPair<Jid,QString> key;
    do {
        sid = QString("s5b_%1").arg(qrand() & 0xffff, 4, 16, QChar('0'));
        key = qMakePair(remote, sid);
    } while (d->sids.contains(key));
    return sid;

    // TODO check key in servers
    //QList<TcpPortServer*> servers = d->jingleManager->client()->tcpPortReserver()->scope(QString::fromLatin1("s5b"))
    //        ->allServers();
}

void Manager::registerSid(const Jid &remote, const QString &sid)
{
    d->sids.insert(qMakePair(remote, sid));
}

Jid Manager::userProxy() const
{
    return d->proxy;
}

//----------------------------------------------------------------
// Pad
//----------------------------------------------------------------
Pad::Pad(Manager *manager, Session *session) :
    _manager(manager),
    _session(session)
{
    auto reserver = _session->manager()->client()->tcpPortReserver();
    _discoScope = reserver->scope(QString::fromLatin1("s5b"));
}

QString Pad::ns() const
{
    return NS;
}

Session *Pad::session() const
{
    return _session;
}

TransportManager *Pad::manager() const
{
    return _manager;
}

QString Pad::generateSid() const
{
    return _manager->generateSid(_session->peer());
}

void Pad::registerSid(const QString &sid)
{
    return _manager->registerSid(_session->peer(), sid);
}

} // namespace S5B
} // namespace Jingle
} // namespace XMPP

#include "jingle-s5b.moc"
