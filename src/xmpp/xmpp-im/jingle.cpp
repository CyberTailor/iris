/*
 * jignle.cpp - General purpose Jingle
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

#include "jingle.h"
#include "xmpp_xmlcommon.h"
#include "xmpp/jid/jid.h"
#include "xmpp-im/xmpp_hash.h"
#include "xmpp_client.h"
#include "xmpp_task.h"
#include "xmpp_stream.h"

#include <QDateTime>
#include <QDomElement>
#include <QMap>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <QDebug>

namespace XMPP {
namespace Jingle {

const QString NS(QStringLiteral("urn:xmpp:jingle:1"));


//----------------------------------------------------------------------------
// Jingle
//----------------------------------------------------------------------------
static const struct {
    const char *text;
    Action action;
} jingleActions[] = {
{ "content-accept",     Action::ContentAccept },
{ "content-add",        Action::ContentAdd },
{ "content-modify",     Action::ContentModify },
{ "content-reject",     Action::ContentReject },
{ "content-remove",     Action::ContentRemove },
{ "description-info",   Action::DescriptionInfo },
{ "security-info",      Action::SecurityInfo },
{ "session-accept",     Action::SessionAccept },
{ "session-info",       Action::SessionInfo },
{ "session-initiate",   Action::SessionInitiate },
{ "session-terminate",  Action::SessionTerminate },
{ "transport-accept",   Action::TransportAccept },
{ "transport-info",     Action::TransportInfo },
{ "transport-reject",   Action::TransportReject },
{ "transport-replace",  Action::TransportReplace }
};

class Jingle::Private : public QSharedData
{
public:
    Action action;
    QString sid;
    Jid initiator;
    Jid responder;
};

Jingle::Jingle()
{

}

Jingle::Jingle(Action action, const QString &sid) :
    d(new Private)
{
    d->action = action;
    d->sid = sid;
}

Jingle::Jingle(const QDomElement &e)
{
    QString actionStr = e.attribute(QLatin1String("action"));
    Action action;
    QString sid = e.attribute(QLatin1String("sid"));
    Jid initiator;
    Jid responder;


    bool found = false;
    for (unsigned int i = 0; i < sizeof(jingleActions) / sizeof(jingleActions[0]); i++) {
        if (actionStr == jingleActions[i].text) {
            found = true;
            action = jingleActions[i].action;
            break;
        }
    }
    if (!found || sid.isEmpty()) {
        return;
    }

    if (!e.attribute(QLatin1String("initiator")).isEmpty()) {
        initiator = Jid(e.attribute(QLatin1String("initiator")));
        if (initiator.isNull()) {
            qDebug("malformed initiator jid");
            return;
        }
    }
    if (!e.attribute(QLatin1String("responder")).isEmpty()) {
        responder = Jid(e.attribute(QLatin1String("responder")));
        if (responder.isNull()) {
            qDebug("malformed responder jid");
            return;
        }
    }

    d = new Private;
    d->action = action;
    d->sid = sid;
    d->responder = responder;
}

Jingle::Jingle(const Jingle &other) :
    d(other.d)
{

}

Jingle::~Jingle()
{

}

Jingle::Private* Jingle::ensureD()
{
    if (!d) {
        d = new Private;
    }
    return d.data();
}

QDomElement Jingle::toXml(QDomDocument *doc) const
{
    if (!d || d->sid.isEmpty() || d->action == Action::NoAction) {
        return QDomElement();
    }

    QDomElement query = doc->createElementNS(NS, QLatin1String("jingle"));
    //query.setAttribute("xmlns", JINGLE_NS);
    for (unsigned int i = 0; i < sizeof(jingleActions) / sizeof(jingleActions[0]); i++) {
        if (jingleActions[i].action == d->action) {
            query.setAttribute(QLatin1String("action"), QLatin1String(jingleActions[i].text));
            break;
        }
    }

    if(!d->initiator.isNull())
        query.setAttribute(QLatin1String("initiator"), d->initiator.full());
    if(!d->responder.isNull())
        query.setAttribute(QLatin1String("responder"), d->responder.full());
    query.setAttribute(QLatin1String("sid"), d->sid);

    return query;
}

Action Jingle::action() const
{
    return d->action;
}

const QString &Jingle::sid() const
{
    return d->sid;
}

const Jid &Jingle::initiator() const
{
    return d->initiator;
}

void Jingle::setInitiator(const Jid &jid)
{
    d->initiator = jid;
}

const Jid &Jingle::responder() const
{
    return d->responder;
}

void Jingle::setResponder(const Jid &jid)
{
    d->responder = jid;
}


//----------------------------------------------------------------------------
// Reason
//----------------------------------------------------------------------------
static const QMap<QString,Reason::Condition> reasonConditions = {
    { QStringLiteral("alternative-session"),      Reason::AlternativeSession },
    { QStringLiteral("busy"),                     Reason::Busy },
    { QStringLiteral("cancel"),                   Reason::Cancel },
    { QStringLiteral("connectivity-error"),       Reason::ConnectivityError },
    { QStringLiteral("decline"),                  Reason::Decline },
    { QStringLiteral("expired"),                  Reason::Expired },
    { QStringLiteral("failed-application"),       Reason::FailedApplication },
    { QStringLiteral("failed-transport"),         Reason::FailedTransport },
    { QStringLiteral("general-error"),            Reason::GeneralError },
    { QStringLiteral("gone"),                     Reason::Gone },
    { QStringLiteral("incompatible-parameters"),  Reason::IncompatibleParameters },
    { QStringLiteral("media-error"),              Reason::MediaError },
    { QStringLiteral("security-error"),           Reason::SecurityError },
    { QStringLiteral("success"),                  Reason::Success },
    { QStringLiteral("timeout"),                  Reason::Timeout },
    { QStringLiteral("unsupported-applications"), Reason::UnsupportedApplications },
    { QStringLiteral("unsupported-transports"),   Reason::UnsupportedTransports },
};

class Reason::Private :public QSharedData {
public:
    Reason::Condition cond;
    QString text;
};

Reason::Reason()
{

}

Reason::~Reason()
{

}

Reason::Reason(Reason::Condition cond, const QString &text) :
    d(new Private)
{
    d->cond = cond;
    d->text = text;
}

Reason::Reason(const QDomElement &e)
{
    if(e.tagName() != QLatin1String("reason"))
        return;

    Condition condition = NoReason;
    QString text;
    QString rns = e.attribute(QStringLiteral("xmlns"));

    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.tagName() == QLatin1String("text")) {
            text = c.text();
        }
        else if (c.attribute(QStringLiteral("xmlns")) != rns) {
            // TODO add here all the extensions to reason.
        }
        else {
            condition = reasonConditions.value(c.tagName());
        }
    }

    if (condition != NoReason) {
        d = new Private;
        d->cond = condition;
        d->text = text;
    }
}

Reason::Reason(const Reason &other) :
    d(other.d)
{

}

Reason::Condition Reason::condition() const
{
    if (d) return d->cond;
    return NoReason;
}

void Reason::setCondition(Condition cond)
{
    ensureD()->cond = cond;
}

QString Reason::text() const
{
    if (d) return d->text;
    return QString();
}

void Reason::setText(const QString &text)
{
    ensureD()->text = text;
}

QDomElement Reason::toXml(QDomDocument *doc) const
{
    if (d && d->cond != NoReason) {
        for (auto r = reasonConditions.cbegin(); r != reasonConditions.cend(); ++r) {
            if (r.value() == d->cond) {
                QDomElement e = doc->createElement(QLatin1String("reason"));
                e.appendChild(doc->createElement(r.key()));
                if (!d->text.isEmpty()) {
                    e.appendChild(textTag(doc, QLatin1String("text"), d->text));
                }
                return e;
            }
        }
    }
    return QDomElement();
}

Reason::Private* Reason::ensureD()
{
    if (!d) {
        d = new Private;
    }
    return d.data();
}

//----------------------------------------------------------------------------
// ContentBase
//----------------------------------------------------------------------------
ContentBase::ContentBase(Origin creator, const QString &name) :
    creator(creator), name(name)
{
}

ContentBase::ContentBase(const QDomElement &el)
{
    static QMap<QString,Origin> sendersMap({
                                                {QStringLiteral("initiator"), Origin::Initiator},
                                                {QStringLiteral("none"), Origin::Initiator},
                                                {QStringLiteral("responder"), Origin::Initiator}
                                            });
    creator = creatorAttr(el);
    name = el.attribute(QLatin1String("name"));
    senders = sendersMap.value(el.attribute(QLatin1String("senders")));
    disposition = el.attribute(QLatin1String("disposition")); // if empty, it's "session"
}

QDomElement ContentBase::toXml(QDomDocument *doc, const char *tagName) const
{
    if (!isValid()) {
        return QDomElement();
    }
    auto el = doc->createElement(QLatin1String(tagName));
    setCreatorAttr(el, creator);
    el.setAttribute(QLatin1String("name"), name);

    QString sendersStr;
    switch (senders) {
    case Origin::None:
        sendersStr = QLatin1String("none");
        break;

    case Origin::Initiator:
        sendersStr = QLatin1String("initiator");
        break;

    case Origin::Responder:
        sendersStr = QLatin1String("responder");
        break;

    case Origin::Both:
    default:
        break;
    }

    if (!disposition.isEmpty() && disposition != QLatin1String("session")) {
        el.setAttribute(QLatin1String("disposition"), disposition); // NOTE review how we can parse it some generic way
    }
    if (!sendersStr.isEmpty()) {
        el.setAttribute(QLatin1String("senders"), sendersStr);
    }

    return el;
}


Origin ContentBase::creatorAttr(const QDomElement &el)
{
    auto creatorStr = el.attribute(QLatin1String("creator"));
    if (creatorStr == QLatin1String("initiator")) {
        return Origin::Initiator;
    }
    if (creatorStr == QLatin1String("responder")) {
        return Origin::Responder;
    }
    return Origin::None;
}

bool ContentBase::setCreatorAttr(QDomElement &el, Origin creator)
{
    if (creator == Origin::Initiator) {
        el.setAttribute(QLatin1String("creator"), QLatin1String("initiator"));
    } else if (creator == Origin::Responder) {
        el.setAttribute(QLatin1String("creator"), QLatin1String("responder"));
    } else {
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------
// Application
//----------------------------------------------------------------------------
ApplicationManager::ApplicationManager(QObject *parent) :
    QObject(parent)
{
}

//----------------------------------------------------------------------------
// TransportManager
//----------------------------------------------------------------------------
TransportManager::TransportManager(QObject *parent) :
    QObject(parent)
{

}

//----------------------------------------------------------------------------
// JT - Jingle Task
//----------------------------------------------------------------------------
class JTPush : public Task
{
    Q_OBJECT
public:
    JTPush(Task *parent) :
        Task(parent)
    {

    }

    ~JTPush(){}

    bool take(const QDomElement &iq)
    {
        if (iq.tagName() != QLatin1String("iq") || iq.attribute(QLatin1String("type")) != QLatin1String("set")) {
            return false;
        }
        auto jingleEl = iq.firstChildElement(QStringLiteral("jingle"));
        if (jingleEl.isNull() || jingleEl.attribute(QStringLiteral("xmlns")) != ::XMPP::Jingle::NS) {
            return false;
        }
        Jingle jingle(jingleEl);
        if (!jingle.isValid()) {
            respondError(iq, Stanza::Error::Cancel, Stanza::Error::BadRequest);
            return true;
        }

        QString fromStr(iq.attribute(QStringLiteral("from")));
        Jid from(fromStr);
        if (jingle.action() == Action::SessionInitiate) {
            if (!client()->jingleManager()->isAllowedParty(from) ||
                    (!jingle.initiator().isEmpty() && !client()->jingleManager()->isAllowedParty(jingle.initiator()))) {
                respondError(iq, Stanza::Error::Cancel, Stanza::Error::ServiceUnavailable);
                return true;
            }

            Jid redirection(client()->jingleManager()->redirectionJid());
            if (redirection.isValid()) {
                respondError(iq, Stanza::Error::Modify, Stanza::Error::Redirect, QStringLiteral("xmpp:")+redirection.full());
                return true;
            }

            auto session = client()->jingleManager()->session(from, jingle.sid());
            if (session) {
                // FIXME what if not yet acknowledged. xep-0166 has a solution for that
                respondError(iq, Stanza::Error::Cancel, Stanza::Error::Conflict);
                return true;
            }
            session = client()->jingleManager()->incomingSessionInitiate(from, jingle, jingleEl);
            if (!session) {
                respondError(iq, client()->jingleManager()->lastError());
                return true;
            }
        } else {
            auto session = client()->jingleManager()->session(from, jingle.sid());
            if (session) {
                respondError(iq, Stanza::Error::Cancel, Stanza::Error::Conflict);
                return true;
            }
            if (!session->updateFromXml(jingle.action(), jingleEl)) {
                respondError(iq, session->lastError());
                return true;
            }
        }

        auto resp = createIQ(client()->doc(), "result", fromStr, iq.attribute(QStringLiteral("id")));
        client()->send(resp);
        return true;
    }

    void respondError(const QDomElement &iq, Stanza::Error::ErrorType errType, Stanza::Error::ErrorCond errCond, const QString &text = QString())
    {
        auto resp = createIQ(client()->doc(), "error", iq.attribute(QStringLiteral("from")), iq.attribute(QStringLiteral("id")));
        Stanza::Error error(errType, errCond, text);
        resp.appendChild(error.toXml(*client()->doc(), client()->stream().baseNS()));
        client()->send(resp);
    }

    void respondError(const QDomElement &iq, const Stanza::Error &error)
    {
        auto resp = createIQ(client()->doc(), "error", iq.attribute(QStringLiteral("from")), iq.attribute(QStringLiteral("id")));
        resp.appendChild(error.toXml(*client()->doc(), client()->stream().baseNS()));
        client()->send(resp);
    }
};

//----------------------------------------------------------------------------
// JT - Jingle Task
//----------------------------------------------------------------------------
class JT : public Task
{
    Q_OBJECT

    QDomElement iq_;
    Jid to_;

public:
    JT(Task *parent) :
        Task(parent)
    {

    }

    ~JT(){}

    void request(const Jid &to, const QDomElement &jingleEl)
    {
        to_ = to;
        iq_ = createIQ(doc(), "set", to.full(), id());
        iq_.appendChild(jingleEl);
    }

    void onGo()
    {
        send(iq_);
    }

    bool take(const QDomElement &x)
    {
        if(!iqVerify(x, to_, id()))
            return false;

        if(x.attribute("type") == "error") {
            setError(x);
        } else {
            setSuccess();
        }
        return true;
    }
};


//----------------------------------------------------------------------------
// Session
//----------------------------------------------------------------------------
class Session::Private
{
public:
    Session *q;
    Manager *manager;
    QTimer stepTimer;
    State state = State::Created; // state of session on our side. if it's incoming we start from Created anyaway but Pending state is skipped
    Origin  role  = Origin::Initiator; // my role in the session
    XMPP::Stanza::Error lastError;
    Reason terminateReason;
    QMap<QString,QWeakPointer<ApplicationManagerPad>> applicationPads;
    QMap<QString,QWeakPointer<TransportManagerPad>> transportPads;
    //QMap<QString,Application*> myContent;     // content::creator=(role == Session::Role::Initiator?initiator:responder)
    //QMap<QString,Application*> remoteContent; // content::creator=(role == Session::Role::Responder?initiator:responder)
    QMap<ContentKey,Application*> contentList;
    QSet<Application*> signalingContent;
    QString sid;
    Jid origFrom; // "from" attr of IQ.
    Jid otherParty; // either "from" or initiator/responder. it's where to send all requests.
    Jid localParty; // that one will be set as initiator/responder if provided
    bool waitingAck = false;

    void sendJingle(Action action, QList<QDomElement> update, std::function<void()> successCB = std::function<void()>())
    {
        QDomDocument &doc = *manager->client()->doc();
        Jingle jingle(action, sid);
        if (action == Action::SessionInitiate) {
            jingle.setInitiator(manager->client()->jid());
        }
        if (action == Action::SessionAccept) {
            jingle.setResponder(manager->client()->jid());
        }
        auto xml = jingle.toXml(&doc);

        for (const QDomElement &e: update) {
            xml.appendChild(e);
        }

        auto jt = new JT(manager->client()->rootTask());
        jt->request(otherParty, xml);
        QObject::connect(jt, &JT::finished, manager, [jt, jingle, successCB, this](){
            waitingAck = false;
            if (jt->success()) {
                if (successCB) {
                    successCB();
                } else {
                    planStep();
                }
            } else {
                state = State::Finished;
                lastError = jt->error();
                emit q->terminated();
                q->deleteLater();
            }
        });
        waitingAck = true;
        jt->go(true);
    }

    void planStep() {
        if (waitingAck) {
            return;
        }
        lastError = Stanza::Error(0, 0);
        if (!stepTimer.isActive()) {
            stepTimer.start();
        }
    }

    void doStep() {
        if (waitingAck) { // we will return here when ack is received. Session::Unacked is possible also only with waitingAck
            return;
        }
        if (terminateReason.condition() && state != State::Finished) {
            if (state != State::Created || role == Origin::Responder) {
                sendJingle(Action::SessionTerminate, QList<QDomElement>() << terminateReason.toXml(manager->client()->doc()));
            }
            state = State::Finished;
            q->deleteLater();
            emit q->terminated();
            return;
        }
        if (state == State::Created || state == State::Finished) {
            return; // we will start doing something when initiate() is called
        }
        typedef std::tuple<QPointer<Application>,OutgoingUpdateCB> AckHndl; // will be used from callback on iq ack
        if (state == State::PrepareLocalOffer) { // we are going to send session-initiate/accept (already accepted by the user but not sent yet)
            /*
             * For session-initiate everything is prety much straightforward, just any content with Action::ContentAdd
             * update type has to be added. But with session-accept things are more complicated
             *   1. Local client could add its content. So we have to check content origin too.
             *   2. Remote client could add more content before local session-accept. Then we have two options
             *         a) send content-accept and skip this content in session-accept later
             *         b) don't send content-accept and accept everything with session-accept
             *      We prefer option (b) in our implementation.
             */
            Action expectedContentAction = role == Origin::Initiator? Action::ContentAdd : Action::ContentAccept;
            for (const auto &c: contentList) {
                if (c->creator() != role) {
                    continue; // we care only about local content for now.
                }
                auto out = c->outgoingUpdateType();
                if (out == Action::ContentReject) { // yeah we are rejecting local content. invalid?
                    lastError = XMPP::Stanza::Error(XMPP::Stanza::Error::Cancel, XMPP::Stanza::Error::BadRequest);
                    state = State::Finished;
                    q->deleteLater();
                    emit q->terminated();
                    return;
                }
                if (out != expectedContentAction) {
                    return; // keep waiting.
                }
            }
            Action actionToSend = Action::SessionAccept;
            State finalState = State::Active;
            // so all contents is ready for session-initiate. let's do it
            if (role == Origin::Initiator) {
                sid = manager->generateSessionId(otherParty);
                actionToSend = Action::SessionInitiate;
                finalState = State::Pending;
            }

            QList<QDomElement> contents;
            QList<AckHndl> acceptApps;
            for (const auto &p: contentList) {
                if (p->creator() != role) {
                    continue; // we care only about local content for now.
                }
                QDomElement xml;
                OutgoingUpdateCB callback;
                std::tie(xml, callback) = p->takeOutgoingUpdate();
                contents.append(xml);
                //p->setState(State::Unacked);
                if (callback) {
                    acceptApps.append(AckHndl{p, callback});
                }
            }
            state = State::Unacked;
            sendJingle(actionToSend, contents, [this, acceptApps, finalState](){
                state = finalState;
                for (const auto &h: acceptApps) {
                    auto app = std::get<0>(h);
                    auto callback = std::get<1>(h);
                    if (app) {
                        callback();
                    }
                }
                if (finalState == State::Active) {
                    emit q->activated();
                }
                planStep();
            });

            return;
        }

        // So session is either in State::Pending or State::Active here.
        // State::Connecting status is skipped for session.
        QList<QDomElement> updateXml;
        for (auto mp : applicationPads) {
            auto p = mp.toStrongRef();
            QDomElement el = p->takeOutgoingSessionInfoUpdate();
            if (!el.isNull()) {
                updateXml.append(el);
                // we can send session-info for just one application. so stop processing
                sendJingle(Action::SessionInfo, updateXml, [this](){planStep();});
                return;
            }
        }

        QMultiMap<Action, Application*> updates;

        for (auto app : contentList) {
            Action updateType = app->outgoingUpdateType();
            if (updateType != Action::NoAction) {
                updates.insert(updateType, app);
            }
        }

        QList<AckHndl> acceptApps;
        if (updates.size()) {
            Action action = updates.begin().key();
            auto apps = updates.values(action);
            for (auto app: apps) {
                QDomElement xml;
                OutgoingUpdateCB callback;
                std::tie(xml, callback) = app->takeOutgoingUpdate();
                updateXml.append(xml);
                if (callback) {
                    acceptApps.append(AckHndl{app, callback});
                }
            }
            sendJingle(action, updateXml, [this, acceptApps](){
                for (const auto &h: acceptApps) {
                    auto app = std::get<0>(h);
                    auto callback = std::get<1>(h);
                    if (app) {
                        callback();
                    }
                }
                planStep();
            });
        }
    }

    Reason reason(const QDomElement &jingleEl)
    {
        QDomElement re = jingleEl.firstChildElement(QLatin1String("reason"));
        Reason reason;
        if(!re.isNull()) {
            reason = Reason(re);
            if (!reason.isValid()) {
                qDebug("invalid reason");
            }
        }
        return reason;
    }

    enum AddContentError {
        Ok,
        Unparsed,
        Unsupported
    };

    std::tuple<AddContentError, Reason::Condition, Application*> parseContent(const QDomElement &ce)
    {
        QDomElement descriptionEl = ce.firstChildElement(QLatin1String("description"));
        QDomElement transportEl = ce.firstChildElement(QLatin1String("transport"));
        QString descriptionNS = descriptionEl.attribute(QStringLiteral("xmlns"));
        QString transportNS = transportEl.attribute(QStringLiteral("xmlns"));
        typedef std::tuple<AddContentError, Reason::Condition, Application*> result;

        ContentBase c(ce);
        if (!c.isValid() || descriptionEl.isNull() || transportEl.isNull() || descriptionNS.isEmpty() || transportNS.isEmpty()) {
            return result{Unparsed, Reason::Success, nullptr};
        }

        auto appPad = q->applicationPadFactory(descriptionNS);
        if (!appPad) {
            return result{Unsupported, Reason::UnsupportedApplications, nullptr}; // <unsupported-applications/> condition
        }
        QScopedPointer<Application> app(appPad->manager()->startApplication(appPad, c.name, c.creator, c.senders));
        auto descErr = app->setDescription(descriptionEl);
        if (descErr == Application::IncompatibleParameters) {
            return result{Unsupported, Reason::IncompatibleParameters, nullptr};
        } else
        if (descErr == Application::Unparsed) {
            return result{Unparsed, Reason::Success, nullptr};
        } else
        {
            // same for transport
            auto trPad = q->transportPadFactory(transportNS);
            if (!trPad) {
                return result{Unsupported, Reason::UnsupportedTransports, app.take()}; // <unsupported-transports/> condition or we can try fallback and fail with <failed-transport/>
            }
            auto transport = trPad->manager()->newTransport(trPad, transportEl);
            if (transport) {
                if (app->setTransport(transport)) {
                    return result{Ok, Reason::Success, app.take()};
                }
                return result{Unsupported, Reason::UnsupportedTransports, app.take()};
            }
        }

        return result{Unparsed, Reason::Success, nullptr};
    }


    typedef std::tuple<AddContentError, Reason::Condition, QList<Application*>, QList<QDomElement>> ParseContentListResult;

    ParseContentListResult parseContentList(const QDomElement &jingleEl)
    {
        QMap<QString,Application *> addSet;
        QMap<QString,std::pair<QDomElement,Reason::Condition>> rejectSet;

        QString contentTag(QStringLiteral("content"));
        for(QDomElement ce = jingleEl.firstChildElement(contentTag);
            !ce.isNull(); ce = ce.nextSiblingElement(contentTag))
        {

            Private::AddContentError err;
            Reason::Condition cond;
            Application *app;

            std::tie(err, cond, app) = parseContent(ce);
            if (err == Private::AddContentError::Unparsed) {
                lastError = XMPP::Stanza::Error(XMPP::Stanza::Error::Cancel, XMPP::Stanza::Error::BadRequest);
                qDeleteAll(addSet);
                return ParseContentListResult(Unparsed, cond, QList<Application*>(), QList<QDomElement>());
            }

            auto contentName = app->contentName();
            auto it = addSet.find(contentName);
            if (err != Private::AddContentError::Ok) {
                // can't continue as well
                if (app) { // we are going to reject it completely so delete
                    delete app;
                }

                if (it == addSet.end()) {
                    rejectSet.insert(contentName, std::make_pair(ce, cond));
                }
                continue;
            }

            rejectSet.remove(contentName);
            if (it == addSet.end() || (*it)->wantBetterTransport(app->transport())) { // probably not wantBetterTransport but wantBetterApplication
                delete *it; // unpreferred app
                *it = app;
            }
        }

        if (rejectSet.size()) {
            QList<QDomElement> rejectList;
            for (auto const &i: rejectSet) {
                rejectList.append(i.first);
            }
            return ParseContentListResult(Unsupported, rejectSet.first().second, addSet.values(), rejectList);
        }

        return ParseContentListResult(Ok, Reason::Success, addSet.values(), QList<QDomElement>());
    }
};

