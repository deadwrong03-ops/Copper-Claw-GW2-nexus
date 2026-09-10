# Copper&Claw Development Log

## Project Overview

Copper&Claw is a standalone Guild Wars 2 Nexus Trading Post watcher.

The project began by extracting the already-working Trading Post framework that had been developed inside FoodReminder-Nexus into a new standalone addon. Copper&Claw is now maintained as its own separate project.

Display name: `Copper&Claw`  
Filesystem/data prefix: `CopperAndClaw`  
Latest published release: `v0.1.3`  
Development: `Active`

Current development builds may contain features or improvements that have not yet been included in the latest published release.

---


## 2026-09-10 — Copper&Claw Collector Integration

Integrated the Copper&Claw Nexus addon with the standalone Copper&Claw Collector.

### Collector-first current pricing

`TradingPostPriceManager` now attempts to retrieve watched-item prices from the local Collector first.

Local endpoint pattern:

```text
http://127.0.0.1:5177/item/{itemID}
```

If the Collector cannot be reached or cannot return usable data, Copper&Claw falls back to the official Guild Wars 2 API.

This preserves standalone addon behavior while allowing Collector-backed pricing when available.

**Result: PASS**

### Collector history import

`TradingPostHistoryManager` now supports importing existing historical observations from:

```text
/item/{itemID}/history
```

Current behavior:

- watched items request Collector history
- imported history is merged into existing Copper&Claw local history
- existing local observations are preserved
- exact duplicate observations are skipped
- imported observations are persisted into Copper&Claw's local TSV history
- newly watched items can immediately receive historical coverage collected before they were added to the Copper&Claw watch list

Fresh-item testing confirmed this behavior with previously unwatched Trading Post items.

Examples included:

- Heavy Supply Bag immediately receiving historical samples
- Dusk Dye importing Collector history before the first local-only history could accumulate

**Result: PASS**

### Import retry when Collector starts late

Initial integration attempted Collector history once during watched-item registration.

This exposed a usability issue:

- if Guild Wars 2 / Copper&Claw started before the Collector
- the first history request failed
- history would not be imported until the addon restarted

Retry support was added.

Current behavior:

- a failed history import is not treated as completed
- when a later price refresh successfully reaches the Collector, Copper&Claw retries the history import
- the import can recover automatically after the Collector starts
- Guild Wars 2 does not need to be restarted

Runtime validation confirmed `/item/{itemID}/history` requests appeared after the Collector was started later in the same Guild Wars 2 session.

**Result: PASS**

### Collector-aware refresh cadence

The previous automatic Trading Post refresh cadence was fixed at:

```text
60 seconds
```

This was wasteful while using the Collector because the Collector itself performs full market scans every five minutes.

Copper&Claw now tracks whether Collector-backed price retrieval is currently succeeding.

Automatic refresh behavior:

```text
Collector available: 300 seconds
Collector unavailable: 60 seconds
```

Manual Refresh and Refresh All remain immediate.

Runtime validation confirmed:

- Collector online → countdown near 300 seconds
- Collector stopped → next failed Collector request switches the countdown to 60 seconds
- fallback to the Guild Wars 2 API remains functional

**Result: PASS**

### End-to-end integration validation

Verified in-game:

- Collector current-price endpoint is consumed successfully
- Collector historical endpoint is consumed successfully
- existing local Copper&Claw history remains intact
- fresh watched items receive earlier Collector history
- Collector history and fresh Copper&Claw observations coexist
- Collector-unavailable startup no longer requires a game restart once the Collector later becomes available
- Collector-aware 5-minute refresh works
- 60-second API fallback works

This establishes the first working end-to-end Copper&Claw ↔ Collector integration checkpoint.

**Result: PASS**

---

## 2026-09-08 — v0.1.3 Release Preparation

Prepared Copper&Claw v0.1.3 for release after completing the post-v0.1.2 Trading Post UI and usability pass.

### Version metadata

Updated `entry.cpp`:

- Nexus addon version: `0.1.3.0`
- in-app footer: `Copper&Claw v0.1.3 - Development Build`
- standalone/non-Raidcore signature remains `(uint32_t)-26090301`

The signature comment was also corrected so it matches the existing project decision: keep the current standalone signature unless Copper&Claw later receives an assigned Raidcore signature.

### v0.1.3 feature set

This release includes the work completed after v0.1.2:

- independent Buy Targets and Buy Target alerts
- compact Sell / Buy target display with dedicated `Edit Targets`
- Normal / Slim / Ultra Slim watch-list density modes
- Slim as the default watch-list view
- Sell / Buy Analysis sparklines that follow the selected Trend Window
- standalone History UI removed while local history collection remains active
- optional per-item Favorites
- All Items / Favorites filtering
- persistent favorite state
- custom drawn favorite-star control
- cleaner two-row Trading Post toolbar
- label-first Trend Window / Sort / View / Show presentation

### Release validation status

The v0.1.3 source and documentation are prepared.

Final release validation still requires a clean Release x64 rebuild and in-game smoke test of the actual `0.1.3.0` build before the Git tag / GitHub release is created.

**Result: RELEASE PREPARED — FINAL SMOKE TEST PENDING**

---

## 2026-09-08 — Watch-List UI Density, Analysis Sparklines, Favorites & Polish

Completed a focused post-v0.1.2 Trading Post UI cleanup intended to reduce bulk without removing the underlying market-analysis functionality.

### Watch-list density modes

Added three selectable watch-list views:

- **Normal** — roomier rows with additional item metadata
- **Slim** — balanced compact layout and the default view
- **Ultra Slim** — compact Item / Sell / Buy / Actions presentation

