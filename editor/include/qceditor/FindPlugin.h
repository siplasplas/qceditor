#pragma once

namespace spp {
class FindPlugin;
}

namespace qceditor {

/// The find bar module (spp-findbar of github.com/siplasplas/siplasplas-plugins)
/// that the editor tabs created afterwards use for searching and replacing,
/// below the text. Not owned; the program loads it (QPluginLoader) and keeps
/// it loaded. Without one (nullptr, the default) the tabs use qceditor's own
/// FindBar.
void setFindPlugin(spp::FindPlugin* plugin);
spp::FindPlugin* findPlugin();

} // namespace qceditor