Session::Session(Manager *manager, const Jid &peer) :
    d(new Private)
{
    d->q = this;
    d->manager = manager;
    d->otherParty = peer;
    d->stepTimer.setSingleShot(true);
    d->stepTimer.setInterval(0);
    connect(&d->stepTimer, &QTimer::timeout, this, [this](){ d->doStep();});

}

Session::~Session()
{
    qDebug("session %s destroyed", qPrintable(d->sid));
}

Manager *Session::manager() const
{
    return d->manager;
}

State Session::state() const
{
    return d->state;
}

Jid Session::me() const
{
    return d->manager->client()->jid();
}

Jid Session::peer() const
{
    return d->otherParty;
}

Jid Session::initiator() const
{
    return d->role == Origin::Initiator? d->manager->client()->jid() : d->otherParty;
}

Jid Session::responder() const
{
    return d->role == Origin::Responder? d->manager->client()->jid() : d->otherParty;
}

Origin Session::role() const
{
    return d->role;
}

XMPP::Stanza::Error Session::lastError() const
{
    return d->lastError;
}

Application *Session::newContent(const QString &ns, Origin senders)
{
    auto pad = applicationPadFactory(ns);
    if (pad) {
        return pad->manager()->startApplication(pad, pad->generateContentName(senders), d->role, senders);
    }
    return nullptr;
}