The selected view density is persisted in `CopperAndClaw.ini`.

All density modes retain access to watched-item actions and target editing.

### Analysis sparklines

Sell and Buy sparklines were moved into the right side of their respective Analysis sections so they use previously empty horizontal space instead of adding more vertical UI.

Current presentation:

- Sell Listing sparkline uses green Sell-side styling
- Buy Order sparkline uses blue Buy-side styling
- no center baseline
- thicker, brighter lines for readability
- sparkline data follows the currently selected **Trend Window**
- graph dimensions remain consistent while the represented time period changes

This keeps the graph and the surrounding market analysis aligned to the same selected period.

### History UI removal

The standalone History tab/panel inside watched-item Details was removed.

Details now opens directly into Analysis.

This was a UI-only simplification:

- local Trading Post history collection remains active
- existing history remains stored
- history still powers trends, confidence, gap-aware coverage, volatility, historical statistics, and sparklines
- preserve-all history behavior is unchanged

### Watch-list Groups experiment

A grouped/folder-style watch-list experiment was implemented and tested.

The approach was rejected because moving items beneath separate stacked group headers made the list longer and introduced unnecessary organization work.

Decision:

- Groups were removed
- no group assignment workflow is required
- Copper&Claw keeps one continuous normal watch list

### Favorites

Groups were replaced with a simpler optional Favorites system.

Behavior:

- each watched item has a small star control
- newly watched items are not favorited by default
- **All Items** shows the normal complete watch list
- **Favorites** filters the same list to starred items only
- favorite state persists across restarts
- no folders, categories, or mandatory organization are required

The first star implementation used Unicode star characters, which were not supported correctly by the bundled font/source setup and rendered as `?`.

The star was replaced with direct ImGui draw-list geometry.

The initial filled-star implementation used a convex fill on a concave five-point star and looked visually uneven. It was corrected by filling the star with triangle fans from the center and retaining a clean outline.

### Toolbar cleanup

The growing toolbar was split into two rows to avoid crowding:

Row 1:

- Trend Window
- Sort

Row 2:

- View
- Show: All Items / Favorites

A final label polish changed the controls to natural label-first presentation:

- `Trend Window: [7d]`
- `Sort: [Name A-Z]`
- `View: [Normal]`
- `Show: All Items / Favorites`

The existing two-row structure was retained.

### In-game validation

Verified in-game:

- Normal / Slim / Ultra Slim views render correctly
- Slim remains the default
- target editing remains available
- Analysis sparklines render correctly
- sparklines follow the selected Trend Window
- removing the History UI does not remove the underlying analysis/history behavior
- Favorites filtering works
- drawn favorite stars render without Unicode/font issues
- filled favorite star renders cleanly
- two-row toolbar avoids crowding
- final label-first toolbar presentation reads cleanly in-game

**Result: PASS**

---

## 2026-09-07 — Buy Target Alerts & Target UI Cleanup

Added independent Buy Target support alongside the existing Sell Target system.

### Buy Target behavior

- each watched item can now store an independent Buy Target in addition to its Sell Target
- Buy Targets trigger when the current highest buy order is greater than or equal to the configured Buy Target
- Sell Targets continue to trigger when the current lowest sell listing is less than or equal to the configured Sell Target
- Sell and Buy targets use independent armed/reached state
- Sell and Buy alerts can both exist for the same watched item without interfering with each other
- Buy Targets re-arm only after a later observed highest buy order falls below the configured target
- Sell Targets retain their existing re-arm behavior
- Sell and Buy target hits share the existing queued alert system and are shown one at a time

### Persistence / compatibility

- watched items now persist both Sell Target and Buy Target values
- the watch-file format was extended without breaking older saved watch lists
- older watch rows containing only Sell Target data remain readable
- older alert-latch rows remain readable
- Buy Target values persist across Guild Wars 2 restarts
- reached/latched target state persists across restart
- the already-visible celebration banner itself does not persist across restart, preventing an old price-hit celebration from being resurrected after relaunch

### Alert presentation

- target alerts now identify whether the triggered side is Sell or Buy
- Sell alerts continue to show `CURRENT SELL`
- Buy alerts show `CURRENT BUY`
- both alert types show `YOUR TARGET`
- Buy prices use the existing blue Buy-side color
- Sell prices keep the existing green Sell-side color
- compact in-panel alerts identify the triggered side
- Buy Target hits use blue highlighting
- Sell Target hits use green highlighting
- whole watched-item row highlighting was removed
- only the specific triggered Sell Target or Buy Target block is highlighted, avoiding ambiguity

### Target editor cleanup

The initial Buy Target implementation made each watched-item row too busy because both Sell and Buy targets permanently displayed separate gold/silver/copper input controls.

The target UI was simplified so normal watch rows are primarily read-only:

- Sell Target shows its formatted target value and status
- Buy Target shows its formatted target value and status
- the six gold/silver/copper input boxes are hidden during normal viewing
- a dedicated `Edit Targets` button opens the editor only when needed
- Refresh and Remove remain available on the main row
- the Actions column was widened so all buttons remain fully visible

The Edit Targets popup was then polished:

- wider popup
- Sell Target and Buy Target use aligned rows
- gold / silver / copper inputs line up cleanly
- each target has its own `Clear` action
- `Done` is centered
- target editing remains independent for Sell and Buy

### In-game validation

Verified in-game:

- Buy Target controls appear and save correctly
- Buy Target triggers from a qualifying highest-buy observation
- Sell and Buy targets remain independent
- compact Buy Target alert identifies `Buy`
- Buy Target block highlights blue without highlighting the Sell Target block
- large gameplay alert displays `CURRENT BUY / YOUR TARGET`
- Sell Target alert behavior remains intact
- Buy Target value persists through restart
- latched target state persists through restart
- celebration banner does not persist through restart
- compact target display is substantially less cluttered
- Edit Targets popup opens correctly
- Refresh and Remove remain fully visible
- polished Edit Targets popup renders correctly

**Result: PASS**

---


## 2026-09-06 — v0.1.2 Release Validation

Prepared and validated the next Copper&Claw release build.

Version changes:

- Nexus addon version changed from `0.1.1` to `0.1.2`
- Nexus reports the running addon as `0.1.2.0`
- in-app footer changed to `Copper&Claw v0.1.2 - Development Build`

In-game smoke testing confirmed:

- the new version was recognized correctly by Nexus
- the one-time post-update notice appeared automatically after the real version change
- no manual reset of `acknowledgedUpdateNoticeVersion` was required
- the notice still acknowledges and persists normally
- watched-item Guild Wars 2 icons loaded automatically after a few seconds
- the Trading Post watch list remained functional after the update
- no blocking regression was observed during the v0.1.2 smoke test

**Result: PASS**

---

## 2026-09-06 — Version Metadata Sync

Updated the standalone addon version metadata in `entry.cpp` so the running addon matches the current published release.

Changes:

- Nexus addon version changed from `0.1.0` to `0.1.1`
- in-app footer changed from `Copper&Claw v0.1.0 - Development Build` to `Copper&Claw v0.1.1 - Development Build`
- no functional Trading Post, backup, history, or settings behavior was changed

The README and project overview already identified `v0.1.1` as the latest published release, so no README version change was required.

A clean rebuild completed successfully and the updated version display was verified in-game.

**Result: PASS**

---

## 2026-09-06 — Guild Wars 2 Item Icons

Added official Guild Wars 2 item icons to watched-item rows without changing the compact watch-list layout.

Implementation:

- `TradingPostIndexedItem` now stores the Guild Wars 2 item icon URL
- item metadata parsing now reads the API `icon` field
- the local item-index cache was expanded to store item ID, name, variant label, and icon URL
- older two-column and three-column caches remain readable
- legacy caches are treated as stale so Copper&Claw can rebuild them automatically and populate icon URLs
- watched items look up their indexed metadata by item ID
- Nexus texture loading is used to request the official `render.guildwars2.com` icon
- icons use a stable per-item texture identifier
- watched-item rows display a subtle 28x28 icon beside the item name
- no watch-list persistence format change was required

A compile issue caused by Windows `min` / `max` macros after including the Nexus API header was corrected by defining `NOMINMAX` before `nexus/Nexus.h`.

In-game validation confirmed icons render correctly beside watched items including Superior Sharpening Stone, Bowl of Sweet and Spicy Butternut Squash Soup, and Aurene's Bite.

README screenshot:

`screenshots/itemicon.jpg`

**Result: PASS**

---

## 2026-09-06 — One-Time Post-Update Item-Index Notice

Added a small one-time gameplay notice so players understand that some Copper&Claw updates may require the Trading Post item index to rebuild.

Behavior:

- shown after an addon version changes
- shown only during gameplay
- independent of the Copper&Claw options panel
- explains that some updates may rebuild the Trading Post item index
- explains that the rebuild starts automatically
- current wording states that the rebuild may take a couple minutes
- no progress bar is shown
- the player acknowledges the notice with `Got it`
- acknowledgement is persisted in `CopperAndClaw.ini`
- the acknowledgement key is derived from the running addon version
- the notice becomes eligible again automatically after a future version bump
- no actual rebuild state is required for the notice to appear

The first functional version used a normal ImGui title bar and duplicated the `Copper&Claw Updated` heading. It was redesigned into a compact Copper&Claw-style card with:

- no title-bar chrome
- dark background
- thin copper border and top accent
- copper/gold heading
- muted explanatory text
- styled `Got it` button
- compact placement near the upper center of the game screen

In-game testing confirmed the notice appears after resetting the acknowledged version value, saves acknowledgement when dismissed, and does not reappear for the same addon version afterward.

README screenshot:

`screenshots/updatenotification.jpg`

**Result: PASS**

---

## 2026-09-06 — Backup & Restore System

Added a dedicated Backup & Restore tab for protecting Copper&Claw's locally collected data without cluttering the Trading Post interface.

Current user-facing behavior:

- Automatic Backups can be enabled or disabled
- automatic backups are enabled by default
- default interval is 24 hours
- default retention is 10 backups
- available intervals are 6h, 12h, 24h, 3d, and 7d
- retention options are 5, 10, or 20 backups
- Last backup and Next backup are shown directly in the tab
- Back Up Now creates an immediate manual backup
- available backups are shown as a simple date/time list
- Restore and Delete remain visually inactive until a backup is selected
- clicking the selected backup again deselects it
- Restore and Delete both use confirmation dialogs
- Last restored shows the date/time of the backup most recently restored

Backup contents:

- `CopperAndClaw.ini`
- `CopperAndClaw_TradingPostWatch.ini`
- `CopperAndClaw_TradingPostHistory.tsv`

The item-search cache is intentionally excluded because it can rebuild automatically.

Restore safety behavior:

- restore is staged while Guild Wars 2 is running
- the selected backup is applied on the next Copper&Claw startup before Settings or Trading Post managers open the live data files
- automatic backups pause while a restore is waiting to be applied
- an internal safety snapshot is created before live files are replaced
- the safety snapshot is hidden from the normal backup list
- safety snapshots do not affect Last backup or the automatic-backup schedule
- a successful restore removes the temporary safety snapshot
- if replacement fails, Copper&Claw attempts to recover the previous live files from the safety snapshot

