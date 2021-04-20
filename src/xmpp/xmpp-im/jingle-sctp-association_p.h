/*
 * jignle-sctp-association_p.h - Private parto of Jingle SCTP Association
 * Copyright (C) 2021  Sergey Ilinykh
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

#include "jingle-connection.h"

#include "irisnet/noncore/sctp/DepUsrSCTP.hpp"
#include "irisnet/noncore/sctp/SctpAssociation.hpp"

#include <QHash>
#include <QQueue>

#include <mutex>

namespace XMPP { namespace Jingle { namespace SCTP {

    struct Keeper {
        using Ptr = std::shared_ptr<Keeper>;

        static std::weak_ptr<Keeper> instance;
        Keeper();
        ~Keeper();
        static Ptr use();
    };

    class Association;
    class AssociationPrivate : public QObject, RTC::SctpAssociation::Listener {
        Q_OBJECT
    public:
        Association *                   q;
        Keeper::Ptr                     keeper;
        QQueue<QByteArray>              outgoingQueue;
        std::mutex                      mutex;
        QHash<quint16, Connection::Ptr> channels; // streamId -> WebRTCDataChannel
        QQueue<Connection::Ptr>         pendingChannels;
        QQueue<Connection::Ptr>         pendingLocalChannels;
        RTC::SctpAssociation            assoc;

        bool    transportConnected = false;
        bool    useOddStreamId     = false;
        quint16 nextStreamId       = 0;
        quint16 channelsLeft       = 32768;

        AssociationPrivate(Association *q);

        void OnSctpAssociationConnecting(RTC::SctpAssociation *) override;
        void OnSctpAssociationConnected(RTC::SctpAssociation *) override;
        void OnSctpAssociationFailed(RTC::SctpAssociation *) override;
        void OnSctpAssociationClosed(RTC::SctpAssociation *) override;
        void OnSctpAssociationSendData(RTC::SctpAssociation *, const uint8_t *data, size_t len) override;
        void OnSctpAssociationMessageReceived(RTC::SctpAssociation *, uint16_t streamId, uint32_t ppid,
                                              const uint8_t *msg, size_t len) override;
        void OnSctpAssociationBufferedAmount(RTC::SctpAssociation *sctpAssociation, uint32_t len) override;
        void OnSctpStreamClosed(RTC::SctpAssociation *sctpAssociation, uint16_t streamId) override;

        void    handleIncomingDataChannelOpen(const QByteArray &data, quint16 streamId);
        bool    write(const QByteArray &data, quint16 streamId, quint32 ppid);
        void    close(quint16 streamId);
        quint16 takeNextStreamId();

    private Q_SLOTS:
        void onOutgoingData(const QByteArray &data);
        void onIncomingData(const QByteArray &data, quint16 streamId, quint32 ppid);
        void onStreamClosed(quint16 streamId);
    };

}}}