Application *Session::content(const QString &contentName, Origin creator)
{
    return d->contentList.value(ContentKey{contentName, creator});
}

void Session::addContent(Application *content)
{
    d->contentList.insert(ContentKey{content->contentName(), d->role}, content);
    if (d->state != State::Created && content->outgoingUpdateType() != Action::NoAction) {
        d->signalingContent.insert(content);
    }
    connect(content, &Application::updated, this, [this](){
        d->signalingContent.insert(static_cast<Application*>(sender()));
        if (!d->waitingAck && !d->stepTimer.isActive()) {
            d->stepTimer.start();
        }
    });
}

const QMap<ContentKey, Application *> &Session::contentList() const
{
    return d->contentList;
}

ApplicationManagerPad::Ptr Session::applicationPad(const QString &ns)
{
    return d->applicationPads.value(ns).toStrongRef();
}

TransportManagerPad::Ptr Session::transportPad(const QString &ns)
{
    return d->transportPads.value(ns).toStrongRef();
}

QSharedPointer<Transport> Session::newOutgoingTransport(const QString &ns)
{
    auto pad = transportPadFactory(ns);
    if (pad) {
        return pad->manager()->newTransport(pad); // pad on both side becaue we need shared pointer
    }
    return QSharedPointer<Transport>();
}