The first UI pass exposed implementation details such as `[Automatic]`, `[Safety]`, `Stage Restore`, and `Restore Pending`. These were intentionally removed from the normal interface so the workflow reads like standard backup software:

`select a date → Restore → restart Guild Wars 2`

Restore-point tracking was then added so the user can immediately see which backup was restored.

Example:

`Last restored: Sep 6, 2026 2:20 AM`

The success message also identifies the restored backup:

`Restored backup from Sep 6, 2026 2:20 AM.`

In-game validation confirmed:

- automatic first backup creation
- manual Back Up Now
- Last backup / Next backup scheduling
- Delete confirmation
- Restore confirmation
- restore staging
- restore application after Guild Wars 2 restart
- internal safety snapshot behavior
- internal safety snapshots remain hidden from the normal list
- normal backup list remains intact after restore
- Last restored persists across restart
- restored backup timestamp is displayed correctly
- backup-row select/deselect behavior
- Restore/Delete inactive appearance when no backup is selected
- clean rebuild after compatibility adjustment for the Nexus-bundled ImGui version

**Result: PASS**

## 2026-09-06 — Most / Least Volatile Watch-List Sorting

Extended the persistent watch-list sorting system with two additional options:

- Most Volatile
- Least Volatile

Behavior:

- volatility sorting uses Sell Listing price movement inside the selected Trend Window
- it uses the same volatility calculation as the Market Analysis badge
- an item must meet the volatility readiness requirements before it receives a sortable volatility value
- readiness requires at least 15 selected-window samples and at least 75% gap-aware selected-window coverage
- items still classified as `DEVELOPING` are placed below items with mature volatility data
- the sort remains display-only and does not rewrite the saved watch-list order
- the selected sort mode continues to persist in `CopperAndClaw.ini`

In-game testing confirmed both new sort options appear correctly in the dropdown.

Because the currently watched items are still `DEVELOPING`, final live ranking behavior will become directly observable once multiple items have mature volatility data.

**Result: UI / INTEGRATION PASS — LIVE RANKING VALIDATION PENDING MATURE DATA**

---

## 2026-09-06 — Colored Volatility Badge

Added a compact volatility classification to Market Analysis.

User-facing labels:

- `CALM`
- `NORMAL`
- `ACTIVE`
- `WILD`
- `DEVELOPING`

The UI intentionally does not display another volatility percentage or raw statistical value.

Current calculation:

- uses Sell Listing observations inside the selected Trend Window
- calculates relative price dispersion internally
- requires at least 15 selected-window samples
- requires at least 75% gap-aware selected-window coverage before assigning a mature volatility label
- otherwise displays `DEVELOPING`

Current internal classification thresholds:

- CALM: under 0.75%
- NORMAL: 0.75% to under 2.0%
- ACTIVE: 2.0% to under 5.0%
- WILD: 5.0% or higher

The badge is shown inside Market Analysis and uses a compact colored presentation rather than adding more number-heavy watch-row data.

In-game testing with a 7-day window and insufficient gap-aware coverage correctly displayed `DEVELOPING`.

**Result: PASS**

---

## 2026-09-06 — Watch-List Sorting

Added persistent watch-list display sorting without changing the saved watch-list order.

Current sort options:

- Added Order
- Name A-Z
- Closest to Target
- Biggest Price Drop
- Biggest Price Rise
- Most Volatile
- Least Volatile

Behavior:

- sorting is applied only to the local display copy returned to the UI
- the underlying persisted watch list remains in its original added order
- the selected sort mode is saved in `CopperAndClaw.ini`
- Closest to Target uses relative distance so differently priced items can be compared fairly
- items without a usable target/current price sort below items with usable target data
- Biggest Price Drop / Rise use Sell Listing trend percentage over the selected Trend Window
- items without usable trend history sort below items with usable trend data
- Most Volatile / Least Volatile use the same gap-aware volatility readiness rules as Market Analysis
- items still marked `DEVELOPING` sort below items with mature volatility data

In-game testing confirmed the watch list reorders correctly.

**Result: PASS**

---

## 2026-09-05 — Historical Data Recovery Validation

Previously compacted local history was reconstructed by merging:

- the fuller pre-compaction backup
- the current live Copper&Claw history file

Only exact duplicate rows were removed.

Recovery results:

- current live file before merge: 688 observations
- fuller backup: 1,181 observations
- exact overlap: 551 observations
- restored combined history: 1,318 unique observations
- recovered from backup: 630 observations

Aurene's Bite:

- live before merge: 410 observations
- backup: 656 observations
- restored combined history: 701 observations
- recovered Aurene's Bite observations: 291

After launching Copper&Claw with the restored file:

- Aurene's Bite displayed 702 observations after one fresh observation was recorded
- other watched-item counts also increased appropriately
- historical charts rendered normally
- gap-aware confidence remained active
- new observations continued appending

**Result: PASS**

---

## 2026-09-05 — Preserve-All Historical Data Policy

Review of `TradingPostHistoryManager.cpp` confirmed that older Copper&Claw builds destructively compacted historical observations:

- newest 24 hours: every observation
- 24 hours to 7 days: one representative observation per 5-minute bucket
- older than 7 days: one representative observation per 30-minute bucket

Compaction could occur:

- immediately on startup
- again periodically while recording observations

This caused retained observation counts to decrease as data aged.

Decision:

Copper&Claw should not silently discard market observations that the user has already collected.

Automatic destructive history compaction was disabled.

Current behavior:

