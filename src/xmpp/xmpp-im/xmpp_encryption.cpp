/*
 * Copyright (C) 2021  Sergey Ilinykh
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "xmpp_encryption.h"

namespace XMPP {

class EncryptionManager::Private {
public:
    QList<EncryptionMethod *> methods;
};

EncryptionManager::~EncryptionManager() { }

void EncryptionManager::registerMethod(EncryptionMethod *algo) { d->methods.append(algo); }

void EncryptionManager::unregisterMethod(EncryptionMethod *algo) { d->methods.removeOne(algo); }

EncryptionManager::MethodsMap EncryptionManager::methods(EncryptionMethod::Capabilities caps) const
{
    MethodsMap ret;
    for (auto const &m : qAsConst(d->methods)) {
        if (caps & m->capabilities())
            ret[m->id()] = m->name();
    }
    return ret;
}

}
