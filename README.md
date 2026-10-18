# qceditor

Editor that uses qcodeedit and the shared MruTabWidget from qt-extra.

## Building

Requires C++17, CMake 3.16+, Qt 6.2+, installed qcodeedit 1.19.0 packages
(`qcodeedit`, `qcodeedit-kate`, `qcodeedit-katedata`, `qcodeedit-encoding`
with [cpg](https://github.com/siplasplas/cpg), `qcodeedit-spell` and
`qcodeedit-diff`), qt-extra 2.7 or later (moving older versions' files, see
below) and Botan 2 or 3 (`libbotan-2-dev`, for encrypted files).
Like gemini-commander, QCEditor uses `find_package(qt-extra 2.7 REQUIRED)`
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

### The editor library

The editor itself (the window `qceditor::EditorWindow` with its tabs, menus
and status bar, editor tabs with search and replace, Kate syntax, encodings
and spelling) is the static library `qceditor-editor` in `editor/`, shared
with other programs such as gemini-commander; the application is a thin
layer over it (`src/main.cpp`). A program can add its own menu items through
the window's menus, its own items to the tab bar's context menu
(`tabContextMenuRequested`), check a file before it is saved
(`setSaveValidator()`), and choose the tab limit and whether modified files
changed on disk ask to reload (`EditorWindowOptions`). `cmake --install build` installs the library and its
CMake package (not the application):

```cmake
find_package(qceditor 1.1 REQUIRED)
target_link_libraries(my_app PRIVATE qceditor::editor)
```

A program may also fetch qceditor (FetchContent) instead; with
`QCEDITOR_BUILD_APP=OFF` (the default when qceditor is not the top-level
project) only the library is built. `EditorWindow::openFile()` with a path
that does not exist yet opens an empty document saved there.

Each program keeps the editor's settings in its own place: it passes its own
`qceditor::Settings` to `qceditor::setSettings()`; without it the editor uses
the application's QSettings.

Tabs support reordering, pinning, Ctrl+Tab MRU navigation and bulk closing.
A tab is titled with its file name, made unique with a number when another
open file has the same name (`a.txt (2)`); new documents are `Untitled-1`,
`Untitled-2`, …, and the Ctrl+Tab list shows the last two path components.
The window title shows the full path. The unpinned tab limit is 20: a new tab
closes the least recently used one, without asking unless it has unsaved
changes or its file was deleted on disk; then **Save and Close**, **Close
Without Saving**, **Keep Open (Pin)** or **Cancel** (the new tab is not
opened). Closing a modified tab asks whether to save that document, and
closing a tab whose file was deleted on disk asks whether to save it back.
The tab bar's context menu also has **Close Unmodified Tabs**, **Copy File
Name** and **Copy Path**. qt-extra 2.x keeps tab state attached to the page
when tabs move and provides a built-in pin icon.

**Tools > Remove Trailing Whitespace on Save** removes spaces and tabs at the
ends of lines when a file is saved: **In Changed Lines** (the default: lines
changed since the file was opened or last saved, so untouched lines make no
stray changes, e.g. in git), **In All Lines** or **Never**. The line with the
cursor is never trimmed (typing on after a space goes on as typed), and the
removal is one undo step: Ctrl+Z after saving brings the spaces back.

**View > Wrap Lines** (on by default) and **View > Show Special Characters**
apply to all tabs and are remembered. **Tools > Insert Date**, **Insert Time**
and **Insert Both Date and Time** insert e.g. `Sunday 2026-10-18` and
`16:21:05` at the cursor. **Help > About** shows the version and where the
syntax definitions are.

The editor's context menu starts with the spelling corrections of the word
under the mouse, then **Undo**, **Redo**, **Cut**, **Copy**, **Paste**,
**Delete** and **Select All**. Long lines wrap at the window's width, with or
without syntax highlighting.

Right-click in the editor and choose **Syntax > section > language** to select
a Kate syntax definition for that tab. Sections come from the syntax index.
**Automatic** restores file-name detection; **Plain Text** disables highlighting.
Manual selection survives Save As and syntax-definition updates for the open tab.

**Theme** in the same context menu selects a downloaded Kate color theme for
the current tab, including syntax colors, editor background, normal text and
the line-number gutter. **Default** uses Breeze Light, or Breeze Dark on a
dark desktop (Breeze Light when Breeze Dark is missing); without these themes
the editor shows black text on white. Theme selection survives syntax
changes, Save As and definition updates for the open tab.

Files are opened in their own encoding: UTF-8/16/32 or a legacy code page such
as cp1250, ISO 8859-2 or cp852, detected automatically. They are saved in the
same encoding, keeping BOM, line breaks (Windows CRLF, Unix LF or classic Mac
CR) and the final line break; a file with mixed line breaks is saved with its
most frequent kind.

When detection is not confident (a few national letters are little evidence),
the file still opens at once in the most likely encoding, and a bar above the
editor says so, e.g. "Encoding uncertain: cp1250 73 %, amiga-fat 24 %".
**Choose…** opens a dialog listing the plausible encodings with their language
and probability, and a preview of the lines where they decode the file
differently; **Other encoding** adds any encoding to the comparison, and OK
reopens the file in the chosen one (asking first about unsaved edits). **Keep**,
or editing the text, closes the bar. **Encoding > Reopen with Encoding** in the
context menu lists the same encodings with their probability at the top.

**Tools > Languages of Files Not in UTF-8…** checks the languages such files are
written in (e.g. Polish and Czech): only their code pages are then considered,
which is faster and makes detection surer. With none checked the language is
guessed from the text. The choice is saved in `~/.config/siplasplas/qceditor.conf`
and applies to files opened afterwards. The language field of the status bar
shows the detected language's probability in its tooltip.

Spelling is checked as you type (**Tools > Check Spelling**, on by default and
remembered): misspelled words get a wavy red underline, in source code only in
comments, strings and documentation, in plain text everywhere; identifiers,
addresses and paths are not checked. Right-clicking a misspelled word offers up
to 8 suggestions (choosing one replaces the word, undone in one step), **Add to
Dictionary** (a user dictionary shared with other siplasplas programs,
`~/.local/share/siplasplas/spell/`) and **Ignore** (this document, this session).
Alt+Enter opens the same choices for the misspelled word at the cursor, below
it, to pick with the keyboard. Usually there is one suggestion, three at most:
only those nearly as good as the best are offered (qcodeedit 1.20). A space
missing or typed in the wrong place comes first: for "n aoknie" the menu on
"aoknie" offers "na oknie", for "mam yz" it offers "mamy z", replacing both
words.
Common words come before rare ones ("dialac": działać, not dialach) once the
list of the most frequent words of the language is there. The first time a
dictionary without one is used, the editor asks whether to download it
(FrequencyWords, from film subtitles, CC-BY-SA 4.0, under 1 MB per language,
from GitHub, into `~/.local/share/word-frequencies`, shared with other
programs); after a Yes the lists of other languages are downloaded when first
needed, without asking. **Tools > Download Word Frequencies for Spelling**
turns this on or off. Without the lists suggestions work as before.
The language is the detected one; **Spelling Language** in the context menu
chooses another for the document, from the recently used ones, up to two at
once (e.g. English + Polish), or **More…** for every dictionary hunspell has
installed. Other languages are added by installing a hunspell dictionary (e.g.
`sudo apt install hunspell-de-de`); a missing one is named in the status bar.
Building needs `libhunspell-dev` (qcodeedit-spell).

The status bar shows `line:column` (the column in characters), the code of the
character after the cursor (e.g. `U+0105`), the line breaks (**Unix (LF)**,
**Windows (CRLF)**, **Mac (CR)** or **Mixed**), the encoding (**UTF-8**,
**UTF-8 BOM** or the code page), the text's language, and the tab width. The
language is detected in the background after the file is shown, so opening is
not slowed down; source code usually comes out as English. Typing or pasting characters the code page cannot store asks
whether to write them as `?` or switch the file to UTF-8. **Encoding** in the
context menu reopens the file in another encoding or saves it in another one
(e.g. UTF-8) from now on.

The line with the caret has a slightly darker background (lighter on dark
themes).

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

Ctrl+R (or Ctrl+H) opens the replacement row. The arrow beside the search field expands
or collapses it. **Replace** changes the active result; **Replace All** changes
all included results in one undo step. **Exclude** skips the active result until
the query/options or document change. **Aa** preserves upper/lower/title case.
Regex replacements support `$1`, `\1`, `${name}`, and `\n`/`\t` escapes.
Replacement respects selection-only search and can be undone in the editor.

Navigating to a search result expands every collapsed fold hiding that result,
including nested folds and folds intersecting a multiline match. Other folds
keep their current state.

Ctrl+G opens the standard Qt input dialog with the current one-based
`line:column` selected. Enter `line:column` or just `line` (column 1); the
column counts characters, as the status bar does (an emoji is one). The
drop-down lists positions entered before, without spaces, most recent first;
they are kept only while the application runs.
OK moves the cursor and reveals any folds hiding the destination; Cancel leaves
the position unchanged. Invalid or out-of-range positions disable OK.

**File > Recent Files** lists up to 20 recently closed files, newest first,
without duplicates. Untitled documents and cancelled closes are not recorded.
Closing the application records its remaining files, with the active file first.
Selecting an entry reopens it (or activates its existing tab); missing files are
shown disabled. Paths are normalized so relative paths and symlinks to the same
file do not create duplicate entries.

The list is saved atomically as `recentFiles` in
`~/.local/share/siplasplas/qceditor/recent-files.json` (the application's data
directory, honoring `XDG_DATA_HOME`). Other JSON keys in it are preserved.

### Encrypted files (.een)

A file with the `.een` extension is encrypted on disk. Opening it asks for
its password (any text; a wrong one asks again); the text is decrypted only
in memory. QCEditor reads the files of the earlier editors EdiQ (AES-256 with
an Argon2id key) and Editas (AES-128 with an MD5 key, an older and weaker
format), and always saves in EdiQ's format, so an Editas file is converted
the first time it is saved; EdiQ still opens what QCEditor saves. Saving a new
`.een` file asks for a password twice; a new password may contain only
visible ASCII characters (letters, digits, punctuation, no spaces), so that
every program and keyboard gives the same bytes. **File > Change Password…**
sets a new password, used when the file is saved next. Saving the text under
a name without `.een` asks first, as it would be written unencrypted. A
`.een` file without the encryption header (e.g. new) is read as plain text
and encrypted when saved.

### Where QCEditor keeps its files

Like the other siplasplas programs, QCEditor keeps its configuration (settings,
such as declining the syntax download) in `~/.config/siplasplas/qceditor.conf`
and `~/.config/siplasplas/qceditor/`, and its data (the recent files) in
`~/.local/share/siplasplas/qceditor/`. The files of older versions,
`~/.local/share/qceditor/config.json` and `~/.config/qceditor/QCEditor.conf`, are
moved there once at start-up (qt-extra's `qxMigrateToSiplasplas()`) and the
emptied old directories removed; files there that QCEditor does not recognize are
moved only when you agree.

The Kate syntax definitions and themes are qcodeedit's shared data, in
`~/.local/share/kate-data/<version>/` (usable by other programs too). Up to
qcodeedit 1.12 they were in `~/.local/share/qcodeedit/kate-<version>/`; QCEditor
moves them once at start-up (`qce::kate::migrateDataRoot()`), so they are not
downloaded again.

When the main window becomes active (for example after Alt+Tab), and every
second while it is, QCEditor compares modification times and sizes of all
files open in tabs with their last read/saved versions. Changed files reload
automatically unless the tab has unsaved edits; then, with **Tools > Ask to
Reload Modified Files** (on by default), a question asks whether to load the
disk version (Ctrl+Z brings the edits back); after **No** it is not asked
again until the file changes once more. With the option off such a tab is
left as it is.
A reload changes only the lines that differ on disk, as one undo step: Ctrl+Z
brings back the text before it, and the earlier undo history stays. The
cursor and the selection follow the text they were in (a line added above
moves them down), and the tab's syntax/theme choices stay. Show events alone
do not trigger this check; untitled and missing files are skipped. A tab is
modified when its text differs from the saved one: undoing back to the saved
text makes it unmodified again.
