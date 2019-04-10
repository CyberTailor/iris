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

class SocksClient;

namespace XMPP {

class Client;
class S5BServer;

namespace Jingle {
namespace S5B {

extern const QString NS;

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
     *   Discarded    - we got "candidate-error" so all pending were marked Discarded
     *
     * Remote candidates states:
     *   New          - the candidate waits its turn to start connection probing
     *   Probing      - connection probing
     *   Accepted     - connection was successful
     *   Discarded    - failed to connect to all remote candidates
     */
    enum State {
        New,
        Probing,
        Unacked,
        Pending,
        Accepted,
        Discarded,
    };

    Candidate();
    Candidate(const QDomElement &el);
    Candidate(const Candidate &other);
    Candidate(const Jid &proxy, const QString &cid);
    Candidate(const QString &host, quint16 port, const QString &cid, Type type, quint16 localPreference = 0);
    ~Candidate();
    inline bool isValid() const { return d != nullptr; }
    inline operator bool() const { return isValid(); }
    Type type() const;
    QString cid() const;
    Jid jid() const;
    QString host() const;
    void setHost(const QString &host);
    quint16 port() const;
    void setPort(quint16 port);
    State state() const;
    void setState(State s);
    quint32 priority() const;

    QDomElement toXml(QDomDocument *doc) const;

private:
    class Private;
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
    void setApplication(Application *app) override;

    void prepare() override;
    void start() override;
    bool update(const QDomElement &transportEl) override;
    Action outgoingUpdateType() const override;
    OutgoingUpdate takeOutgoingUpdate() override;
    bool isValid() const override;
    Features features() const override;

    QString sid() const;

    bool incomingConnection(SocksClient *sc, const QString &key);

private:
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
private:
    Manager *_manager;
    Session *_session;
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

    void setServer(S5BServer *serv);
    bool incomingConnection(SocksClient *client, const QString &key); // returns false if key is unknown

    QString generateSid(const Jid &remote);
    void registerSid(const Jid &remote, const QString &sid);

    S5BServer* socksServ() const;
    Jid userProxy() const;
private:
    class Private;
    QScopedPointer<Private> d;
};

} // namespace S5B
} // namespace Jingle
} // namespace XMPP

#endif // JINGLE_S5B_H