QString Session::preferredApplication() const
{
    // TODO some heuristics to detect preferred application
    return d->applicationPads.keys().value(0);
}

QStringList Session::allApplicationTypes() const
{
    return d->applicationPads.keys();
}

void Session::setLocalJid(const Jid &jid)
{
    d->localParty = jid;
}

void Session::accept()
{
    // So we presented a user incoming session in UI, the user modified it somehow and finally accepted.
    if (d->role == Origin::Responder && d->state == State::Created) {
        d->state = State::PrepareLocalOffer;
        for (auto &c: d->contentList) {
            c->prepare();
        }
        d->planStep();
    }
}

void Session::initiate()
{
    if (d->role == Origin::Initiator && d->state == State::Created) {
        d->state = State::PrepareLocalOffer;
        for (auto &c: d->contentList) {
            c->prepare();
        }
        d->planStep();
    }
}

void Session::terminate(Reason::Condition cond, const QString &comment)
{
    d->terminateReason = Reason(cond, comment);
}

TransportManagerPad::Ptr Session::transportPadFactory(const QString &ns)
{
    auto pad = d->transportPads.value(ns).toStrongRef();
    if (!pad) {
        auto deleter = [ns, this](TransportManagerPad *pad){
            d->transportPads.remove(ns);
            delete pad;
        };
        pad = TransportManagerPad::Ptr(d->manager->transportPad(this, ns), deleter);
        if (pad) {
            d->transportPads.insert(ns, pad);
        }
    }
    return pad;
}

