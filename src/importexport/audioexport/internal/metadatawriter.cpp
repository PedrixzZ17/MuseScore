/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */
#include "metadatawriter.h"

#include <QFile>

#include "engraving/dom/score.h"
#include "notation/inotation.h"
#include "notation/inotationelements.h"

#include "global/io/iodevice.h"
#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace mu::engraving;
using namespace mu::iex::audioexport;

namespace {
static QString tag(const Score* score, const char16_t* name)
{
    return score ? score->metaTag(name).toQString().trimmed() : QString();
}

static void appendTextFrame(QByteArray& out, const QByteArray& id, const QString& value)
{
    if (value.isEmpty()) {
        return;
    }

    QByteArray payload;
    payload.append('\x03'); // UTF-8 text encoding for ID3v2.4
    payload.append(value.toUtf8());

    out.append(id.leftJustified(4, ' ', true));
    out.append(MetadataWriter::synchsafe(static_cast<quint32>(payload.size())));
    out.append("\0\0", 2);
    out.append(payload);
}

static QByteArray riffChunk(const QByteArray& id, const QByteArray& payload)
{
    QByteArray result;
    result.append(id.left(4));
    quint32 size = static_cast<quint32>(payload.size());
    result.append(reinterpret_cast<const char*>(&size), 4);
    result.append(payload);
    if (payload.size() % 2) {
        result.append('\0');
    }
    return result;
}
}

Ret MetadataWriter::writeMetadata(const notation::INotationPtr& notation, io::IODevice& device, SoundTrackType type)
{
    const QString filePath = QString::fromStdString(device.meta("file_path"));
    if (filePath.isEmpty()) {
        return make_ok();
    }

    const Tags tags = collectTags(notation);

    switch (type) {
    case SoundTrackType::MP3:
        return writeId3v2(filePath, tags);
    case SoundTrackType::WAV:
        return writeWaveInfo(filePath, tags);
    case SoundTrackType::FLAC:
    case SoundTrackType::OGG:
        LOGW() << "Advanced audio metadata for this container requires encoder-native Vorbis comment support";
        return make_ok();
    default:
        return make_ok();
    }
}

MetadataWriter::Tags MetadataWriter::collectTags(const notation::INotationPtr& notation)
{
    const Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;

    Tags tags;
    tags["TITLE"] = tag(score, u"workTitle");
    tags["SUBTITLE"] = tag(score, u"subtitle");
    tags["COMPOSER"] = tag(score, u"composer");
    tags["ARRANGER"] = tag(score, u"arranger");
    tags["LYRICIST"] = tag(score, u"lyricist");
    tags["ALBUM"] = tag(score, u"movementTitle");
    if (tags["ALBUM"].isEmpty() && notation) {
        tags["ALBUM"] = notation->projectName();
    }
    tags["GENRE"] = tag(score, u"genre");
    tags["DATE"] = tag(score, u"year");
    if (tags["DATE"].isEmpty()) {
        tags["DATE"] = tag(score, u"creationDate").left(4);
    }
    tags["COPYRIGHT"] = tag(score, u"copyright");
    tags["TRACKNUMBER"] = tag(score, u"trackNumber");
    tags["COMMENT"] = tag(score, u"comment");
    if (tags["COMMENT"].isEmpty()) {
        tags["COMMENT"] = QStringLiteral("Exported from MuseScore");
    }
    return tags;
}

QByteArray MetadataWriter::synchsafe(quint32 size)
{
    QByteArray out;
    out.append(char((size >> 21) & 0x7f));
    out.append(char((size >> 14) & 0x7f));
    out.append(char((size >> 7) & 0x7f));
    out.append(char(size & 0x7f));
    return out;
}

Ret MetadataWriter::writeId3v2(const QString& filePath, const Tags& tags)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return make_ret(Ret::Code::UnknownError, "Unable to open MP3 for metadata writing");
    }
    QByteArray audio = file.readAll();
    file.close();

    if (audio.startsWith("ID3") && audio.size() >= 10) {
        const int tagSize = ((audio[6] & 0x7f) << 21) | ((audio[7] & 0x7f) << 14) | ((audio[8] & 0x7f) << 7) | (audio[9] & 0x7f);
        audio.remove(0, 10 + tagSize);
    }

    QByteArray frames;
    appendTextFrame(frames, "TIT2", tags.at("TITLE"));
    appendTextFrame(frames, "TIT3", tags.at("SUBTITLE"));
    appendTextFrame(frames, "TCOM", tags.at("COMPOSER"));
    appendTextFrame(frames, "TPE4", tags.at("ARRANGER"));
    appendTextFrame(frames, "TEXT", tags.at("LYRICIST"));
    appendTextFrame(frames, "TALB", tags.at("ALBUM"));
    appendTextFrame(frames, "TCON", tags.at("GENRE"));
    appendTextFrame(frames, "TDRC", tags.at("DATE"));
    appendTextFrame(frames, "TCOP", tags.at("COPYRIGHT"));
    appendTextFrame(frames, "TRCK", tags.at("TRACKNUMBER"));
    if (!tags.at("COMMENT").isEmpty()) {
        QByteArray comment;
        comment.append('\x03');
        comment.append("eng\0", 4);
        comment.append(tags.at("COMMENT").toUtf8());
        frames.append("COMM", 4);
        frames.append(synchsafe(static_cast<quint32>(comment.size())));
        frames.append("\0\0", 2);
        frames.append(comment);
    }

    QByteArray id3("ID3\x04\0\0", 6);
    id3.append(synchsafe(static_cast<quint32>(frames.size())));
    id3.append(frames);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return make_ret(Ret::Code::UnknownError, "Unable to update MP3 metadata");
    }
    file.write(id3);
    file.write(audio);
    return make_ok();
}

Ret MetadataWriter::writeWaveInfo(const QString& filePath, const Tags& tags)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite)) {
        return make_ret(Ret::Code::UnknownError, "Unable to open WAV for metadata writing");
    }

    QByteArray listPayload("INFO", 4);
    const std::map<QString, QByteArray> riffMap {
        { "TITLE", "INAM" }, { "SUBTITLE", "ISBJ" }, { "COMPOSER", "IART" }, { "ARRANGER", "IENG" },
        { "LYRICIST", "IWRI" }, { "ALBUM", "IPRD" }, { "GENRE", "IGNR" }, { "DATE", "ICRD" },
        { "COPYRIGHT", "ICOP" }, { "TRACKNUMBER", "IPRT" }, { "COMMENT", "ICMT" }
    };
    for (const auto& item : riffMap) {
        const QString value = tags.at(item.first);
        if (!value.isEmpty()) {
            listPayload.append(riffChunk(item.second, value.toUtf8() + '\0'));
        }
    }

    if (listPayload.size() == 4) {
        return make_ok();
    }

    file.seek(file.size());
    file.write(riffChunk("LIST", listPayload));

    const quint32 riffSize = static_cast<quint32>(file.size() - 8);
    file.seek(4);
    file.write(reinterpret_cast<const char*>(&riffSize), 4);
    return make_ok();
}
