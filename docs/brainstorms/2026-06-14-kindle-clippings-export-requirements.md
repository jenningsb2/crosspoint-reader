# Requirements — Kindle-style clippings log + clean on-demand export

*Brainstorm, 2026-06-14.*

## Context

Highlights are stored on-device per book (`annotations.bin`, with stored text + stable
ids) and exported via a menu action. Today's export **re-emits the whole book every
run**, so in single-file mode (`My Clippings.txt`) repeated exports **duplicate** the
same highlights, and deleting a highlight can't be reflected in an append-only shared
file without reconstructing across all books.

Research into Kindle's `My Clippings.txt` resolved the tension: Kindle **appends each
highlight exactly once, at creation**, into one shared file, and is **append-only** —
deleting a highlight on-device never scrubs the file (a known, accepted limitation).
Our duplication problem is therefore self-inflicted (we re-export-all on demand); Kindle
never does that. Mimicking Kindle for the shared log dissolves the duplication and the
"reconstruct across all books" problem entirely.

**Decision:** split the two concerns that were tangled together —
1. a **Kindle-compatible running log** (auto-append on create, never scrubbed), and
2. a **clean, deletion-accurate on-demand export** (regenerated per book) —
so they are independent and can be used together.

## Goals

- Restore Kindle-style zero-effort logging to `My Clippings.txt` **without duplicates**.
- Keep a clean export (JSON and/or per-book TXT) that **reflects deletions** and never
  duplicates — the modern path into Readwise / Obsidian.
- Make the two behaviors independent and individually toggleable.
- Require **no cross-book reconstruction** anywhere.

## Non-goals

- Making `My Clippings.txt` reflect deletions (explicitly Kindle parity: it does not).
- Back-filling the log with highlights made while logging was off (Kindle doesn't).
- De-duping or editing a user-hand-edited `My Clippings.txt`.
- Highlighting in the XTC reader (EPUB only).

## Behavior

### 1. Kindle log — "Append to My Clippings.txt" (new on/off setting)

- When **on**, each highlight is appended **once, at the moment it's created**, to the
  single shared `/My Clippings.txt` (Kindle block format, already implemented).
- The shared log is **append-only**: deleting a highlight updates the device + on-page
  underline but **does not** touch `My Clippings.txt`. Re-clipping the same passage
  appends a new entry (same as Kindle).
- Nothing else ever writes the shared file — the on-demand export no longer touches it,
  so duplicates are impossible by construction.

### 2. Clean export — "Export Highlights" (on-demand menu action)

- Always **regenerated per book** and **deletion-accurate** (overwrites the book's
  file(s) from current highlights). Never appends, never duplicates.
- Format chooser (existing `exportFormat`): **JSON / TXT / Both**.
  - JSON → `/clippings/<Title>.json`
  - TXT → `/clippings/<Title>.txt`
- Independent of the Kindle log: a user can have the auto-log **and** clean per-book
  exports at the same time.
- **Page locator unchanged (decided):** clippings keep the current chapter-relative page
  ("Page 12 | Chapter Three"). A book-wide page number was considered and rejected —
  reflowable EPUBs have no canonical book page (the device positions by byte-progress;
  page counts are per-chapter only), so a computed cumulative page would be expensive,
  only valid once the whole book is paginated, and unstable across font/margin changes.

### 3. Delete Book Cache — confirmation step

- Highlights now live in the book cache, so *Delete Book Cache* can wipe them. Add a
  **confirmation prompt** before clearing (reuse the existing hold/confirm dialog
  pattern from the Highlights/Bookmarks delete).
- The prompt should **state how many highlights will be removed** (e.g. "Delete cache?
  This will remove 7 highlights.") so the consequence is explicit. If exports exist they
  are unaffected; only on-device highlights are lost.

### 4. Settings changes

- **Add** `Append to My Clippings.txt` toggle (the Kindle log).
- **Retire** the `Storage: Single File / Per Book` setting — its two meanings now map to
  the log toggle (single shared file) and the always-per-book clean export.
- **Keep** `Export Format` (JSON / TXT / Both) — governs the on-demand export only.
- Migrate existing `clippingStorage`: `SINGLE_FILE` → log toggle **on**; `PER_BOOK` →
  log toggle **off** (preserves each user's current effective behavior).

## Resulting behavior matrix

| User wants | Setting(s) | Result |
|---|---|---|
| Kindle running log | Log toggle **on** | Each new highlight appended once to `/My Clippings.txt`; deletions not reflected |
| Clean, deletable export | Export Highlights (JSON/TXT/Both) | Per-book file(s) regenerated from current highlights each run |
| Both | Log on + run Export | Running log **and** clean per-book files coexist |
| Neither / private | Log off, never export | Highlights live only on-device (still underlined, still in the Highlights menu) |

## Open decisions

- **Default for the log toggle on fresh installs:** recommend **off** (clean export is
  the modern default; the log is opt-in Kindle compatibility). Migration above preserves
  existing users' behavior regardless.

## Success criteria

- Enabling the log + creating 3 highlights → `My Clippings.txt` has exactly 3 entries;
  re-reading/re-opening adds none.
- Deleting a highlight → gone from device, underline, Highlights menu, and the **next
  clean export**; **still present** in `My Clippings.txt` (Kindle parity).
- Running Export Highlights twice → per-book files identical (no duplicates).
- Log and clean export can both be active without interfering.
- *Delete Book Cache* shows a confirmation naming the highlight count; cancelling leaves
  everything intact; confirming clears the cache as before.

## Out of scope / future

- A "rebuild My Clippings.txt from all on-device books" maintenance action (would be the
  only way to scrub the shared log; deferred — adds cross-book reconstruction we
  deliberately avoided).
- Per-highlight notes/annotations; KOReader highlight sync.

## Implementation pointers (for ce-plan, not decided here)

- Auto-append path already exists in history (the original pre-`7e43dce6` behavior) —
  re-add at clip-creation in `src/activities/reader/EpubReaderActivity.cpp`, gated on the
  new toggle, reusing `ClippingsManager` Kindle-block formatting.
- `ClippingsManager::exportText` already overwrites per-book; drop the single-file
  append branch (the log path supersedes it).
- New setting in `CrossPointSettings` + `SettingsList.h`; remove `clippingStorage` usage
  in `ClippingsMenuActivity` / `ClippingsManager`; i18n string for the toggle.
- Cache-delete confirmation: the `DELETE_CACHE` case in `EpubReaderActivity::onReaderMenuConfirm`
  currently clears immediately — gate it behind a confirm prompt; highlight count is
  `annotations.size()`. i18n string for the prompt.