ApplicationManagerPad::Ptr Session::applicationPadFactory(const QString &ns)
{
    auto pad = d->applicationPads.value(ns).toStrongRef();
    if (!pad) {
        auto deleter = [ns, this](ApplicationManagerPad *pad){
            d->applicationPads.remove(ns);
            delete pad;
        };
        pad = ApplicationManagerPad::Ptr(d->manager->applicationPad(this, ns), deleter);
        if (pad) {
            d->applicationPads.insert(ns, pad);
        }
    }
    return pad;
}

bool Session::incomingInitiate(const Jingle &jingle, const QDomElement &jingleEl)
{
    d->sid = jingle.sid();
    d->origFrom = d->otherParty;
    if (jingle.initiator().isValid() && !jingle.initiator().compare(d->origFrom)) {
        d->otherParty = jingle.initiator();
    }

    Private::AddContentError err;
    Reason::Condition cond;
    QList<Application *> apps;
    QList<QDomElement> rejects;

    std::tie(err, cond, apps, rejects) = d->parseContentList(jingleEl);
    switch (err) {
    case Private::AddContentError::Unparsed:
        return false;
    case Private::AddContentError::Unsupported:
        QTimer::singleShot(0, this, [this, cond](){
            d->sendJingle(Action::SessionTerminate, QList<QDomElement>() << Reason(cond).toXml(d->manager->client()->doc()));
            d->state = State::Finished;
            emit terminated();
        });
        return true;
    case Private::AddContentError::Ok:
        for (auto app: apps) {
            d->contentList.insert(ContentKey{app->contentName(), Origin::Initiator}, app);
        }
        d->planStep();
        return true;
    }

    return false;
}

