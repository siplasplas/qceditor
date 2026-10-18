#pragma once
#include <QByteArray>
#include <QString>

// Encrypted text files with the .een extension, compatible with the files of
// the earlier editors:
// - EdiQ (header ID 0x1479A43A, version 1): AES-256, the key from Argon2id
//   (the password is also the salt), its SHA3-256 in the header to check
//   the password. Read and written; new files are written in it.
// - Editas (header ID 0xE4EEA4AA, version 0, DCPcrypt): AES-128-CBC with the
//   MD5 of the password as the key, and the MD5 of the text and the password
//   in the header. Read only; saving writes the EdiQ format.
namespace qceditor::een {

/// The file name has the .een extension.
bool isEenPath(const QString& path);

enum class Format { None, Editas, EdiQ };
/// The format the header of `file` declares; None when it has no .een header
/// (e.g. an empty new file), which is then read as plain text.
Format format(const QByteArray& file);

enum class Status { Ok, WrongPassword, Invalid };
struct Decrypted
{
    Status status = Status::Invalid;
    QByteArray data;
};
/// Decrypts a file of either format.
Decrypted decrypt(const QByteArray& file, const QString& password);

/// Encrypts in the EdiQ format (Argon2id with 256 MiB, 2 passes, 8 lanes, as
/// EdiQ writes; takes about a second).
QByteArray encrypt(const QByteArray& data, const QString& password);

} // namespace qceditor::een
