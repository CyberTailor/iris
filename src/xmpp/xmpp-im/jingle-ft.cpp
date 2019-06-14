/*
 * jignle-ft.h - Jingle file transfer
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

#include "jingle-ft.h"
#include "xmpp_client.h"
#include "xmpp_thumbs.h"
#include "xmpp_hash.h"
#include "xmpp_xmlcommon.h"

namespace XMPP {
namespace Jingle {

namespace FileTransfer {

const QString NS = QStringLiteral("urn:xmpp:jingle:apps:file-transfer:5");


QDomElement Range::toXml(QDomDocument *doc) const
{
    auto r = doc->createElement(QStringLiteral("range"));
    if (length) {
        r.setAttribute(QStringLiteral("length"), QString::number(length));
    }
    if (offset) {
        r.setAttribute(QStringLiteral("length"), QString::number(length));
    }
    auto h = hash.toXml(doc);
    if (!h.isNull()) {
        r.appendChild(h);
    }
    return r;
}

//----------------------------------------------------------------------------
// File
//----------------------------------------------------------------------------
class File::Private : public QSharedData
{
public:
    QDateTime date;
    QString   mediaType;
    QString   name;
    QString   desc;
    quint64   size = 0;
    Range     range;
    bool      rangeSupported = false;
    Hash      hash;
    Thumbnail thumbnail;
};

File::File()
{

}

File::~File()
{

}

File::File(const File &other) :
    d(other.d)
{

}

File::File(const QDomElement &file)
{
    QDateTime date;
    QString   mediaType;
    QString   name;
    QString   desc;
    size_t    size = 0;
    bool      rangeSupported = false;
    Range     range;
    Hash      hash;
    Thumbnail thumbnail;

    bool ok;

    for(QDomElement ce = file.firstChildElement();
        !ce.isNull(); ce = ce.nextSiblingElement()) {

        if (ce.tagName() == QLatin1String("date")) {
            date = QDateTime::fromString(ce.text().left(19), Qt::ISODate);
            if (!date.isValid()) {
                return;
            }

        } else if (ce.tagName() == QLatin1String("media-type")) {
            mediaType = ce.text();

        } else if (ce.tagName() == QLatin1String("name")) {
            name = ce.text();

        } else if (ce.tagName() == QLatin1String("size")) {
            size = ce.text().toULongLong(&ok);
            if (!ok) {
                return;
            }

        } else if (ce.tagName() == QLatin1String("range")) {
            if (ce.hasAttribute(QLatin1String("offset"))) {
                range.offset = ce.attribute(QLatin1String("offset")).toULongLong(&ok);
                if (!ok) {
                    return;
                }
            }
            if (ce.hasAttribute(QLatin1String("length"))) {
                range.offset = ce.attribute(QLatin1String("length")).toULongLong(&ok);
                if (!ok) {
                    return;
                }
            }
            QDomElement hashEl = ce.firstChildElement(QLatin1String("hash"));
            if (hashEl.attribute(QStringLiteral("xmlns")) == QLatin1String("urn:xmpp:hashes:2")) {
                range.hash = Hash(hashEl);
                if (range.hash.type() == Hash::Type::Unknown) {
                    return;
                }
            }
            rangeSupported = true;

        } else if (ce.tagName() == QLatin1String("desc")) {
            desc = ce.text();

        } else if (ce.tagName() == QLatin1String("hash")) {
            if (ce.attribute(QStringLiteral("xmlns")) == QLatin1String(XMPP_HASH_NS)) {
                hash = Hash(ce);
                if (hash.type() == Hash::Type::Unknown) {
                    return;
                }
            }

        } else if (ce.tagName() == QLatin1String("hash-used")) {
            if (ce.attribute(QStringLiteral("xmlns")) == QLatin1String(XMPP_HASH_NS)) {
                hash = Hash(ce);
                if (hash.type() == Hash::Type::Unknown) {
                    return;
                }
            }

        } else if (ce.tagName() == QLatin1String("thumbnail")) {
            thumbnail = Thumbnail(ce);
        }
    }

    auto p = new Private;
    p->date = date;
    p->mediaType = mediaType;
    p->name = name;
    p->desc = desc;
    p->size = size;
    p->rangeSupported = rangeSupported;
    p->range = range;
    p->hash = hash;
    p->thumbnail = thumbnail;

    d = p;
}

QDomElement File::toXml(QDomDocument *doc) const
{
    if (!isValid()) {
        return QDomElement();
    }
    QDomElement el = doc->createElement(QStringLiteral("file"));
    if (d->date.isValid()) {
        el.appendChild(XMLHelper::textTag(*doc, QStringLiteral("date"), d->date.toString(Qt::ISODate)));
    }
    if (d->desc.size()) {
        el.appendChild(XMLHelper::textTag(*doc, QStringLiteral("desc"), d->desc));
    }
    if (d->hash.isValid()) {
        el.appendChild(d->hash.toXml(doc));
    }
    if (d->mediaType.size()) {
        el.appendChild(XMLHelper::textTag(*doc, QStringLiteral("media-type"), d->mediaType));
    }
    if (d->name.size()) {
        el.appendChild(XMLHelper::textTag(*doc, QStringLiteral("name"), d->name));
    }
    if (d->size) {
        el.appendChild(XMLHelper::textTag(*doc, QStringLiteral("size"), QString::number(d->size)));
    }
    if (d->rangeSupported || d->range.isValid()) {
        el.appendChild(d->range.toXml(doc));
    }
    if (d->thumbnail.isValid()) {
        el.appendChild(d->thumbnail.toXml(doc));
    }
    return el;
}

QDateTime File::date() const
{
    return d? d->date : QDateTime();
}

QString File::description() const
{
    return d? d->desc : QString();
}

Hash File::hash() const
{
    return d? d->hash : Hash();
}

QString File::mediaType() const
{
    return d? d->mediaType : QString();
}

QString File::name() const
{
    return d? d->name : QString();
}

quint64 File::size() const
{
    return d? d->size : 0;
}

Range File::range() const
{
    return d? d->range : Range();
}

Thumbnail File::thumbnail() const
{
    return d? d->thumbnail: Thumbnail();
}

void File::setDate(const QDateTime &date)
{
    ensureD()->date = date;
}

void File::setDescription(const QString &desc)
{
    ensureD()->desc = desc;
}

void File::setHash(const Hash &hash)
{
    ensureD()->hash = hash;
}

void File::setMediaType(const QString &mediaType)
{
    ensureD()->mediaType = mediaType;
}

void File::setName(const QString &name)
{
    ensureD()->name = name;
}

void File::setSize(quint64 size)
{
    ensureD()->size = size;
}

void File::setRange(const Range &range)
{
    ensureD()->range = range;
    d->rangeSupported = true;
}

void File::setThumbnail(const Thumbnail &thumb)
{
    ensureD()->thumbnail = thumb;
}

File::Private *File::ensureD()
{
    if (!d) {
        d = new Private;
    }
    return d.data();
}

//----------------------------------------------------------------------------
// Checksum
//----------------------------------------------------------------------------
Checksum::Checksum(const QDomElement &cs) :
    ContentBase(cs)
{
    file = File(cs.firstChildElement(QLatin1String("file")));
}

bool Checksum::isValid() const
{
    return ContentBase::isValid() && file.isValid();
}

QDomElement Checksum::toXml(QDomDocument *doc) const
{
    auto el = ContentBase::toXml(doc, "checksum");
    if (!el.isNull()) {
        el.appendChild(file.toXml(doc));
    }
    return el;
}

//----------------------------------------------------------------------------
// Received
//----------------------------------------------------------------------------
QDomElement Received::toXml(QDomDocument *doc) const
{
    return ContentBase::toXml(doc, "received");
}

//----------------------------------------------------------------------------
// ApplicationManager
//----------------------------------------------------------------------------
Manager::Manager(QObject *parent):
    XMPP::Jingle::ApplicationManager(parent)
{

}

void Manager::setJingleManager(XMPP::Jingle::Manager *jm)
{
    jingleManager = jm;
}

Application* Manager::startApplication(const ApplicationManagerPad::Ptr &pad, const QString &contentName, Origin creator, Origin senders)
{
    if (!(contentName.size() > 0 && (senders == Origin::Initiator || senders == Origin::Responder))) {
        qDebug("Invalid Jignle FT App start parameters");
        return nullptr;
    }
    return new Application(pad.staticCast<Pad>(), contentName, creator, senders); // ContentOrigin::Remote
}

ApplicationManagerPad* Manager::pad(Session *session)
{
    return new Pad(this, session);
}

void Manager::closeAll()
{

}

Client *Manager::client()
{
    if (jingleManager) {
        return jingleManager->client();
    }
    return nullptr;
}

QStringList Manager::availableTransports() const
{
    return jingleManager->availableTransports(Transport::Reliable);
}

//----------------------------------------------------------------------------
// Application
//----------------------------------------------------------------------------
class Application::Private
{
public:
    Application *q = nullptr;
    State   state = State::Created;
    Action  updateToSend = Action::NoAction;
    QSharedPointer<Pad> pad;
    QString contentName;
    File    file;
    File    acceptFile; // as it comes with "accept" response
    Origin  creator;
    Origin  senders;
    Origin  transportFailedOrigin = Origin::None;
    XMPP::Stanza::Error lastError;
    QSharedPointer<Transport> transport;
    Connection::Ptr connection;
    QStringList availableTransports;
    bool transportReady = false; // when prepare local offer finished for the transport
    bool closeDeviceOnFinish = true;
    bool waitTransportAccept = false;
    QIODevice *device = nullptr;
    quint64 bytesLeft = 0;

    void setState(State s)
    {
        state = s;
        if (s == State::Finished) {
            if (device && closeDeviceOnFinish) {
                device->close();
            }
            if (connection) {
                connection->close();
            }
        }
        emit q->stateChanged(s);
    }

    void handleStreamFail()
    {
        // TODO d->lastError = Condition::FailedApplication
        setState(State::Finished);
    }

    void writeNextBlockToTransport()
    {
        if (!bytesLeft) {
            setState(State::Finished);
            return; // everything is written
        }
        auto sz = connection->blockSize();
        sz = sz? sz : 8192;
        if (sz > bytesLeft) {
            sz = bytesLeft;
        }
        QByteArray data = device->read(sz);
        if (data.isEmpty()) {
            handleStreamFail();
            return;
        }
        if (connection->write(data) == -1) {
            handleStreamFail();
            return;
        }
        emit q->progress(device->pos());
        bytesLeft -= sz;
    }

    void readNextBlockFromTransport()
    {
        quint64 bytesAvail;
        while (bytesLeft && (bytesAvail = connection->bytesAvailable())) {
            quint64 sz = 65536; // shall we respect transport->blockSize() ?
            if (sz > bytesLeft) {
                sz = bytesLeft;
            }
            if (sz > bytesAvail) {
                sz = bytesAvail;
            }
            QByteArray data = connection->read(sz);
            if (data.isEmpty()) {
                handleStreamFail();
                return;
            }
            if (device->write(data) == -1) {
                handleStreamFail();
                return;
            }
            emit q->progress(device->pos());
            bytesLeft -= sz;
        }
        if (!bytesLeft) {
            // TODO send <received>
            setState(State::Finished);
        }
    }
};

Application::Application(const QSharedPointer<Pad> &pad, const QString &contentName, Origin creator, Origin senders) :
    d(new Private)
{
    d->q       = this;
    d->pad     = pad;
    d->contentName = contentName;
    d->creator = creator;
    d->senders = senders;
    d->availableTransports = static_cast<Manager*>(pad->manager())->availableTransports();
}

Application::~Application()
{

}

ApplicationManagerPad::Ptr Application::pad() const
{
    return d->pad.staticCast<ApplicationManagerPad>();
}

State Application::state() const
{
    return d->state;
}

void Application::setState(State state)
{
    d->setState(state);
}

Stanza::Error Application::lastError() const
{
    return d->lastError;
}

QString Application::contentName() const
{
    return d->contentName;
}

Origin Application::creator() const
{
    return d->creator;
}

Origin Application::senders() const
{
    return d->senders;
}

Application::SetDescError Application::setDescription(const QDomElement &description)
{
    d->file = File(description.firstChildElement("file"));
    //d->state = State::Pending; // basically it's incomming  content. so if we parsed it it's pending. if not parsed if will rejected anyway.
    return d->file.isValid()? Ok: Unparsed;
}

void Application::setFile(const File &file)
{
    d->file = file;
}

File Application::file() const
{
    return d->file;
}

File Application::acceptFile() const
{
    return d->acceptFile;
}

// incoming one? or we have to check real direction
bool Application::setTransport(const QSharedPointer<Transport> &transport)
{
    if (!(transport->features() & Transport::Reliable))
        return false;

    int nsIndex = d->availableTransports.indexOf(transport->pad()->ns());
    if (nsIndex == -1) {
        return false;
    }

    // in case we automatically select a new transport on our own we definitely will come up to this point
    if (d->transport) {
        d->transport->disconnect(this);
        d->transport.reset();
    }

    d->availableTransports.removeAt(nsIndex);
    d->transport = transport;
    connect(transport.data(), &Transport::updated, this, &Application::updated);
    connect(transport.data(), &Transport::connected, this, [this](){
        d->transportFailedOrigin = Origin::None;
        d->connection = d->transport->connection();
        connect(d->connection.data(), &Connection::readyRead, this, [this](){
            if (!d->device) {
                return;
            }
            if (d->pad->session()->role() != d->senders) {
                d->readNextBlockFromTransport();
            }
        });
        connect(d->connection.data(), &Connection::bytesWritten, this, [this](qint64 bytes){
            Q_UNUSED(bytes)
            if (d->pad->session()->role() == d->senders && !d->connection->bytesToWrite()) {
                d->writeNextBlockToTransport();
            }
        });
        d->setState(State::Active);
        if (d->acceptFile.range().isValid()) {
            d->bytesLeft = d->acceptFile.range().length;
            emit deviceRequested(d->acceptFile.range().offset, d->bytesLeft);
        } else {
            d->bytesLeft = d->file.size();
            emit deviceRequested(0, d->bytesLeft);
        }
    });

    connect(transport.data(), &Transport::failed, this, [this](){
        d->transportFailedOrigin = d->pad->session()->role();
        d->waitTransportAccept = false;
        if (d->state >= State::Active) {
            emit updated(); // late failure are unhandled. just notify the remote
            return;
        }
        // we can try to replace the transport
        if (!selectNextTransport()) { // we can do transport-replace here
            if (d->state == State::PrepareLocalOffer && d->creator == d->pad->session()->role()) {
                // we were unable to send even initial offer
                d->setState(State::Finished);
            } else {
                emit updated(); // we have to notify our peer about failure
            }
        } // else transport will notify when ready
    });

    if (d->state >= State::Unacked) {
        // seems like we are in transport failure recovery. d->transportFailed may confirm this
        d->transport->prepare();
    }
    return true;
}

Origin Application::transportReplaceOrigin() const
{
    return d->transportFailedOrigin;
}

bool Application::incomingTransportReplace(const QSharedPointer<Transport> &transport)
{
    auto prev = d->transportFailedOrigin;
    if (d->pad->session()->role() == Origin::Responder && prev == Origin::Responder && d->transport) {
        // if I'm a responder and tried to send transport-replace too, then push ns back
        d->availableTransports.append(d->transport->pad()->ns());
    }
    d->transportFailedOrigin = d->pad->session()->peerRole();
    auto ret = setTransport(transport);
    if (ret)
        d->waitTransportAccept = false;
    else {
        d->transportFailedOrigin = prev;
        d->lastError.reset();
    }

    return ret;
}

QSharedPointer<Transport> Application::transport() const
{
    return d->transport;
}

Action Application::evaluateOutgoingUpdate()
{
    d->updateToSend = Action::NoAction;
    if (!isValid() || d->state == State::Created || d->state == State::Finished || (d->waitTransportAccept && d->state < State::Finishing)) {
        return d->updateToSend;
    }

    switch (d->state) {
    case State::Created:
        break;
    case State::PrepareLocalOffer:
        if (d->transportFailedOrigin != Origin::None && !d->transport)
            d->updateToSend = Action::ContentReject; // case me=creator was already handled by this momemnt. see Transport::failed connectio above

        else if (d->transport->hasUpdates() || d->transportReady) {
            d->transportReady = true;
            auto myRole = d->pad->session()->role();
            if (d->creator == myRole)
                d->updateToSend = Action::ContentAdd;
            else
                d->updateToSend = d->transportFailedOrigin == myRole? Action::TransportReplace : Action::ContentAccept;
        }
        break;
    case State::Connecting:
    case State::Pending:
    case State::Active:
        if (d->transportFailedOrigin != Origin::None && (d->state == State::Active || !d->transport))
            d->updateToSend = Action::ContentRemove;

        else if (d->transport->hasUpdates()) {
            if (d->transportFailedOrigin != Origin::None)
                d->updateToSend = d->transportFailedOrigin == d->pad->session()->role()? Action::TransportReplace : Action::TransportAccept;
            else
                d->updateToSend = Action::TransportInfo;
        }
        break;
    case State::Finishing:
        if (d->transportFailedOrigin != Origin::None) {
            d->updateToSend = Action::ContentRemove;
        } else {
            d->updateToSend = Action::SessionInfo;
        }
        break;
    default:
        break;
    }
    return d->updateToSend; // TODO
}

OutgoingUpdate Application::takeOutgoingUpdate()
{
    if (d->updateToSend == Action::NoAction) {
        return OutgoingUpdate();
    }

    auto client = d->pad->session()->manager()->client();
    auto doc = client->doc();

    if (d->updateToSend == Action::SessionInfo) {
        if (d->state != State::Finishing) {
            // TODO implement
            return OutgoingUpdate();
        }
        ContentBase cb(d->pad->session()->role(), d->contentName);
        return OutgoingUpdate{QList<QDomElement>() << cb.toXml(doc, "received"), [this](){
                d->setState(State::Finished);
            }
        };
    }

    QDomElement transportEl;
    OutgoingUpdateCB transportCB;

    ContentBase cb(d->creator, d->contentName);
    if (d->state == State::PrepareLocalOffer)
        cb.senders = d->senders;
    QList<QDomElement> updates;
    auto contentEl = cb.toXml(doc, "content");
    updates << contentEl;

    switch (d->updateToSend) {
    case Action::ContentReject:
    case Action::ContentRemove:
        updates << Reason(Reason::Condition::FailedTransport).toXml(doc); // do we have other reasons?
        return OutgoingUpdate{updates, [this](){
                d->setState(State::Finished);
            }
        };
    case Action::ContentAdd:
    case Action::ContentAccept:
        if (d->file.thumbnail().data.size()) {
            auto thumb = d->file.thumbnail();
            auto bm = client->bobManager();
            BoBData data = bm->append(thumb.data, thumb.mimeType);
            thumb.uri = QLatin1String("cid:") + data.cid();
            d->file.setThumbnail(thumb);
        }
        contentEl.appendChild(doc->createElementNS(NS, QString::fromLatin1("description"))).appendChild(d->file.toXml(doc));
        if (d->transport->hasUpdates()) {
            std::tie(transportEl, transportCB) = d->transport->takeOutgoingUpdate();
        } else {
            transportEl = doc->createElementNS(d->transport->pad()->ns(), QString::fromLatin1("transport"));
        }
        contentEl.appendChild(transportEl);

        d->setState(State::Unacked);
        return OutgoingUpdate{updates, [this, transportCB](){
                if (transportCB) {
                    transportCB();
                }
                d->setState(d->pad->session()->role() == Origin::Initiator? State::Pending : State::Connecting);
            }};
    case Action::TransportInfo:
        Q_ASSERT(d->transport->hasUpdates());
        std::tie(transportEl, transportCB) = d->transport->takeOutgoingUpdate();
        contentEl.appendChild(transportEl);
        return OutgoingUpdate{updates, transportCB};
    case Action::TransportReplace:
        Q_ASSERT(d->transport->hasUpdates());
        std::tie(transportEl, transportCB) = d->transport->takeOutgoingUpdate();
        contentEl.appendChild(transportEl);
        return OutgoingUpdate{updates, transportCB};
    }


    if (d->state == State::PrepareLocalOffer) { // basically when we come to this function Created is possible only for outgoing content

        if (d->transportFailedOrigin == d->pad->session()->role()) {
            d->waitTransportAccept = true;
            return OutgoingUpdate{QList<QDomElement>()<<contentEl, transportCB};
        }

    }
    if (d->state == State::Connecting || d->state == State::Active || d->state == State::Pending) {
        if (d->transport->hasUpdates()) { // failed to select next transport. can't continue
            QDomElement tel;
            OutgoingUpdateCB trCallback;
            std::tie(tel, trCallback) = d->transport->takeOutgoingUpdate();
            if (tel.isNull()) {
                qWarning("transport for content=%s reported it had updates but got null update", qPrintable(d->contentName));
                return OutgoingUpdate();
            }
            ContentBase cb(d->creator, d->contentName);
            auto cel = cb.toXml(doc, "content");
            cel.appendChild(tel);
            if (d->transportFailedOrigin != Origin::None) {
                d->waitTransportAccept = true;
            }
            return OutgoingUpdate{QList<QDomElement>()<<cel, trCallback};
        }
    }
    if (d->state == State::Finishing) {

    }
    return OutgoingUpdate(); // TODO
}

bool Application::wantBetterTransport(const QSharedPointer<Transport> &t) const
{
    Q_UNUSED(t)
    return true; // TODO check
}

bool Application::selectNextTransport()
{
    while (d->availableTransports.size()) {
        auto t = d->pad->session()->newOutgoingTransport(d->availableTransports.last());
        if (t && setTransport(t)) {
            return true;
        } else {
            d->availableTransports.removeLast();
        }
    }
    return false;
}

void Application::prepare()
{
    if (!d->transport) {
        selectNextTransport();
    }
    if (d->transport) {
        d->setState(State::PrepareLocalOffer);
        d->transport->prepare();
    }
}

void Application::start()
{
    if (d->transport) {
        d->setState(State::Connecting);
        d->transport->start();
    }
    // TODO we need QIODevice somewhere here
}

bool Application::accept(const QDomElement &el)
{
    File f(el.firstChildElement("file"));
    if (!f.isValid()) {
        return false;
    }
    d->acceptFile = f;
    // TODO validate if accept file matches to the offer
    setState(State::Accepted);
    return true;
}

bool Application::incomingTransportAccept(const QSharedPointer<Transport> &transport)
{
    if (d->transport->update(transport)) {
        d->waitTransportAccept = false;
        d->transportFailedOrigin = Origin::None;
        emit updated();
        return true;
    }
    return false;
}

bool Application::isValid() const
{
    return d->file.isValid() &&  d->contentName.size() > 0 &&
            (d->senders == Origin::Initiator || d->senders == Origin::Responder);
}

void Application::setDevice(QIODevice *dev, bool closeOnFinish)
{
    d->device = dev;
    d->closeDeviceOnFinish = closeOnFinish;
    if (d->senders == d->pad->session()->role()) {
        d->writeNextBlockToTransport();
    } else {
        d->readNextBlockFromTransport();
    }
}

Pad::Pad(Manager *manager, Session *session) :
    _manager(manager),
    _session(session)
{

}

QDomElement Pad::takeOutgoingSessionInfoUpdate()
{
    return QDomElement(); // TODO
}

QString Pad::ns() const
{
    return NS;
}

Session *Pad::session() const
{
    return _session;
}

ApplicationManager *Pad::manager() const
{
    return _manager;
}

QString Pad::generateContentName(Origin senders)
{
    QString prefix = senders == _session->role()? "fileoffer" : "filereq";
    QString name;
    do {
        name = prefix + QString("_%1").arg(qrand() & 0xffff, 4, 16, QChar('0'));
    } while (_session->content(name, _session->role()));
    return name;
}

void Pad::addOutgoingOffer(const File &file)
{
    auto selfp = _session->applicationPad(NS);
    auto app = _manager->startApplication(selfp, "ft", _session->role(), _session->role());
    app->setFile(file);
}



} // namespace FileTransfer
} // namespace Jingle
} // namespace XMPP