- every successful Trading Post observation is appended normally
- startup no longer compacts/re-writes history
- ongoing observation recording no longer triggers destructive compaction
- existing history files are left intact
- new observations are preserved indefinitely unless the user manually removes or edits the data file

A compile issue from the first preserve-all edit was corrected by retaining the legacy compaction constant/helper code while disabling the actual invocation points.

**Result: BUILD PASS**

---

## 2026-09-05 — Gap-Aware Confidence / Coverage

The earlier confidence system could overstate coverage because it mainly compared total historical span against the selected trend window.

Example problem:

A history file could contain observations separated by long offline gaps but still appear to cover most or all of a 7-day window.

Added gap-aware coverage calculation.

Current behavior:

- counts samples inside the selected trend window
- measures observed continuity instead of only oldest-to-newest span
- discounts long periods with no observations
- accounts for the historical sampling tiers used by older Copper&Claw builds
- requires selected-window samples and coverage thresholds for analysis readiness
- HIGH confidence requires full readiness plus at least 90% gap-aware coverage

Coverage display formatting was also improved so partial-day and partial-hour values can be shown more clearly.

Example:

`1d 7h of 7d (19%)`

In-game Aurene's Bite test showed:

- `Conf LOW`
- approximately `1d 7h of 7d (19%)`
- selected-window sample count remained available
- Sell / Buy direction calculations continued working
- buyer / seller guidance correctly remained `Developing`

**Result: PASS**

---

## 2026-09-04 — Current Functional Validation

The following systems are currently working in-game:

- standalone Nexus loading
- `Copper&Claw` addon display name
- `Copper&Claw` in-app heading
- multi-item watch list
- 60-second automatic refresh
- manual item refresh
- Refresh All
- persistent watched items
- persistent Sell Targets
- persisted Trend Window
- cached searchable Trading Post item index
- official Guild Wars 2 item icons beside watched items
- icon URL caching and automatic legacy-cache refresh
- variant-aware duplicate-name item search
- player-friendly stat/variant labels
- searchable stat variants
- variant labels retained when adding watched items
- one-time post-update item-index notice
- per-version acknowledgement persistence for the update notice
- local Trading Post history
- preserve-all historical observation behavior
- history continuity across game restarts
- observation counts retained across game restarts
- current lowest sell price
- current highest buy price
- Sell Target editing
- target status
- target alert triggering
- queued target alerts
- Dismiss Party
- compact watch-list UI
- collapsible Details / History
- configurable trend windows
- separate Sell Listings analysis
- separate Buy Orders analysis
- direction percentages
- recent-average comparisons
- price-position analysis
- buyer-facing signals
- seller-facing signals
- confidence display
- gap-aware selected-window coverage display
- selected-window sample-count display
- spread display
- local-history disclaimer
- sell/buy history sparklines
- historical min/avg/max values
- dedicated Backup & Restore tab
- automatic rolling backups
- manual Back Up Now
- restore/delete confirmation flow
- staged restore on next startup
- internal pre-restore safety protection
- persistent Last restored restore-point tracking

**Current overall status: Core standalone functionality is working. Collector integration is now active in development builds, with Collector-first pricing, historical import, automatic retry, and dynamic refresh fallback validated in-game.**

Copper&Claw has since published releases through `v0.1.3`.

---


## 2026-09-05 — Aurene's Bite Changed to First-Run Starter

Removed the permanent-default behavior for Aurene's Bite.

New behavior:

- Aurene's Bite is seeded only when the watch settings file does not yet exist
- it is no longer marked as a permanent default item
- it can be removed normally
- removing it unregisters it from new history recording while preserving existing history
- after removal, restarting Guild Wars 2 does not automatically add it back
- it can be added again normally through Copper&Claw item search

In-game test:

- removed Aurene's Bite
- restarted/relogged
- verified it stayed removed
- added it back successfully

**Result: PASS**

---

## 2026-09-04 — Final Pre-Release Validation

Completed the final Copper&Claw pre-release verification pass using the current standalone build.

Verified:

- clean Release x64 rebuild
- fresh DLL load through Nexus
- standalone addon startup
- `Copper&Claw` display name and in-app heading
- multiple watched items
- automatic 60-second refresh
- manual item refresh
- Refresh All
- watched-item persistence
- Sell Target persistence
- Trend Window persistence
- cached Trading Post item index
- variant-aware duplicate-name item search
- player-friendly stat/variant labels
- local Trading Post history continuity
- separate Sell Listings / Buy Orders analysis
- direction percentages
- recent-average comparisons
- price-position analysis
- buyer-facing and seller-facing signals
- confidence / coverage / sample reporting
- sell/buy history sparklines
- historical min / avg / max values
- target alert triggering
- queued target alerts
- Dismiss Party behavior
- local-history disclaimer
- compact watch-list layout

No blocking in-game regression was observed during the current release-candidate testing.

**Result: PASS**

Copper&Claw is now at release-candidate status pending the final documentation commit/push and release/tag packaging.

---

## 2026-09-04 — Full Restart / Persistence Validation

Performed another full Guild Wars 2 restart using the current Copper&Claw build after the market-analysis changes.

Before restart, verified:

- multiple watched items
- Sell Targets configured
- Trend Window set to `6h`
- existing observation counts/history

After restart, verified:

- watched items remained
- Sell Targets remained
- Trend Window remained `6h`
- historical observations remained
- observation counts continued increasing
- local history remained intact
- current market analysis rendered normally

**Result: PASS**

---

## 2026-09-04 — Nexus Signature Decision

Reviewed the current development signature:

`(uint32_t)-26090301`

