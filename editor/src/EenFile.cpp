#include "EenFile.h"

#include <botan/block_cipher.h>
#include <botan/hash.h>
#include <botan/pwdhash.h>

#include <QList>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <vector>

namespace qceditor::een {

namespace {

constexpr quint32 kIdent = 0x4E4545;          // "EEN\0"
constexpr quint32 kEditasId = 0xE4EEA4AA;
constexpr quint32 kEdiQId = 0x1479A43A;
constexpr int kEditasHeaderSize = 40;          // ident, ID, version, cipher, int64 size, MD5
constexpr int kEdiQHeaderSize = 64;            // ident, ID, version, size, Argon2 M/t/p, padding, SHA3
constexpr int kBlock = 16;

// EdiQ's Argon2id parameters for new files.
constexpr qint32 kArgonMemoryKiB = 256 * 1024;
constexpr qint32 kArgonIterations = 2;
constexpr qint32 kArgonThreads = 8;

quint32 u32(const QByteArray& b, int offset)
{
    return qFromLittleEndian<quint32>(b.constData() + offset);
}

qint32 i32(const QByteArray& b, int offset)
{
    return qFromLittleEndian<qint32>(b.constData() + offset);
}

void putU32(QByteArray& b, int offset, quint32 value)
{
    qToLittleEndian<quint32>(value, b.data() + offset);
}

QByteArray hash(const char* algorithm, const QByteArray& a, const QByteArray& b = {})
{
    auto h = Botan::HashFunction::create_or_throw(algorithm);
    h->update(reinterpret_cast<const uint8_t*>(a.constData()), a.size());
    h->update(reinterpret_cast<const uint8_t*>(b.constData()), b.size());
    const auto digest = h->final();
    return QByteArray(reinterpret_cast<const char*>(digest.data()), int(digest.size()));
}

// The password as the program that wrote the file saw it: UTF-8 (EdiQ,
// Lazarus Editas), or the system code page of the Delphi Editas.
QList<QByteArray> passwordBytes(const QString& password)
{
    QList<QByteArray> forms{password.toUtf8()};
    const QByteArray latin1 = password.toLatin1();
    if (latin1 != forms.front() && QString::fromLatin1(latin1) == password) forms << latin1;
    return forms;
}

// EdiQ: Argon2id with the password as the salt; 32 bytes.
QByteArray ediqKey(const QByteArray& password, qint32 memoryKiB, qint32 iterations, qint32 threads)
{
    auto family = Botan::PasswordHashFamily::create_or_throw("Argon2id");
    auto argon = family->from_params(size_t(memoryKiB), size_t(iterations), size_t(threads));
    QByteArray key(32, Qt::Uninitialized);
    argon->derive_key(reinterpret_cast<uint8_t*>(key.data()), key.size(),
                      password.constData(), password.size(),
                      reinterpret_cast<const uint8_t*>(password.constData()), password.size());
    return key;
}

Decrypted decryptEdiQ(const QByteArray& file, const QString& password)
{
    if (file.size() < kEdiQHeaderSize || u32(file, 8) != 1) return {};
    const qint32 size = i32(file, 12);
    const qint32 memoryKiB = i32(file, 16);
    const qint32 iterations = i32(file, 20);
    const qint32 threads = i32(file, 24);
    const QByteArray digest = file.mid(32, 32);
    const QByteArray payload = file.mid(kEdiQHeaderSize);
    // Sane parameters only: a damaged header must not ask for gigabytes.
    if (size < 0 || size > payload.size() || payload.size() % kBlock != 0
        || memoryKiB < 8 || memoryKiB > 4 * 1024 * 1024 || iterations < 1 || iterations > 64
        || threads < 1 || threads > 64)
        return {};
    for (const QByteArray& pw : passwordBytes(password)) {
        const QByteArray key = ediqKey(pw, memoryKiB, iterations, threads);
        if (hash("SHA-3(256)", key) != digest) continue;
        auto aes = Botan::BlockCipher::create_or_throw("AES-256");
        aes->set_key(reinterpret_cast<const uint8_t*>(key.constData()), key.size());
        QByteArray data = payload;
        aes->decrypt_n(reinterpret_cast<uint8_t*>(data.data()), reinterpret_cast<uint8_t*>(data.data()),
                       data.size() / kBlock);
        data.truncate(size);
        return {Status::Ok, data};
    }
    return {Status::WrongPassword, {}};
}

// DCPcrypt's CBC as Editas used it: the IV is the encrypted initial vector
// (zeros, or 0xFF in DCPcrypt 1 compatibility), a last partial block is XORed
// with the encrypted chaining value.
QByteArray dcpDecryptCbc(Botan::BlockCipher& aes, const QByteArray& input, uint8_t ivFill)
{
    uint8_t cv[kBlock];
    std::memset(cv, ivFill, kBlock);
    aes.encrypt(cv);
    QByteArray out = input;
    auto* p = reinterpret_cast<uint8_t*>(out.data());
    const int blocks = out.size() / kBlock;
    for (int i = 0; i < blocks; ++i, p += kBlock) {
        uint8_t cipher[kBlock];
        std::memcpy(cipher, p, kBlock);
        aes.decrypt(p);
        for (int j = 0; j < kBlock; ++j) p[j] ^= cv[j];
        std::memcpy(cv, cipher, kBlock);
    }
    if (const int rest = out.size() % kBlock) {
        aes.encrypt(cv);
        for (int j = 0; j < rest; ++j) p[j] ^= cv[j];
    }
    return out;
}

Decrypted decryptEditas(const QByteArray& file, const QString& password)
{
    if (file.size() < kEditasHeaderSize || u32(file, 8) != 0 || u32(file, 12) != 1) return {};
    const qint64 size = qFromLittleEndian<qint64>(file.constData() + 16);
    const QByteArray md5 = file.mid(24, 16);
    const QByteArray payload = file.mid(kEditasHeaderSize);
    if (size < 0 || size > payload.size()) return {};
    for (const QByteArray& pw : passwordBytes(password)) {
        const QByteArray key = hash("MD5", pw);
        auto aes = Botan::BlockCipher::create_or_throw("AES-128");
        aes->set_key(reinterpret_cast<const uint8_t*>(key.constData()), key.size());
        for (const uint8_t ivFill : {uint8_t(0x00), uint8_t(0xFF)}) {
            QByteArray data = dcpDecryptCbc(*aes, payload.left(int(size)), ivFill);
            if (hash("MD5", data, pw) == md5) return {Status::Ok, data};
        }
    }
    return {Status::WrongPassword, {}};
}

} // namespace

bool isEenPath(const QString& path)
{
    return path.endsWith(QLatin1String(".een"), Qt::CaseInsensitive);
}

Format format(const QByteArray& file)
{
    if (file.size() < kEditasHeaderSize || u32(file, 0) != kIdent) return Format::None;
    if (u32(file, 4) == kEditasId) return Format::Editas;
    if (u32(file, 4) == kEdiQId && file.size() >= kEdiQHeaderSize) return Format::EdiQ;
    return Format::None;
}

Decrypted decrypt(const QByteArray& file, const QString& password)
{
    try {
        switch (format(file)) {
        case Format::EdiQ: return decryptEdiQ(file, password);
        case Format::Editas: return decryptEditas(file, password);
        case Format::None: break;
        }
    } catch (const std::exception&) {
        // e.g. Argon2 memory that cannot be allocated
    }
    return {};
}

QByteArray encrypt(const QByteArray& data, const QString& password)
{
    const QByteArray pw = password.toUtf8();
    const QByteArray key = ediqKey(pw, kArgonMemoryKiB, kArgonIterations, kArgonThreads);
    QByteArray header(kEdiQHeaderSize, '\0');
    putU32(header, 0, kIdent);
    putU32(header, 4, kEdiQId);
    putU32(header, 8, 1);
    putU32(header, 12, quint32(data.size()));
    putU32(header, 16, quint32(kArgonMemoryKiB));
    putU32(header, 20, quint32(kArgonIterations));
    putU32(header, 24, quint32(kArgonThreads));
    header.replace(32, 32, hash("SHA-3(256)", key));
    // Zeros up to whole blocks, as EdiQ pads.
    QByteArray payload = data;
    payload.append(QByteArray((kBlock - payload.size() % kBlock) % kBlock, '\0'));
    auto aes = Botan::BlockCipher::create_or_throw("AES-256");
    aes->set_key(reinterpret_cast<const uint8_t*>(key.constData()), key.size());
    aes->encrypt_n(reinterpret_cast<uint8_t*>(payload.data()), reinterpret_cast<uint8_t*>(payload.data()),
                   payload.size() / kBlock);
    return header + payload;
}

} // namespace qceditor::een
