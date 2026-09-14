/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#include "audioexportmetadata.h"

#include <QByteArray>
#include <QString>
#include <limits>

#include "engraving/dom/score.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"

using namespace muse;
using namespace mu;
using namespace mu::iex::audioexport;

namespace {
struct Metadata {
    QString title, artist, composer, arranger, lyricist, copyright, year, source, description, comment;
};

Metadata scoreMetadata(const notation::INotationPtr& notation)
{
    Metadata result;
    if (!notation || !notation->masterNotation()) {
        return result;
    }
    engraving::MasterScore* score = notation->masterNotation()->masterScore();
    if (!score) {
        return result;
    }
    auto tag = [score](const char* name) { return score->metaTag(String::fromUtf8(name)).toQString(); };
    result.title = tag("workTitle");
    result.artist = tag("artist");
    result.composer = tag("composer");
    result.arranger = tag("arranger");
    result.lyricist = tag("lyricist");
    result.copyright = tag("copyright");
    result.year = tag("year");
    if (result.year.isEmpty()) {
        result.year = tag("creationDate").left(4);
    }
    result.source = tag("source");
    result.description = tag("description");
    result.comment = tag("comment");
    return result;
}

void appendLe32(QByteArray& data, quint32 value)
{
    for (int i = 0; i != 4; ++i) {
        data.append(char((value >> (i * 8)) & 0xff));
    }
}

quint32 le32(const QByteArray& data, int offset)
{
    return quint8(data[offset]) | (quint32(quint8(data[offset + 1])) << 8) | (quint32(quint8(data[offset + 2])) << 16)
           | (quint32(quint8(data[offset + 3])) << 24);
}

QByteArray synchsafe(quint32 value)
{
    QByteArray result;
    for (int i = 3; i >= 0; --i) {
        result.append(char((value >> (i * 7)) & 0x7f));
    }
    return result;
}

void appendId3TextFrame(QByteArray& frames, const char id[5], const QString& value)
{
    if (value.isEmpty()) {
        return;
    }
    const QByteArray text = value.toUtf8();
    QByteArray payload(1, char(3)); // UTF-8
    payload += text;
    frames.append(id, 4);
    frames += synchsafe(payload.size());
    frames.append("\0\0", 2);
    frames += payload;
}

void appendId3UserTextFrame(QByteArray& frames, const QString& description, const QString& value)
{
    if (value.isEmpty()) {
        return;
    }
    QByteArray payload(1, char(3)); // UTF-8
    payload += description.toUtf8();
    payload += '\0';
    payload += value.toUtf8();
    frames += "TXXX";
    frames += synchsafe(payload.size());
    frames.append("\0\0", 2);
    frames += payload;
}

QByteArray addMp3Metadata(const QByteArray& audio, const Metadata& m)
{
    QByteArray frames;
    appendId3TextFrame(frames, "TIT2", m.title);
    appendId3TextFrame(frames, "TPE1", m.artist);
    appendId3TextFrame(frames, "TCOM", m.composer);
    appendId3TextFrame(frames, "TPE4", m.arranger);
    appendId3TextFrame(frames, "TEXT", m.lyricist);
    appendId3TextFrame(frames, "TCOP", m.copyright);
    appendId3TextFrame(frames, "TDRC", m.year);
    appendId3TextFrame(frames, "TENC", QStringLiteral("MuseScore Studio"));
    appendId3UserTextFrame(frames, QStringLiteral("SOURCE"), m.source);
    appendId3UserTextFrame(frames, QStringLiteral("DESCRIPTION"), m.description);
    if (!m.comment.isEmpty()) {
        const QByteArray text = m.comment.toUtf8();
        QByteArray payload("\3eng\0", 5); // UTF-8, language and empty description
        payload += text;
        frames += "COMM";
        frames += synchsafe(payload.size());
        frames.append("\0\0", 2);
        frames += payload;
    }
    QByteArray result("ID3\4\0\0", 6);
    result += synchsafe(frames.size());
    result += frames;
    result += audio;
    return result;
}

void appendVorbisComment(QByteArray& comments, const char* name, const QString& value)
{
    if (value.isEmpty()) {
        return;
    }
    const QByteArray comment = QByteArray(name) + '=' + value.toUtf8();
    appendLe32(comments, comment.size());
    comments += comment;
}

QByteArray vorbisCommentBlock(const Metadata& m)
{
    QByteArray entries;
    appendVorbisComment(entries, "TITLE", m.title); appendVorbisComment(entries, "ARTIST", m.artist);
    appendVorbisComment(entries, "COMPOSER", m.composer); appendVorbisComment(entries, "ARRANGER", m.arranger);
    appendVorbisComment(entries, "LYRICIST", m.lyricist); appendVorbisComment(entries, "COPYRIGHT", m.copyright);
    appendVorbisComment(entries, "DATE", m.year); appendVorbisComment(entries, "SOURCE", m.source);
    appendVorbisComment(entries, "DESCRIPTION", m.description); appendVorbisComment(entries, "COMMENT", m.comment);
    appendVorbisComment(entries, "ENCODER", QStringLiteral("MuseScore Studio"));
    QByteArray result;
    const QByteArray vendor("MuseScore Studio");
    appendLe32(result, vendor.size()); result += vendor;
    // Count the little-endian length-prefixed entries.
    int count = 0;
    for (int offset = 0; offset < entries.size();) { ++count; offset += 4 + int(le32(entries, offset)); }
    appendLe32(result, count); result += entries;
    return result;
}

QByteArray addFlacMetadata(const QByteArray& audio, const Metadata& m)
{
    if (!audio.startsWith("fLaC") || audio.size() < 8) {
        return audio;
    }
    QByteArray result("fLaC");
    int offset = 4;
    bool last = false;
    while (!last && offset + 4 <= audio.size()) {
        last = (quint8(audio[offset]) & 0x80) != 0;
        const quint32 length = (quint8(audio[offset + 1]) << 16) | (quint8(audio[offset + 2]) << 8) | quint8(audio[offset + 3]);
        if (offset + 4 + int(length) > audio.size()) return audio;
        QByteArray header = audio.mid(offset, 4);
        if (last) header[0] = char(quint8(header[0]) & 0x7f);
        result += header + audio.mid(offset + 4, length);
        offset += 4 + length;
    }
    const QByteArray comments = vorbisCommentBlock(m);
    result.append(char(0x80 | 4)); // last metadata block, VORBIS_COMMENT
    result.append(char((comments.size() >> 16) & 0xff)); result.append(char((comments.size() >> 8) & 0xff)); result.append(char(comments.size() & 0xff));
    result += comments;
    result += audio.mid(offset);
    return result;
}

QByteArray addWavMetadata(const QByteArray& audio, const Metadata& m)
{
    if (!audio.startsWith("RIFF") || audio.mid(8, 4) != "WAVE" || audio.size() < 12) return audio;
    QByteArray info;
    auto add = [&info](const char id[5], const QString& value) {
        if (value.isEmpty()) return;
        QByteArray text = value.toUtf8(); text.append('\0');
        info.append(id, 4); appendLe32(info, text.size()); info += text;
        if (text.size() & 1) info.append('\0');
    };
    add("INAM", m.title); add("IART", m.artist); add("ICRD", m.year); add("ICOP", m.copyright); add("ICMT", m.comment);
    add("ISFT", QStringLiteral("MuseScore Studio"));
    QByteArray chunk("LIST"); appendLe32(chunk, info.size() + 4); chunk += "INFO"; chunk += info;
    QByteArray result = audio + chunk;
    const quint64 riffSize = quint64(result.size()) - 8;
    if (riffSize > std::numeric_limits<quint32>::max()) return audio;
    result[4] = char(riffSize & 0xff); result[5] = char((riffSize >> 8) & 0xff);
    result[6] = char((riffSize >> 16) & 0xff); result[7] = char((riffSize >> 24) & 0xff);
    return result;
}
}

ByteArray mu::iex::audioexport::addAudioExportMetadata(const ByteArray& audio, const notation::INotationPtr& notation,
                                                        audio::SoundTrackType type)
{
    const QByteArray encoded = audio.toQByteArrayNoCopy();
    const Metadata metadata = scoreMetadata(notation);
    switch (type) {
    case audio::SoundTrackType::MP3: return ByteArray::fromQByteArray(addMp3Metadata(encoded, metadata));
    case audio::SoundTrackType::FLAC: return ByteArray::fromQByteArray(addFlacMetadata(encoded, metadata));
    case audio::SoundTrackType::WAV: return ByteArray::fromQByteArray(addWavMetadata(encoded, metadata));
    default: return audio; // Ogg needs packet re-pagination; leave it to its native encoder.
    }
}
