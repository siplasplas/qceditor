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
