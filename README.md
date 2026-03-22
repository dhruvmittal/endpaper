# Endpaper

Endpaper is a powerful, native desktop application suite designed to bridge the gap between your digital reading sources and your e-reader. It provides both a **modern Kirigami-based GUI** (`endpaper-qt`) and a **lightweight terminal interface** (`endpaper`), allowing you to collect articles from emails (KMail), RSS readers (Miniflux), and direct RSS feeds, organizing them into beautifully formatted EPUB books.

Built with **C++20**, **Qt6**, and **Kirigami**, Endpaper provides a fast, responsive, and KDE-native experience for curating your own personal digital library.

## 🚀 Features

-   **Multi-Source Ingest:**
    -   **KMail (Akonadi):** Import newsletters and long-form emails directly from your local mail store.
    -   **Miniflux:** Sync with your Miniflux RSS server, filter by category or status (Starred/Unread).
    -   **Native RSS:** Add and manage custom RSS feeds directly within the app.
-   **Book Management:**
    -   Create multiple book projects.
    -   Stage articles as chapters.
    -   Reorder, rename, or delete chapters to perfect your reading sequence.
-   **EPUB Generation:**
    -   Build high-quality EPUB files with customizable metadata (Author, Publisher).
    -   Optional content simplification for a distraction-free reading experience.
-   **Smart Caching & Persistence:**
    -   Offline caching of folder and category metadata for instant loading.
    -   Automatic background updates.
    -   State restoration: The app remembers exactly where you left off, including active tabs and dropdown selections.

## 🛠 Building from Source

Endpaper is primarily developed and packaged using **Nix**.

### Using Nix (Recommended)

To build the application and its dependencies:

```bash
nix build
./result/bin/endpaper-qt
```

To enter a development shell with all required tools and libraries:

```bash
nix develop
cmake -B build -G Ninja
cmake --build build
./build/endpaper-qt
```

### Manual Build Requirements

If you aren't using Nix, you will need:
-   **C++20** compliant compiler
-   **Qt 6.6+** (Core, Gui, Qml, Quick, QuickControls2, Network)
-   **Kirigami** & **Kirigami Addons**
-   **SQLite 3**
-   **libxml2** (for EPUB building)
-   **Akonadi** (optional, for Email support)

## 📖 How to Use

1.  **Ingest Content:** Navigate to the **KMail**, **Miniflux**, or **RSS** tabs. Select the articles or emails you want to include.
2.  **Target a Book:** Select an existing book (or create a new one in the **Books** tab) from the dropdown in the sidebar or page header.
3.  **Stage Chapters:** Use the "Stage Selected" button to add your items to the current book.
4.  **Organize:** Go to the **Books** tab to review your chapters. You can move them up/down to set the reading order.
5.  **Build:** On the **Build** tab, choose your output path, set the author/publisher, and hit **Build EPUB**. Your book is ready for your Kindle, Kobo, or mobile reader!

---

*Disclaimer: This is a single-purpose tool built to explore the capabilities of the [Antigravity](https://github.com/google-deepmind/antigravity) agentic AI coding assistant while also fulfilling a niche personal use case.*