bool Session::updateFromXml(Action action, const QDomElement &jingleEl)
{
    if (d->state == State::Finished) {
        d->lastError = XMPP::Stanza::Error(XMPP::Stanza::Error::Cancel, XMPP::Stanza::Error::UnexpectedRequest); // TODO OutOfOrder
        return false;
    }

    if (action == Action::SessionInfo) {

    }

    if (action == Action::ContentAdd) {
        Private::AddContentError err;
        Reason::Condition cond;
        QList<Application *> apps;
        QList<QDomElement> rejects;

        std::tie(err, cond, apps, rejects) = d->parseContentList(jingleEl);
        switch (err) {
        case Private::AddContentError::Unparsed:
            return false;
        case Private::AddContentError::Unsupported:
            QTimer::singleShot(0, this, [this, cond, rejects]() mutable {
                rejects += Reason(cond).toXml(d->manager->client()->doc());
                d->sendJingle(Action::ContentReject, rejects);
            });
            break;
        case Private::AddContentError::Ok:
            break;
        }

        if (apps.size()) {
            Origin remoteRole = negateOrigin(d->role);
            for (auto app: apps) {
                d->contentList.insert(ContentKey{app->contentName(), remoteRole}, app); // TODO check conflicts
            }
            QTimer::singleShot(0, this, [this](){ emit newContentReceived(); });
        }

        return true;
    }

    d->lastError = XMPP::Stanza::Error(XMPP::Stanza::Error::Cancel, XMPP::Stanza::Error::UnexpectedRequest);
    return false;
}