Decision: retain the current unique negative signature for standalone/non-Raidcore use.

If Copper&Claw is later hosted through Raidcore, replace it with the positive signature assigned by Raidcore.

The earlier documentation note saying the signature must always be replaced before public release is no longer considered accurate for a standalone release.

---

## 2026-09-04 — DLL Naming Decision

Reviewed whether the Visual Studio build target should be renamed from:

`Copper & Claw.dll`

to:

`CopperAndClaw.dll`

Decision: keep the current DLL filename.

Current naming:

- Visual Studio project: `Copper & Claw`
- DLL: `Copper & Claw.dll`
- user-facing addon name: `Copper&Claw`
- persistence/data prefix: `CopperAndClaw`

No build-target rename is currently planned.

---

## 2026-09-04 — In-App Heading Cleanup

The main Trading Post panel still displayed the inherited heading:

`Trading Post Watcher`

This was a leftover from the original FoodReminder-Nexus Trading Post framework.

Changed the visible heading to:

`Copper&Claw`

No functional logic was changed.

**Result: PASS**

---

## 2026-09-04 — Historical Data Disclaimer Added

Copper&Claw does not have external historical Trading Post data from before an item begins being tracked.

Added a global UI notice near the Trend Window:

`Historical note: Copper&Claw builds its own price history from the time you begin tracking an item.`

`No earlier market history is available on initial install.`

Developing analysis can also display:

`Analysis is based only on locally collected history.`

This clarifies that longer trend windows become meaningful only after enough local observations have been collected.

**Result: PASS**

---

## 2026-09-04 — Confidence / Coverage Presentation

Market analysis now presents maturity information so newly tracked items are not treated as if they have complete long-term history.

Current analysis considers:

- number of observations
- available history span
- selected trend window
- whether the selected window comparison is available
- selected-window coverage

Current minimum analysis sample floor:

`15`

Current readiness threshold:

`75%` selected-window coverage

Current high-confidence threshold:

`90%` selected-window coverage

Confidence labels:

- LOW
- MEDIUM
- HIGH

The UI also shows:

- Coverage
- Samples

This is intended to make the limitations of locally collected data visible instead of presenting early analysis with false certainty.

**Result: PASS**

---

## 2026-09-04 — Direction Percentages Added

Added percentage change next to Sell Listing and Buy Order direction labels.

Examples:

`Getting More Expensive (+1.19%)`

`Buyers Offering More (+0.38%)`

The compact watched-item summary now also includes the direction percentages.

Example:

`Listings: More Expensive +1.19% | Buy Orders: Higher +0.38% | Conf HIGH`

Recent-average comparison percentages remain visible separately.

Decision: do not add additional percentages to signals or confidence so the interface remains readable.

**Result: PASS**

---

## 2026-09-04 — Separate Sell Listings / Buy Orders Analysis

The old combined market terminology could look contradictory because Sell Listings and Buy Orders mean different things.

Example:

- Sell listing rising = buyers must pay more
- Buy order rising = sellers can receive more

Decision: analyze both sides separately.

New sections:

### SELL LISTINGS

Displays:

- Direction
- Recent Avg
- Price Position
- For Buyers

Possible direction wording includes:

- Getting More Expensive
- Getting Cheaper
- Stable
- Developing

Buyer-facing signals include:

- Favorable
- Worth Watching
- Typical
- Caution
- Developing

### BUY ORDERS

Displays:

- Direction
- Recent Avg
- Price Position
- For Sellers

Possible direction wording includes:

- Buyers Offering More
- Buyers Offering Less
- Stable
- Developing

Seller-facing signals include:

- Favorable
- Worth Watching
- Typical
- Weak
- Developing

Shared information includes:

- Confidence
- Coverage
- Samples
- Spread

This removed the confusing appearance of Sell and Buy information contradicting each other.

**Result: PASS**

---

## 2026-09-04 — Market Analysis Accuracy Review

Reviewed the original Deal / Signal logic after live in-game examples showed misleading combinations.

A key issue was identified:

A rising sell trend could cause the old system to label an item overpriced or cautionary even when the current price was still below its recent average.

Decision:

Price position should carry more weight than trend direction.

A rising price alone should not automatically mean an item is overpriced.

Example logic direction:

- below average + falling/stable → favorable
- below average + rising → worth watching
- near average + rising → watch
- above average + rising → caution
- well above average + rising → stronger caution

This became the basis for the next market-analysis redesign.

---

## 2026-09-03 — Git Repository / Documentation Setup

Initialized a Git repository for Copper&Claw and pushed the standalone project to GitHub.

The repository root contains the Visual Studio solution, project folder, README, and development log.

Added:

- `README.md`
- `DEVELOPMENT_LOG.md`

The documentation files were initially referenced by Visual Studio from outside the repository and therefore did not appear on GitHub.

They were moved into the repository root, re-added to the Visual Studio project, committed, and pushed successfully.

**Result: PASS**

---

## 2026-09-03 — Player-Friendly Variant Labels

The first variant-aware implementation exposed raw Guild Wars 2 API attribute names.

Examples included:

- `AgonyResistance / Power`
- `AgonyResistance / BoonDuration`
- `AgonyResistance / ConditionDuration`

This worked technically but made autocomplete results longer and less readable than necessary.

Added player-facing attribute formatting.

Current mappings include:

- `Power` → `Power`
- `Precision` → `Precision`
- `Toughness` → `Toughness`
- `Vitality` → `Vitality`
- `Ferocity` → `Ferocity`
- `ConditionDamage` → `Condition Damage`
- `Healing` → `Healing Power`
- `BoonDuration` → `Concentration`
- `ConditionDuration` → `Expertise`
- `AgonyResistance` → omitted from the visible variant label

