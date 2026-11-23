// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <memory>

#include "mdtypes.h"

namespace mmm {

// Talks to one NetMD recorder (M10). Synchronous and slow (USB commands can take seconds):
// only ever used from the NetMD worker thread (NetMdManager), never from two threads.
// The real one wraps libnetmd (LibNetMdBackend); tests use an in-memory fake.
class NetMdBackend
{
public:
    virtual ~NetMdBackend() = default;

    virtual NetMdError open(const NetMdDevice &device) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Everything on the disc: header (title + groups), tracks, capacity.
    virtual NetMdError readDisc(MdDisc *disc) = 0;
    virtual NetMdError readHeader(QString *raw) = 0;
    virtual NetMdError readTrackTitle(int index, QString *title) = 0;

    // Titles must already be clean (MdTitle::clean / withTitle).
    virtual NetMdError writeHeader(const QString &raw) = 0;
    virtual NetMdError writeTrackTitle(int index, const QString &title) = 0;
};

// Makes a backend on the worker thread (each recorder session gets its own).
using NetMdBackendFactory = std::function<std::unique_ptr<NetMdBackend>()>;

// The libnetmd one.
std::unique_ptr<NetMdBackend> makeLibNetMdBackend();

} // namespace mmm