//----------------------------------------------------------------------------
// SessionManagerPad - handle event related to a type of app/transport but not specific instance
//----------------------------------------------------------------------------
QDomElement SessionManagerPad::takeOutgoingSessionInfoUpdate()
{
    return QDomElement();
}

//----------------------------------------------------------------------------
// Manager
//----------------------------------------------------------------------------
class Manager::Private
{
public:
    XMPP::Client *client;
    Manager *manager;
    QScopedPointer<JTPush> pushTask;
    // ns -> application
    QMap<QString,QPointer<ApplicationManager>> applicationManagers;
    // ns -> parser function
    QMap<QString,QPointer<TransportManager>> transportManagers;
    std::function<bool(const Jid &)> remoteJidCecker;

    // when set/valid any incoming session initiate will be replied with redirection error
    Jid redirectionJid;
    XMPP::Stanza::Error lastError;
    QHash<QPair<Jid,QString>,Session*> sessions;
    int maxSessions = -1; // no limit
};

Manager::Manager(Client *client) :
    d(new Private())
{
    d->client = client;
    d->manager = this;
    d->pushTask.reset(new JTPush(client->rootTask()));
}

Manager::~Manager()
{
}

Client *Manager::client() const
{
    return d->client;
}

void Manager::setRedirection(const Jid &to)
{
    d->redirectionJid = to;
}

