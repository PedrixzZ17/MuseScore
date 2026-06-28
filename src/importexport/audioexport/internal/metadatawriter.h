/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */
#ifndef MU_IMPORTEXPORT_AUDIOMETADATAWRITER_H
#define MU_IMPORTEXPORT_AUDIOMETADATAWRITER_H

#include <map>
#include <memory>

#include <QByteArray>
#include <QString>

#include "audio/common/audiotypes.h"
#include "global/types/ret.h"

namespace muse::io {
class IODevice;
}

namespace mu::notation {
class INotation;
using INotationPtr = std::shared_ptr<INotation>;
}

namespace mu::iex::audioexport {

class MetadataWriter
{
public:
    static muse::Ret writeMetadata(const notation::INotationPtr& notation, muse::io::IODevice& device,
                                   muse::audio::SoundTrackType type);

private:
    using Tags = std::map<QString, QString>;

    static Tags collectTags(const notation::INotationPtr& notation);
    static muse::Ret writeId3v2(const QString& filePath, const Tags& tags);
    static muse::Ret writeWaveInfo(const QString& filePath, const Tags& tags);

public:
    static QByteArray synchsafe(quint32 size);
};
}

#endif
