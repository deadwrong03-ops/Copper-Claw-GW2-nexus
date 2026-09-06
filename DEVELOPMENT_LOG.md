# Copper&Claw Development Log

## Project Overview

Copper&Claw is a standalone Guild Wars 2 Nexus Trading Post watcher.

The project began by extracting the already-working Trading Post framework that had been developed inside FoodReminder-Nexus into a new standalone addon. Copper&Claw is now maintained as its own separate project.

Display name: `Copper&Claw`  
Filesystem/data prefix: `CopperAndClaw`  
Latest published release: `v0.1.2`  
Development: `Active`

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

**Current overall status: Core standalone functionality is working and the current build is a release candidate.**

Copper&Claw has since published releases through `v0.1.2`.

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

### Local history limitation

Copper&Claw builds its own local price-history database.

No historical market data exists for periods before an item began being tracked locally.

New installations therefore begin with no historical market data.

Copper&Claw now preserves every successfully recorded observation and does not automatically compact or downsample older history.

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

## Pre-Release Checklist

- [x] make Aurene's Bite a removable first-run starter item
- [x] perform clean Release x64 rebuild
- [x] verify final DLL loads through Nexus
- [x] verify Nexus reports `0.1.2.0`
- [x] verify v0.1.2 update notice triggers automatically after version change
- [x] verify watched-item icons still load after the v0.1.2 update
- [x] verify watch persistence
- [x] verify Sell Target persistence
- [x] verify Trend Window persistence
- [x] verify cached item index
- [x] verify watched-item Guild Wars 2 icons
- [x] verify legacy item-index cache refresh for icon metadata
- [x] verify one-time post-update item-index notice
- [x] verify update-notice acknowledgement persistence
- [x] verify variant-aware duplicate-name search after restart
- [x] verify history continuity
- [x] verify preserve-all history behavior
- [x] verify restored history continues appending
- [x] verify observation counts survive restart
- [x] verify Sell Listings analysis
- [x] verify Buy Orders analysis
- [x] verify confidence / gap-aware coverage display
- [x] verify local-history disclaimer
- [x] verify target alert
- [x] verify queued alerts
- [x] update `README.md`
- [x] update `DEVELOPMENT_LOG.md`
- [ ] commit
- [ ] push
- [ ] create release/tag when ready

---

## Development Rule

When making future Copper&Claw changes, use the current project files as the source of truth.

Do not substitute older copies of files from prior work.

When a current source file is needed for modification, request the current file first, even if that same file was edited or supplied earlier in the current development session.
