#pragma once

#include <qce/kate/KateSyntaxIndex.h>

// Application-wide view of the Kate syntax data managed by qcodeedit
// (qce::kate::dataDir()). Shared by all editor tabs; the same index as
// qce::kate::syntaxIndex(), kept for the programs that use this header.
namespace qceditor::SyntaxData {

/// Index over qce::kate::syntaxDir(); loaded on first use.
const qce::kate::KateSyntaxIndex& index();

/// Re-read index.json and the syntax directory (e.g. after a download).
void reload();

} // namespace qceditor::SyntaxData