The item-index cache was manually removed once during testing so the new labels could be rebuilt immediately.

After the rebuild, Festive Confetti Infusion autocomplete results displayed cleanly as:

- `Festive Confetti Infusion - Power`
- `Festive Confetti Infusion - Vitality`
- `Festive Confetti Infusion - Expertise`
- `Festive Confetti Infusion - Precision`
- `Festive Confetti Infusion - Toughness`
- `Festive Confetti Infusion - Concentration`
- `Festive Confetti Infusion - Healing Power`
- `Festive Confetti Infusion - Condition Damage`

The results are now short enough to scan easily while still identifying the exact Trading Post listing being watched.

**Result: PASS**

---

## 2026-09-03 — Variant-Aware Trading Post Search

Extended the Trading Post item index so searchable items can carry an optional stat/variant label in addition to:

- item ID
- item name

The item index now reads stat attribute metadata from the Guild Wars 2 item API and uses it to distinguish duplicate-name Trading Post items.

The local cache format was expanded to store:

- item ID
- item name
- variant label

Search text also includes the variant label, allowing searches to identify a specific version more easily.

Example concept:

`Festive Confetti Infusion - Power`

instead of:

`Festive Confetti Infusion (Item ID: 84882)`

When a variant-aware search result is added to the watch list, the variant label is retained in the watched-item name.

Legacy item-index cache rows remain readable, but an older cache without variant information triggers a background rebuild so the new metadata can be populated.

**Result: PASS**

---

## 2026-09-03 — Duplicate-Name Trading Post Item Discovery

Testing uncovered an important Trading Post search issue with items that share the same display name but use different item IDs and have separate Trading Post listings.

Example tested:

`Festive Confetti Infusion`

Searching for this item returned several entries with the same visible name but different item IDs.

Because each variant has its own Trading Post listing and price, choosing an entry by item ID alone made it difficult to know which stat variant was actually being watched.

This could cause the Copper&Claw price to appear incorrect when compared against a different stat variant opened in the in-game Trading Post.

The Trading Post price lookup itself was correct; the problem was item identification in the search/autocomplete interface.

---

## 2026-09-03 — Target Alert UI Redesign

The original celebratory target overlay worked but was visually cramped.

Redesigned the alert into a taller card-style layout with clearer hierarchy:

1. TARGET REACHED!
2. item name
3. CURRENT SELL
4. YOUR TARGET
5. Dismiss Party

Removed the redundant explanatory sentence indicating that the current sell was at or below the target.

Preserved the Dragon Bash-style celebration:

- animated confetti
- animated burst effects
- pulsing celebratory border
- Dismiss Party button

The current dark card background may receive additional cosmetic polish later.

**Result: PASS**

---

## 2026-09-03 — Target Alert Testing

Sell Target functionality tested in-game.

Verified:

- target value can be entered directly from the compact item row
- a qualifying fresh Trading Post observation triggers the alert
- active target state is shown in the watch list
- Dismiss Party clears the active alert
- multiple triggered target alerts are queued
- dismissing one alert advances to the next queued alert

**Result: PASS**

---

## 2026-09-03 — Persistence Testing

Performed a full game restart with watched items already configured.

Verified:

- watched items persisted
- Trading Post item index loaded from cache without rebuilding
- existing history remained available
- new observations continued to accumulate after restart

**Result: PASS**

---

## 2026-09-03 — Sell Target Visibility Fix

After the compact redesign, the Sell Target controls were still functional but had been moved inside the collapsed Details / History section.

Changed the compact main item row to keep the Sell Target gold/silver/copper controls directly visible. The duplicate target editor inside Details / History was removed.

The same target-row change was also prepared for FoodReminder-Nexus.

**Result: PASS**

---

## 2026-09-03 — Compact Watch-List UI

Redesigned each watched item into a compact presentation.

Changes:

- item name made more prominent
- current sell price kept visible
- current buy price kept visible
- status kept visible
- Refresh / Remove actions kept visible
- Details / History collapsed by default
- trend data moved inside Details / History
- market information moved inside Details / History
- Spread information moved inside Details / History
- signals moved inside Details / History
- charts and historical statistics moved inside Details / History

The same compact layout was also ported back into FoodReminder-Nexus.

**Result: PASS**

---

## 2026-09-03 — Multiple Watch Items Tested

Added multiple items to the watch list.

Functionality worked, but the original Trading Post layout became visually cluttered when several watched items were displayed.

Decision: redesign the main watch-list display into a compact item layout.

---

## 2026-09-03 — First In-Game Standalone Test

Copper&Claw loaded successfully through Nexus.

Verified:

- addon recognized by Nexus
- Copper&Claw options panel opens
- Trading Post UI renders
- Aurene's Bite loads
- sell price populates
- buy price populates
- 60-second automatic watch cycle runs
- Trading Post item index builds successfully
- trend/history system is active

**Result: PASS**

---

## 2026-09-03 — Standalone Nexus Entry Point

Created a standalone `entry.cpp`.

Current addon metadata:

- Name: `Copper&Claw`
- Version: `0.1.0`
- Author: `spectre9510`
- Description: Trading Post watch/history/trends/alerts

Startup initializes Settings, Price Manager, History Manager, Item Index Manager, and Watch Manager.

Nexus render and options-render callbacks are registered. Shutdown saves settings and shuts down the Trading Post systems.

Current standalone signature:

`(uint32_t)-26090301`

Decision: keep the current unique negative signature for standalone/non-Raidcore use.

