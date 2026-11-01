#include <qceditor/SyntaxData.h>

#include <qce/kate/EditorSyntax.h>

// The index is qcodeedit-kate's, shared with qce::kate::EditorSyntax.
namespace qceditor::SyntaxData {

const qce::kate::KateSyntaxIndex& index()
{
    return qce::kate::syntaxIndex();
}

void reload()
{
    qce::kate::reloadSyntaxIndex();
}

} // namespace qceditor::SyntaxData
