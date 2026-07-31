# Automatic reading-progress sync research

## Question and conclusion

Automatic progress sync is feasible, but it should not be implemented as a resident background task. The current sync path is intentionally a foreground activity: it releases the loaded EPUB and section to recover about 65 KB for TLS, performs one or more network requests, turns off Wi-Fi after an upload, and silently restarts the reader on exit to clear Wi-Fi-related heap fragmentation ([`src/activities/reader/EpubReaderActivity.cpp:1191-1209`](../src/activities/reader/EpubReaderActivity.cpp#L1191-L1209), [`src/activities/reader/KOReaderSyncActivity.cpp:336-343`](../src/activities/reader/KOReaderSyncActivity.cpp#L336-L343), [`src/activities/reader/KOReaderSyncActivity.cpp:390-397`](../src/activities/reader/KOReaderSyncActivity.cpp#L390-L397)).

The best first version is therefore an opt-in **Smart sync when sleeping** mode for EPUBs:

- Trigger only when sleep was requested from the EPUB reader.
- Reuse Smart Sync's existing "furthest progress wins" decision.
- Attempt only saved Wi-Fi credentials, without opening an interactive network picker.
- Bound the entire attempt; on no network, low memory, or any server error, continue sleeping.
- Keep the EPUB and Wi-Fi/TLS lifetimes mutually exclusive, as the manual flow does today.
- Do not keep Wi-Fi on, add a periodic FreeRTOS task, or wake the device from deep sleep.

A page-count option can be added later, but automatically interrupting reading every _N_ page turns would currently unload/reload the book and invoke the post-Wi-Fi restart. A lower-impact interpretation is to mark sync "due" in RAM after _N_ forward page changes, then execute it at the next sleep or reader exit. Local progress is already written on every changed rendered page, so no extra per-page SD write is necessary ([`src/activities/reader/EpubReaderActivity.cpp:1677-1686`](../src/activities/reader/EpubReaderActivity.cpp#L1677-L1686)).

## How manual sync works today

### Entry and local-position preparation

The reader exposes sync from its menu and as an optional long-press action ([`src/activities/reader/EpubReaderActivity.cpp:549-570`](../src/activities/reader/EpubReaderActivity.cpp#L549-L570), [`src/activities/reader/EpubReaderActivity.cpp:966-968`](../src/activities/reader/EpubReaderActivity.cpp#L966-L968)). `launchKOReaderSync()`:

1. Does nothing if KOReader credentials are absent.
2. Captures the current spine, page, chapter page count, and an optional paragraph index.
3. Converts the local CrossPoint position to the KOReader percentage/XPath form while the EPUB is still loaded.
4. Atomically persists the local page and aborts sync if that save fails.
5. Releases `Section` and `Epub` before replacing the reader with `KOReaderSyncActivity`.

Those behaviors are in [`src/activities/reader/EpubReaderActivity.cpp:1161-1210`](../src/activities/reader/EpubReaderActivity.cpp#L1161-L1210). Position conversion may inspect the EPUB to resolve an XPath, so it belongs before the EPUB release ([`lib/KOReaderSync/ProgressMapper.cpp:706-724`](../lib/KOReaderSync/ProgressMapper.cpp#L706-L724)).

The local resume file is six bytes—16-bit spine index, page, and chapter page count—and is replaced atomically through `ProgressFile::writeAtomic()` ([`src/activities/reader/EpubReaderUtils.h:10-28`](../src/activities/reader/EpubReaderUtils.h#L10-L28)). On reader entry, the same file restores spine/page state ([`src/activities/reader/EpubReaderActivity.cpp:185-208`](../src/activities/reader/EpubReaderActivity.cpp#L185-L208)).

### Wi-Fi and requests

The sync activity launches `WifiSelectionActivity` unless Wi-Fi is already connected ([`src/activities/reader/KOReaderSyncActivity.cpp:363-387`](../src/activities/reader/KOReaderSyncActivity.cpp#L363-L387)). The selection activity can automatically try the last successful saved SSID, then scan and try other visible saved networks ([`src/activities/network/WifiSelectionActivity.cpp:56-73`](../src/activities/network/WifiSelectionActivity.cpp#L56-L73), [`src/activities/network/WifiSelectionActivity.cpp:297-327`](../src/activities/network/WifiSelectionActivity.cpp#L297-L327)). Each saved-network connection attempt has a seven-second timeout ([`src/activities/network/WifiSelectionActivity.h:92-95`](../src/activities/network/WifiSelectionActivity.h#L92-L95), [`src/activities/network/WifiSelectionActivity.cpp:447-458`](../src/activities/network/WifiSelectionActivity.cpp#L447-L458)). If all automatic attempts fail, the activity falls back to the interactive network list rather than completing automatically ([`src/activities/network/WifiSelectionActivity.cpp:311-327`](../src/activities/network/WifiSelectionActivity.cpp#L311-L327)). That fallback is unsuitable for a sleep preflight.

The current sync path also performs an SNTP attempt before its API calls, waiting up to five seconds ([`src/activities/reader/KOReaderSyncActivity.cpp:41-64`](../src/activities/reader/KOReaderSyncActivity.cpp#L41-L64), [`src/activities/reader/KOReaderSyncActivity.cpp:123-141`](../src/activities/reader/KOReaderSyncActivity.cpp#L123-L141)). This delay should be audited for the automatic path: the general Wi-Fi connector already synchronizes the hardware clock only on the first successful connection ([`src/activities/network/WifiSelectionActivity.cpp:400-407`](../src/activities/network/WifiSelectionActivity.cpp#L400-L407)). The code does not establish that every progress request requires a fresh NTP synchronization, so removing or changing it should be verified against the supported servers before implementation.

### Smart Sync decision

Smart Sync is a behavior of the manual sync activity, not an automatic trigger. Its persisted values are `ASK_EVERY_TIME` and `SMART`, with Smart as the in-memory default ([`lib/KOReaderSync/KOReaderCredentialStore.h:14-18`](../lib/KOReaderSync/KOReaderCredentialStore.h#L14-L18), [`lib/KOReaderSync/KOReaderCredentialStore.h:27-35`](../lib/KOReaderSync/KOReaderCredentialStore.h#L27-L35)). Older files without the setting migrate from Ask and are resaved ([`lib/KOReaderSync/KOReaderCredentialStore.cpp:62-77`](../lib/KOReaderSync/KOReaderCredentialStore.cpp#L62-L77)).

Smart Sync first fetches the configured document ID. It also probes the alternate filename/content-hash ID and selects the furthest remote record found ([`src/activities/reader/KOReaderSyncActivity.cpp:144-192`](../src/activities/reader/KOReaderSyncActivity.cpp#L144-L192)). It then behaves as follows:

- no remote record: upload local progress;
- local and remote within 0.1 percentage points: treat them as already synchronized;
- local further ahead: upload local progress;
- remote further ahead: map and save the remote position locally.

The decision is implemented in [`src/activities/reader/KOReaderSyncActivity.cpp:194-200`](../src/activities/reader/KOReaderSyncActivity.cpp#L194-L200) and [`src/activities/reader/KOReaderSyncActivity.cpp:249-269`](../src/activities/reader/KOReaderSyncActivity.cpp#L249-L269). CrossPoint's server can return an exact rich position; plain KOSync data falls back to approximate XPath/percentage mapping ([`src/activities/reader/KOReaderSyncActivity.cpp:222-247`](../src/activities/reader/KOReaderSyncActivity.cpp#L222-L247), [`lib/KOReaderSync/ProgressMapper.h:54-83`](../lib/KOReaderSync/ProgressMapper.h#L54-L83)).

This "furthest wins" policy means an intentional reread at an earlier position will be moved forward to the remote position. That is already Smart Sync's behavior, but automatic triggering makes the trade-off less visible; the new setting must remain opt-in and explicitly depend on Smart Sync.

Each Smart Sync can make one or two GET requests and, when local is ahead or no record exists, one PUT ([`src/activities/reader/KOReaderSyncActivity.cpp:166-200`](../src/activities/reader/KOReaderSyncActivity.cpp#L166-L200), [`src/activities/reader/KOReaderSyncActivity.cpp:260-265`](../src/activities/reader/KOReaderSyncActivity.cpp#L260-L265), [`lib/KOReaderSync/KOReaderSyncClient.cpp:214-273`](../lib/KOReaderSync/KOReaderSyncClient.cpp#L214-L273)). It is therefore materially more expensive than merely saving the six-byte local progress file.

## Sleep lifecycle and integration constraints

Both inactivity timeout and a held power button call `enterDeepSleep()` directly ([`src/main.cpp:525-540`](../src/main.cpp#L525-L540)). That function currently:

1. records whether sleep originated in a reader and saves application state;
2. latches that deep sleep is committed;
3. immediately replaces the current activity with `SleepActivity`;
4. turns Wi-Fi fully off;
5. sleeps the IMU/display and enters hardware deep sleep.

The ordering is explicit in [`src/main.cpp:194-228`](../src/main.cpp#L194-L228). `goToSleep()` replaces the current activity and forces its sleep-screen render immediately because the caller sleeps directly afterward ([`src/activities/ActivityManager.cpp:211-218`](../src/activities/ActivityManager.cpp#L211-L218)). Consequently, a network operation cannot simply be inserted after `goToSleep()`.

The activity manager has no pause/resume or background-activity concept ([`src/activities/ActivityManager.h:22-35`](../src/activities/ActivityManager.h#L22-L35)). It owns one active activity plus a stack, and transitions destroy the replaced activity ([`src/activities/ActivityManager.cpp:128-169`](../src/activities/ActivityManager.cpp#L128-L169)). Automatic sleep sync therefore needs an explicit asynchronous **sleep-request/preflight/commit** state, rather than starting a task and continuing the existing synchronous sleep function.

The existing `deepSleepInProgress` latch is important: Wi-Fi activities normally request a silent restart during `onExit()`, but once sleep is committed the latch suppresses that restart ([`src/main.cpp:131-160`](../src/main.cpp#L131-L160), [`src/activities/reader/KOReaderSyncActivity.cpp:390-397`](../src/activities/reader/KOReaderSyncActivity.cpp#L390-L397)). An automatic flow must preserve that guarantee without setting the latch so early that a failed attempt leaves the device in a half-committed state.

Deep sleep currently wakes only for the power button; no timer wake is configured ([`lib/hal/HalPowerManager.cpp:80-91`](../lib/hal/HalPowerManager.cpp#L80-L91)). Periodic synchronization while the device is already asleep would require a new wake source and a full boot/network cycle. That is outside the useful scope of this feature and would directly trade standby life for sync frequency.

## RAM, fragmentation, and battery findings

The TLS client refuses a request below 35 KB total free heap or a 20 KB largest free block. Its comments report successful reader-launched sessions entering TLS with about 51.9–58.2 KB free and 42–53 KB maximum allocatable block ([`lib/KOReaderSync/KOReaderSyncClient.cpp:19-47`](../lib/KOReaderSync/KOReaderSyncClient.cpp#L19-L47), [`lib/KOReaderSync/KOReaderSyncClient.cpp:60-69`](../lib/KOReaderSync/KOReaderSyncClient.cpp#L60-L69)). This is the technical reason automatic sync must reuse the existing release-before-TLS sequence instead of keeping the active EPUB renderer resident.

Wi-Fi being active forces normal CPU frequency rather than the low-power frequency ([`lib/hal/HalPowerManager.cpp:25-34`](../lib/hal/HalPowerManager.cpp#L25-L34)). The build deliberately trades Wi-Fi throughput for roughly 25–30 KB more shared IRAM/DRAM heap because networking is intended for occasional sync/OTA use, not streaming ([`platformio.ini:88-94`](../platformio.ini#L88-L94)). Both facts favor infrequent, bounded sync sessions.

No additional heap allocation is needed for a page threshold itself. A small integral counter and flags can live in `EpubReaderActivity`; they should be updated only after the existing local progress save succeeds. Persisting that counter on every page would add SD writes without improving the on-sleep mode. The current reader already avoids redundant progress writes on menu/bookmark/screenshot rerenders by comparing the saved spine/page/page-count tuple ([`src/activities/reader/EpubReaderActivity.h:94-98`](../src/activities/reader/EpubReaderActivity.h#L94-L98), [`src/activities/reader/EpubReaderActivity.cpp:1677-1686`](../src/activities/reader/EpubReaderActivity.cpp#L1677-L1686)).

The reader already performs limited idle work, but it gates deferrable parsing on both total heap and largest-block floors and stops its look-ahead window to avoid monopolizing rendering/input ([`src/activities/reader/EpubReaderActivity.h:103-142`](../src/activities/reader/EpubReaderActivity.h#L103-L142)). A concurrent TLS task would work against those constraints; serialized activity lifetimes are the safer model.

## Recommended product shape

### Phase 1: sleep sync

Add these options inside the existing on-device **KOReader Sync** submenu:

- **Automatic sync:** `Off` / `When sleeping`
- Optional explanatory text in the web UI: requires Smart Sync and a saved Wi-Fi network; failures do not prevent sleep.

The submenu currently has eight fixed rows, including Sync Behavior, and persists changes immediately through `KOReaderCredentialStore` ([`src/activities/settings/KOReaderSettingsActivity.cpp:15-20`](../src/activities/settings/KOReaderSettingsActivity.cpp#L15-L20), [`src/activities/settings/KOReaderSettingsActivity.cpp:107-127`](../src/activities/settings/KOReaderSettingsActivity.cpp#L107-L127)). The web settings list exposes the same store through dynamic values ([`src/SettingsList.h:339-384`](../src/SettingsList.h#L339-L384)). The new setting belongs in `KOReaderCredentialStore`, not the general device settings, and should be saved only when its value changes.

Automatic mode should be effective only when all of these are true:

- sleep originated from `EpubReaderActivity`;
- Smart Sync is selected;
- KOReader credentials exist;
- at least one Wi-Fi credential is saved;
- the local position has changed since entry or since the last successful sync.

If Ask Every Time is selected, sleep should proceed without syncing; a sleep flow must not present an Apply/Upload prompt. Non-EPUB reader types should remain unchanged because the existing KOReader progress mapper and sync entry point are EPUB-specific ([`src/activities/reader/EpubReaderActivity.cpp:1161-1210`](../src/activities/reader/EpubReaderActivity.cpp#L1161-L1210), [`src/activities/reader/ReaderActivity.cpp:154-177`](../src/activities/reader/ReaderActivity.cpp#L154-L177)).

### Required control flow

Use a two-stage sleep operation:

1. **Request sleep.** Capture `fromTimeout` and whether the origin is the EPUB reader.
2. **Prepare local sync data.** Persist the current six-byte progress record, compute the KOReader/XPath position, capture the EPUB path/chapter name, then release `Section` and `Epub` exactly as manual sync does.
3. **Run a sleep-mode sync activity/coordinator.** It may render a translated "syncing before sleep" popup, but it must not expose Apply/Upload choices or return to the reader.
4. **Connect non-interactively.** Try the last saved SSID (optionally one bounded scan for another saved SSID). Never fall through to the network list. Apply a total deadline in addition to the per-network timeout.
5. **Run existing Smart Sync.** Preserve alternate-hash probing and rich-position handling. If remote is ahead, atomically update `progress.bin`; if local is ahead, upload.
6. **Commit sleep on every terminal result.** Set the deep-sleep latch only here, render the chosen sleep screen, turn Wi-Fi off, and enter deep sleep. Failure is logged but never cancels the user's sleep request.

The commit stage must preserve the original `lastSleepFromReader` value; by the time sync finishes, the foreground activity is a sync activity, so recomputing `activityManager.isReaderActivity()` would otherwise lose the origin. The existing sleep screen uses that flag to choose reader orientation/cover behavior ([`src/main.cpp:194-205`](../src/main.cpp#L194-L205), [`src/activities/boot_sleep/SleepActivity.cpp:20-55`](../src/activities/boot_sleep/SleepActivity.cpp#L20-L55)).

### Phase 2: page threshold, if still wanted

Prefer **Sync after _N_ pages, at next sleep** over immediate sync. Suggested values are `Off`, `10`, `25`, and `50`; the exact values should be validated on hardware rather than justified as battery-saving without measurements.

Count successful forward position changes in RAM, saturating the counter at the selected threshold. Reset only after a successful automatic sync. Backward turns, menu redraws, and failed local saves should not increment it. The page-turn function changes the position and requests a render ([`src/activities/reader/EpubReaderActivity.cpp:1265-1299`](../src/activities/reader/EpubReaderActivity.cpp#L1265-L1299)); the render path is where durable-save success is known ([`src/activities/reader/EpubReaderActivity.cpp:1677-1686`](../src/activities/reader/EpubReaderActivity.cpp#L1677-L1686)).

If product requirements demand immediate sync exactly at _N_ pages, present it honestly as a foreground interruption: current architecture must unload the book, connect Wi-Fi, sync, and restart/reopen the reader. It should be a separate explicit option, not the default.

## Failure and safety policy

- Local progress save failure: log with `LOG_ERR`, show the existing translated save failure only when the screen is still awake, and continue sleep using the last durable position.
- No saved network or connection timeout: skip sync and sleep.
- TLS heap gate failure: skip sync and sleep; do not retry in a loop. The client already returns `LOW_MEMORY` rather than entering a doomed request ([`lib/KOReaderSync/KOReaderSyncClient.cpp:60-69`](../lib/KOReaderSync/KOReaderSyncClient.cpp#L60-L69), [`lib/KOReaderSync/KOReaderSyncClient.cpp:276-295`](../lib/KOReaderSync/KOReaderSyncClient.cpp#L276-L295)).
- Authentication/server/JSON error: log once and sleep. Retain the due state only in RAM; deep sleep resets it, so the next sleep may make a fresh best-effort attempt.
- User-facing labels and messages must be added to the translation YAML and displayed through `tr()`; existing sync state messages already follow this pattern ([`src/activities/reader/KOReaderSyncActivity.cpp:125-139`](../src/activities/reader/KOReaderSyncActivity.cpp#L125-L139), [`lib/I18n/translations/english.yaml:122`](../lib/I18n/translations/english.yaml#L122)).

## Verification plan for a future implementation

### Build and static checks

1. Run `pio run` and `pio check`.
2. Confirm no raw SdFat access and no new bare `new`/`malloc` were added.
3. Confirm settings writes happen only when the automatic-sync setting changes, never per page.

### Serial and heap checks

With debug logging enabled, record `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()` at: sleep request, after EPUB/section release, after Wi-Fi connection, before each TLS call, after Wi-Fi teardown, and immediately before deep sleep. Compare against the existing 35 KB free/20 KB max-block gate ([`lib/KOReaderSync/KOReaderSyncClient.cpp:46-69`](../lib/KOReaderSync/KOReaderSyncClient.cpp#L46-L69)). Also confirm there is exactly one connection attempt sequence and no restart back into the reader before sleep.

### Device scenarios

Test both power-button sleep and inactivity sleep with:

- automatic sync off;
- Smart Sync with local ahead, remote ahead, equal progress, and no remote record;
- Ask Every Time selected;
- no KOReader credentials;
- no saved Wi-Fi credentials;
- saved SSID unavailable;
- wrong Wi-Fi password;
- server unavailable/authentication failure;
- heap immediately above and below the TLS gate;
- Quick Resume and each static sleep-screen mode;
- all four reader orientations;
- a large EPUB whose section is still incrementally building;
- a mid-footnote sleep (the reader currently preserves the pre-footnote origin during exit at [`src/activities/reader/EpubReaderActivity.cpp:230-253`](../src/activities/reader/EpubReaderActivity.cpp#L230-L253)).

Measure wall-clock delay from the sleep gesture to panel sleep and battery/current draw for a normal sleep versus successful sync, unavailable Wi-Fi, and server timeout. Those measurements—not an estimate—should determine the final deadline and any recommended page threshold.