If Copper&Claw is later hosted through Raidcore, replace it with the signature assigned by Raidcore.

---

## 2026-09-03 — Trading Post Framework Extracted

Copied the working Trading Post framework from FoodReminder-Nexus into Copper&Claw.

Core files include:

- `entry.cpp`
- `Settings.h`
- `Settings.cpp`
- `TradingPostUI.h`
- `TradingPostUI.cpp`
- `TradingPostWatchManager.h`
- `TradingPostWatchManager.cpp`
- `TradingPostPriceManager.h`
- `TradingPostPriceManager.cpp`
- `TradingPostHistoryManager.h`
- `TradingPostHistoryManager.cpp`
- `TradingPostItemIndexManager.h`
- `TradingPostItemIndexManager.cpp`

Standalone persistence names were changed to:

- `CopperAndClaw.ini`
- `CopperAndClaw_TradingPostWatch.ini`
- `CopperAndClaw_TradingPostHistory.tsv`
- `CopperAndClaw_TradingPostItemIndex.tsv`

HTTP user-agent changed to `CopperAndClaw/1.0`.

---

## 2026-09-03 — Standalone Project Created

Created a new Visual Studio DLL project for Copper&Claw.

Initial project name: `Copper & Claw`

Configured:

- x64
- Dynamic Library
- Platform Toolset v143
- C++17
- Multi-Byte Character Set
- `/MP`
- no precompiled headers
- Debug `/MTd`
- Release `/MT`
- Windows subsystem
- UAC disabled

Initial build issues were caused by stale ImGui source references pointing outside the standalone project. The ImGui source references were removed and re-added from the correct project-local `imgui` folder.

**Result: Release x64 build passed.**

Current generated DLL: `Copper & Claw.dll`

---

## Current Known Notes / Decisions Still Open

### Collector integration

The first local Collector integration is working and validated.

Current local service address:

```text
127.0.0.1:5177
```

Current behavior:

- Collector-first current pricing
- Guild Wars 2 API fallback
- Collector history import
- automatic retry if Collector starts after Guild Wars 2
- 5-minute refresh while Collector is available
- 60-second refresh while falling back

Future work may include cleaner in-game Collector connection/status presentation and tighter synchronization to Collector snapshot timing.


### Aurene's Bite starter item

Aurene's Bite is now used only as a first-run starter watched item.

Item ID: `96356`

Behavior:

- added automatically only when no Copper&Claw watch settings file exists
- fully removable like any other watched item
- stays removed after restart
- can be added back normally through item search

**Decision: RESOLVED**

### DLL / project naming

Current Visual Studio project: `Copper & Claw`  
Current DLL: `Copper & Claw.dll`  
User-facing addon display name: `Copper&Claw`

Decision: keep the current DLL/project naming.

### Target alert appearance

The redesigned target alert is functionally good and clearer to read.

The current dark card background may receive additional cosmetic polish later.

### Item-index cache format

The Trading Post item-index cache now includes:

- item ID
- item name
- variant label
- Guild Wars 2 item icon URL

Older two-column and three-column cache rows remain readable, but legacy caches trigger a background rebuild so the current metadata, including icon URLs, can be populated.

### Local history / Collector history

Copper&Claw preserves its own local price-history database.

When the standalone Copper&Claw Collector is available, watched items can also import historical market observations that were collected before the item was added to the Copper&Claw watch list.

Current behavior:

- existing local history is preserved
- Collector history is merged into local history
- exact duplicate observations are skipped
- imported history is persisted locally
- if the Collector is unavailable, Copper&Claw continues using its own local history and the Guild Wars 2 API

Copper&Claw does not automatically compact or downsample older local history.

### Market-analysis confidence

Confidence now uses gap-aware selected-window coverage instead of relying only on oldest-to-newest history span.

Long offline periods without observations are discounted so they cannot make coverage appear more complete than the actual observation density.

Selected-window sample count is also used for readiness.

**Decision: RESOLVED**

### Nexus signature

Current standalone signature:

`(uint32_t)-26090301`

Decision: retain for standalone/non-Raidcore use.

Replace only if Copper&Claw later receives an assigned Raidcore signature.

---

## v0.1.3 Pre-Release Checklist

- [x] bump `entry.cpp` version to `0.1.3.0`
- [x] update in-app footer to `Copper&Claw v0.1.3 - Development Build`
- [x] update `README.md` for v0.1.3
- [x] update `DEVELOPMENT_LOG.md` for v0.1.3
- [ ] perform clean **Release x64** rebuild
- [ ] verify final DLL loads through Nexus
- [ ] verify Nexus reports `0.1.3.0`
- [ ] verify the v0.1.3 one-time update notice appears after the version change
- [ ] verify watched-item icons repopulate normally
- [ ] verify All Items / Favorites filtering
- [ ] verify favorite state survives a Guild Wars 2 restart
- [ ] verify Normal / Slim / Ultra Slim views
- [ ] verify Trend Window / Sort / View / Show toolbar presentation
- [ ] verify Sell / Buy sparklines follow the selected Trend Window
- [ ] verify Sell Target behavior
- [ ] verify Buy Target behavior
- [ ] verify local history continues appending
- [ ] verify Backup & Restore tab still opens normally
- [ ] confirm no blocking regression during smoke test
- [ ] commit release-prep changes
- [ ] push
- [ ] create Git tag `v0.1.3`
- [ ] create GitHub release `v0.1.3`

---

## Development Rule

When making future Copper&Claw changes, use the current project files as the source of truth.

Do not substitute older copies of files from prior work.

When a current source file is needed for modification, request the current file first, even if that same file was edited or supplied earlier in the current development session.
