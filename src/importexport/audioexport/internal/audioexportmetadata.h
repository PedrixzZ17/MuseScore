/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#ifndef MU_IMPORTEXPORT_AUDIOEXPORTMETADATA_H
#define MU_IMPORTEXPORT_AUDIOEXPORTMETADATA_H

#include "audio/main/iplayback.h"
#include "global/types/bytearray.h"

#include <memory>

namespace mu::notation {
class INotation;
using INotationPtr = std::shared_ptr<INotation>;
}

namespace mu::iex::audioexport {
//! Adds container metadata after the audio renderer has produced its stream.
//! The encoded sample data is copied verbatim; only container/tag bytes change.
muse::ByteArray addAudioExportMetadata(const muse::ByteArray& audio, const notation::INotationPtr& notation,
                                       muse::audio::SoundTrackType type);
}

#endif // MU_IMPORTEXPORT_AUDIOEXPORTMETADATA_H
