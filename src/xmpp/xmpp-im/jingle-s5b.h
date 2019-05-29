/*
 * jignle-s5b.h - Jingle SOCKS5 transport
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

/*
 * In s5b.cpp we have
 * S5BManager        -> Jingle::S5B::Manager
 * S5BManager::Item  -> Jingle::S5B::Transport
 * S5BManager::Entry -> ???
 *
 */


#ifndef JINGLE_S5B_H
#define JINGLE_S5B_H

#include "jingle.h"
#include "tcpportreserver.h"

class QHostAddress;
class SocksClient;

namespace XMPP {

class Client;

namespace Jingle {
namespace S5B {

extern const QString NS;

class Transport;
class Candidate {
public:
    enum Type {
        None, // non standard, just a default
        Proxy,
        Tunnel,
        Assisted,
        Direct
    };

    enum {
        ProxyPreference = 10,
        TunnelPreference = 110,
        AssistedPreference = 120,
        DirectPreference = 126
    };

    /**
     * Local candidates states:
     *   Probing      - potential candidate but no ip:port yet. upnp for example
     *   New          - candidate is ready to be sent to remote
     *   Unacked      - candidate is sent to remote but no iq ack yet
     *   Pending      - canidate sent to remote. we have iq ack but no "used" or "error"
     *   Accepted     - we got "candidate-used" for this candidate
     *   Active       - use this candidate for actual data transfer
     *   Discarded    - we got "candidate-error" so all pending were marked Discarded
     *
     * Remote candidates states:
     *   New          - the candidate waits its turn to start connection probing
     *   Probing      - connection probing
     *   Pending      - connection was successful, but we didn't send candidate-used to remote
     *   Unacked      - connection was successful and we sent candidate-used to remote but no iq ack yet
     *   Accepted     - we sent candidate-used and got iq ack
     *   Active       - use this candidate for actual data transfer
     *   Discarded    - failed to connect to all remote candidates
     */
    enum State {
        New,
        Probing,
        Pending,
        Unacked,
        Accepted,
        Active,
        Discarded,
    };

    Candidate();
    Candidate(const QDomElement &el);
    Candidate(const Candidate &other);
    Candidate(const Jid &proxy, const QString &cid, quint16 localPreference = 0);
    Candidate(const TcpPortServer::Ptr &server, const QString &cid, quint16 localPreference = 0);
    ~Candidate();
    Candidate& operator=(const Candidate& other) = default;
    inline bool isValid() const { return d != nullptr; }
    inline operator bool() const { return isValid(); }
    Type type() const;
    QString cid() const;
    Jid jid() const;
    QString host() const;
    void setHost(const QString &host);
    quint16 port() const;
    void setPort(quint16 port);
    quint16 localPort() const;
    QHostAddress localAddress() const;
    State state() const;
    void setState(State s);
    quint32 priority() const;

    QDomElement toXml(QDomDocument *doc) const;

    void connectToHost(const QString &key, State successState, std::function<void (bool)> callback, bool isUdp = false);
    bool incomingConnection(SocksClient *sc);
    SocksClient* takeSocksClient();
    void deleteSocksClient();

    bool operator==(const Candidate &other) const;
    inline bool operator!=(const Candidate &other) const { return !(*this == other); }
private:
    class Private;
    friend class Transport;
    QExplicitlySharedDataPointer<Private> d;
};

class Manager;
class Transport : public XMPP::Jingle::Transport
{
    Q_OBJECT
public:
    enum Mode {
        Tcp,
        Udp
    };

    Transport(const TransportManagerPad::Ptr &pad);
    Transport(const TransportManagerPad::Ptr &pad, const QDomElement &transportEl);
    ~Transport();

    TransportManagerPad::Ptr pad() const override;

    void prepare() override;
    void start() override;
    bool update(const QDomElement &transportEl) override;
    bool hasUpdates() const override;
    OutgoingTransportInfoUpdate takeOutgoingUpdate() override;
    bool isValid() const override;
    Features features() const override;

    QString sid() const;
    Connection::Ptr connection() const;
    size_t blockSize() const;
private:
    friend class S5BServersManager;
    bool incomingConnection(SocksClient *sc);
    bool incomingUDP(bool init, const QHostAddress &addr, int port, const QString &key, const QByteArray &data);

    friend class Manager;
    static QSharedPointer<XMPP::Jingle::Transport> createOutgoing(const TransportManagerPad::Ptr &pad);
    static QSharedPointer<XMPP::Jingle::Transport> createIncoming(const TransportManagerPad::Ptr &pad, const QDomElement &transportEl);

    class Private;
    QScopedPointer<Private> d;
};

class Pad : public TransportManagerPad
{
    Q_OBJECT
    // TODO
public:
    typedef QSharedPointer<Pad> Ptr;

    Pad(Manager *manager, Session *session);
    QString ns() const override;
    Session *session() const override;
    TransportManager *manager() const override;

    QString generateSid() const;
    void registerSid(const QString &sid);

    inline TcpPortScope *discoScope() const { return _discoScope; }
private:
    Manager *_manager;
    Session *_session;
    TcpPortScope *_discoScope;
};

class Manager : public TransportManager {
    Q_OBJECT
public:
    Manager(QObject *parent = nullptr);
    ~Manager();

    XMPP::Jingle::Transport::Features features() const override;
    void setJingleManager(XMPP::Jingle::Manager *jm) override;
    QSharedPointer<XMPP::Jingle::Transport> newTransport(const TransportManagerPad::Ptr &pad) override; // outgoing. one have to call Transport::start to collect candidates
    QSharedPointer<XMPP::Jingle::Transport> newTransport(const TransportManagerPad::Ptr &pad, const QDomElement &transportEl) override; // incoming
    TransportManagerPad* pad(Session *session) override;

    void closeAll() override;

    bool incomingConnection(SocksClient *client, const QString &key); // returns false if key is unknown
    bool incomingUDP(bool init, const QHostAddress &addr, int port, const QString &key, const QByteArray &data);

    QString generateSid(const Jid &remote);
    void registerSid(const Jid &remote, const QString &sid);

    /**
     * @brief userProxy returns custom (set by user) SOCKS proxy JID
     * @return
     */
    Jid userProxy() const;

    /**
     * @brief addKeyMapping sets mapping between key/socks hostname used for direct connection and transport.
     *        The key is sha1(sid, initiator full jid, responder full jid)
     * @param key
     * @param transport
     */
    void addKeyMapping(const QString &key, Transport *transport);
    void removeKeyMapping(const QString &key);
private:
    class Private;
    QScopedPointer<Private> d;
};

} // namespace S5B
} // namespace Jingle
} // namespace XMPP

#endif // JINGLE_S5B_H