const Jid &Manager::redirectionJid() const
{
    return d->redirectionJid;
}

void Manager::registerApp(const QString &ns, ApplicationManager *app)
{
    d->applicationManagers.insert(ns, app);
    app->setJingleManager(this);
}

void Manager::unregisterApp(const QString &ns)
{
    auto appManager = d->applicationManagers.value(ns);
    if (appManager) {
        appManager->closeAll();
        d->applicationManagers.remove(ns);
    }
}

bool Manager::isRegisteredApplication(const QString &ns)
{
    return d->applicationManagers.contains(ns);
}

ApplicationManagerPad *Manager::applicationPad(Session *session, const QString &ns)
{
    auto am = d->applicationManagers.value(ns);
    if (!am) {
        return NULL;
    }
    return am->pad(session);
}

void Manager::registerTransport(const QString &ns, TransportManager *transport)
{
    d->transportManagers.insert(ns, transport);
    transport->setJingleManager(this);
}

void Manager::unregisterTransport(const QString &ns)
{
    auto trManager = d->transportManagers.value(ns);
    if (trManager) {
        trManager->closeAll();
        d->transportManagers.remove(ns);
    }
}

bool Manager::isRegisteredTransport(const QString &ns)
{
    return d->transportManagers.contains(ns);
}

bool Manager::isAllowedParty(const Jid &jid) const
{
    if (d->remoteJidCecker) {
        return d->remoteJidCecker(jid);
    }
    // REVIEW probably we can check Client's internal roster when checker is not set.
    return true;
}

Session *Manager::session(const Jid &remoteJid, const QString &sid)
{
    return d->sessions.value(qMakePair(remoteJid, sid));
}

void Manager::setRemoteJidChecker(std::function<bool(const Jid &)> checker)
{
    d->remoteJidCecker = checker;
}

TransportManagerPad* Manager::transportPad(Session *session, const QString &ns)
{
    auto transportManager = d->transportManagers.value(ns);
    if (!transportManager) {
        return NULL;
    }
    return transportManager->pad(session);
}

QStringList Manager::availableTransports(const Transport::Features &features) const
{
    QStringList ret;
    for (auto it = d->transportManagers.cbegin(); it != d->transportManagers.cend(); ++it) {
        if (((*it)->features() & features) == features) {
            ret.append(it.key());
        }
    }
    return ret;
}

Session* Manager::incomingSessionInitiate(const Jid &from, const Jingle &jingle, const QDomElement &jingleEl)
{
    if (d->maxSessions > 0 && d->sessions.size() == d->maxSessions) {
        d->lastError = XMPP::Stanza::Error(XMPP::Stanza::Error::Wait, XMPP::Stanza::Error::ResourceConstraint);
        return NULL;
    }
    auto key = qMakePair(from, jingle.sid());
    auto s = new Session(this, from);
    if (s->incomingInitiate(jingle, jingleEl)) { // if parsed well
        d->sessions.insert(key, s);
        // emit incomingSession makes sense when there are no unsolved conflicts in content descriptions / transports
        //QTimer::singleShot(0,[s, this](){ emit incomingSession(s); });
        QMetaObject::invokeMethod(this, "incomingSession", Qt::QueuedConnection, Q_ARG(Session*, s));
        return s;
    }
    d->lastError = s->lastError();
    delete s;
    return NULL;
}

XMPP::Stanza::Error Manager::lastError() const
{
    return d->lastError;
}

Session *Manager::newSession(const Jid &j)
{
    return new Session(this, j);
}

QString Manager::generateSessionId(const Jid &peer)
{
    QString id;
    do {
        id = QString("%1").arg(quint32(qrand()), 6, 32, QChar('0'));
    } while (d->sessions.contains(QPair<Jid,QString>(peer,id)));
    return id;
}

Origin negateOrigin(Origin o)
{
    switch (o) {
    case Origin::None:      return Origin::Both;
    case Origin::Both:      return Origin::None;
    case Origin::Initiator: return Origin::Responder;
    case Origin::Responder: return Origin::Initiator;
    }
    return Origin::None;
}


} // namespace Jingle
} // namespace XMPP

#include "jingle.moc"
