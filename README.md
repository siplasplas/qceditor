# qceditor

Editor that uses qcodeedit and the shared MruTabWidget from qt-extra.

## Building

Requires C++17, CMake 3.16+, Qt 6.2+, installed qcodeedit packages
(`qcodeedit`, `qcodeedit-kate`, `qcodeedit-katedata`) and qt-extra 2.x.
Like gemini-commander, QCEditor uses `find_package(qt-extra 2 REQUIRED)`
and links the installed `qt-extra` target; no widget sources are copied here.

Build and install [qt-extra](https://github.com/siplasplas/qt-extra) first
if it is not already installed:

```sh
cmake -S /path/to/qt-extra -B /path/to/qt-extra/build
cmake --build /path/to/qt-extra/build
sudo cmake --install /path/to/qt-extra/build
```

Then build and run QCEditor:

```sh
cmake -S . -B build
cmake --build build
./build/qceditor [file ...]
```

For dependencies installed outside the default search paths, pass
`-DCMAKE_PREFIX_PATH=/path/to/prefix` when configuring.

Tabs support reordering, pinning, Ctrl+Tab MRU navigation and bulk closing.
The unpinned tab limit is 20. Closing a modified tab asks whether to save
that document, including when it is not the active tab. qt-extra 2.x keeps
tab state attached to the page when tabs move and provides a built-in pin icon.

Right-click in the editor and choose **Syntax > section > language** to select
a Kate syntax definition for that tab. Sections come from the syntax index.
**Automatic** restores file-name detection; **Plain Text** disables highlighting.
Manual selection survives Save As and syntax-definition updates for the open tab.

**Theme** in the same context menu selects a downloaded Kate color theme for
the current tab, including syntax colors, editor background and normal text.
**Default** restores the original palette. Theme selection survives syntax
changes, Save As and definition updates for the open tab.

Use **Search > Find** (Ctrl+F) to open the inline search bar above the current
editor. It shows the current/total match count, marks all matches and emphasizes
the current one. Use the arrows, Enter/F3 or Shift+Enter/Shift+F3 to navigate.
Options include wrap around, case sensitivity, whole words, regular expressions
and searching inside the current selection. Escape closes the bar. Each tab
keeps its own query and search options; search does not modify document contents.

Search uses qcodeedit's `setSelection` and `setExtraSelections` APIs (requires
the installed component with commit `69e2760` or later). Result navigation
selects the matched text; match decorations preserve syntax colors and do not
re-tokenize the document. The original selection-only scope is captured before
navigation changes the editor selection.

Ctrl+R opens the replacement row. The arrow beside the search field expands
or collapses it. **Replace** changes the active result; **Replace All** changes
all included results in one undo step. **Exclude** skips the active result until
the query/options or document change. **Aa** preserves upper/lower/title case.
Regex replacements support `$1`, `\1`, `${name}`, and `\n`/`\t` escapes.
Replacement respects selection-only search and can be undone in the editor.

Navigating to a search result expands every collapsed fold hiding that result,
including nested folds and folds intersecting a multiline match. Other folds
keep their current state.

Ctrl+G opens the standard Qt input dialog with the current one-based
`line:column` selected. Enter `line:column` or just `line` (column 1).
OK moves the cursor and reveals any folds hiding the destination; Cancel leaves
the position unchanged. Invalid or out-of-range positions disable OK.

**File > Recent Files** lists up to 20 recently closed files, newest first,
without duplicates. Untitled documents and cancelled closes are not recorded.
Closing the application records its remaining files, with the active file first.
Selecting an entry reopens it (or activates its existing tab); missing files are
shown disabled. Paths are normalized so relative paths and symlinks to the same
file do not create duplicate entries.

The list is saved atomically as `recentFiles` in a JSON `config.json` under
`QStandardPaths::GenericDataLocation/qceditor`, normally
`~/.local/share/qceditor/config.json` on Linux (honoring `XDG_DATA_HOME`).
Other JSON configuration keys are preserved.
