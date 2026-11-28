# Endpaper

Endpaper is a native Linux desktop and terminal application that turns newsletters, Miniflux feeds, and RSS/Atom articles into e-ink-friendly EPUB books. It provides a keyboard-first **Qt Widgets 3-pane workspace** (`endpaper-qt`) and a **terminal interface** (`endpaper`).

## Features

- **Multi-Source Ingest:**
  - **KMail (Akonadi):** Browse collapsible account/folder trees, pin favorite folders to the top, hide unused/system folders, and import emails directly from your local Akonadi store.
  - **Miniflux:** Sync with a Miniflux server, filter by category, read/unread status, starred flag, or search query, and fetch full article HTML on stub entries.
  - **Direct RSS / Atom:** Manage RSS/Atom feed subscriptions locally and resolve stub articles from their source URLs.
- **Book Queue & EPUB 3.0 Building:**
  - Create multiple book projects persisted in SQLite (`~/.local/share/endpaper/library.db`).
  - Stage checked articles as chapters, reorder via drag-and-drop or keyboard (`Alt+Up` / `Alt+Down`), auto-sort numbered parts, or reverse order.
  - Export clean EPUB 3.0 books (new build or append to an existing EPUB), with optional HTML simplification and grayscale image resizing/compression for e-ink screens.
- **Local Config & Caching:**
  - XDG-compliant configuration (`~/.config/endpaper/endpaper.conf`).
  - Offline folder/category caching, theme-respecting KDE/Wayland icons, and optional session state restoration.

## Building from Source

### Using Nix

```bash
nix build .#all
./result/bin/endpaper-qt
```

Development shell with all build tools and KDE Qt integration:

```bash
nix develop
cmake -B build -G Ninja
cmake --build build
./build/endpaper-qt
```

### Manual Build Requirements

- **C++20** compiler and **CMake 3.25+**
- **Qt 6** (`Core`, `Gui`, `Widgets`)
- **KDE Frameworks 6 / PIM**: `extra-cmake-modules`, `KPim6Akonadi`, `KPim6AkonadiMime`, `KF6Mime` (or `KPim6Mime`)
- **Libraries:** `sqlite3`, `libzip`, `pugixml`, `lexbor`, `libcurl`, `nlohmann_json`
- **FTXUI** (fetched automatically via CMake if not installed locally, when `BUILD_TUI=ON`)

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Workspace Shortcuts (`endpaper-qt`)

1. **Switch source (`Ctrl+1`–`3` / `F1`–`F3`):** Pick **KMail**, **Miniflux**, or **RSS**, then choose a folder/category/feed in the left pane (`Refresh`: `Ctrl+R` / `F5`).
2. **Filter & Select (`Ctrl+F`, `Space`, `Ctrl+A`):** Filter by title/author (fuzzy tokens or regex), toggle checkboxes with `Space`, or double-click an article to stage it immediately.
3. **Target a Book & Stage (`Ctrl+N`, `Ctrl+Enter`):** Pick or create the active book in the top toolbar and stage checked articles into the right-hand **Queue** sidebar (`Ctrl+\` / `F4` toggles it).
4. **Reorder & Build (`Alt+Up`/`Down`, `Ctrl+B`):** Arrange chapters, adjust author/publisher/output or e-ink options, and click **Build EPUB** (`Ctrl+,` opens Settings).

---

*Built to explore [Antigravity](https://github.com/google-deepmind/antigravity) while solving a real reading-to-e-ink workflow.*
