# Copper&Claw
 <img width="75" height="75" alt="logo" src="https://github.com/user-attachments/assets/855fe874-597b-4c22-84a1-2bb9ccd30118" />

Copper&Claw is a standalone **Guild Wars 2 Nexus addon** for tracking Trading Post prices, building local price history, analyzing market movement, and alerting you when watched items reach a target price.

<img width="500" height="500" alt="Screenshot 2026-10-24 233927" src="https://github.com/user-attachments/assets/c3ede746-5c91-4c3e-b190-3588aafbc690" />


## Status

**Latest Published Release:** v0.2.0
**Development:** Active

> **Development Note:** Copper&Claw is actively developed. Some recently added features or improvements may be available in the source or development build before they are included in the latest published release.

---

## What's New in v0.2.0

v0.2.0 is the largest Copper&Claw update since v0.1.4, combining the major market-analysis, Watchlist, item-index, and UI work completed during the post-v0.1.4 development cycle.

Highlights include:

- persistent **Watchlist Groups** alongside the automatic item-category filters
- removal of the redundant top-level **All Items / Favorites** filter while retaining per-item Favorite marking
- **Prowl Radar** with Live, New 1h, Following, and Resolved 24h views
- paginated Collector signal feeds so Radar is no longer intentionally limited to the first 500 rows
- persistent per-item **My Bid** references with **AT TOP / OUTBID / ABOVE TOP / NO MARKET DATA** status
- adaptive **Suggested Bid** reference pricing
- **6 Months / 1 Year** Trend Windows with Collector-first hybrid history support
- compact **Market Depth** order-book diagnostics
- expanded local item-index metadata for icons, hover details, automatic categories, and richer item presentation
- refreshed Guild Wars 2 / Living World themes plus the Cyberpunk 2077 alternate theme
- substantial Item Analysis polish, including the compact **Top Buy / Lowest Sell / selected-window Avg** summary and cleaner analysis/snapshot presentation

The v0.2.0 Release x64 build was validated in-game with the new version identifiers before packaging.

---

## Main Features

- Watch multiple Trading Post items
- Official Guild Wars 2 item icons beside watched items
- Hover item icons for brief indexed item details and stats without making a new network request on hover
- Automatic **Watchlist category filters**: All, Weapons, Armor, Dyes, Consumables, Materials, Upgrades, Trinkets, and Other
- Persistent **Watchlist Groups** for manual organization alongside the automatic category filters
- Persistent per-item **Favorites**
- **List / Heatmap** Watchlist presentation with compact market-state tiles
- Persistent **Prowl Follow List** with 24-hour, 48-hour, or until-removed follow durations
- Collapsible Watchlist with a compact icon rail when collapsed
- Collector-aware automatic refresh: 5 minutes when the hosted Collector is available, 60 seconds when falling back to the Guild Wars 2 API
- Manual per-item refresh and Refresh All with a shared 10-second manual-refresh cooldown
- Current **lowest sell listing**
- Current **highest buy order**
- Per-item **Sell Targets**
- Per-item **Buy Targets**
- Independent Sell and Buy target alerts
- Compact target display with dedicated **Edit Targets** controls
- Persistent watch list, Favorites, targets, **My Bid** references, settings, and price history
- Optional integration with the standalone Copper&Claw Collector for broader historical data
- Hosted Collector connection over HTTPS by default
- Configurable Collector host, port, and HTTP/HTTPS connection settings
- Automatic migration from legacy `localhost:5177` and `127.0.0.1:5177` HTTP defaults
- Bounded Collector history and completed-snapshot requests instead of unbounded lifetime downloads
- On-demand long-window history for **6 Months / 1 Year**, with Collector-first coverage and cached GW2TP gap filling when older coverage is missing
- Compact Collector status line showing connection state, active price source, analysis source, last successful contact, and last completed scan age
- Search items without requiring manual item IDs
- Variant-aware search for duplicate-name items
- Separate **Sell Listings** and **Buy Orders** market analysis
- Adaptive **Suggested Bid** reference pricing that matures from recent 7-day data into 30-day and 3-month context as history grows
- Persistent per-item **My Bid** reference for remembering the price you personally chose without tying it to Suggested Bid
- Time-normalized **Avg** and local **Roll Avg** chart references
- Compact **Market Depth** order-book diagnostics for Sell Listings and Buy Orders
- **Prowl Radar** live scanner for unusual whole-market price/depth behavior
- Radar modes for **Live**, **New 1h**, **Following**, and **Resolved 24h**
- Historical Market Prowl markers on item charts
- Collector snapshot-aware confidence and coverage, with gap-aware local-history fallback
- Colored **Market Volatility** classification based on scan-to-scan movement
- Sortable watch list
- Persistent visual themes for expansions, Living World eras, and the Cyberpunk 2077 alternate style
- Sell / Buy price-history charts that follow the selected **Trend Window**
- Trend Windows from **15 Minutes** through **1 Year**
- Historical minimum / average / maximum prices
- Dragon Bash-style **Target Reached** alerts
- Built-in automatic **Backup & Restore** for local Copper&Claw data
- One-time post-update notice when an item-index rebuild may be required
- Compact game-screen item-index build progress overlay that remains visible without opening Copper&Claw

---

## Item Icons

Watched items display their official Guild Wars 2 item icon directly beside the item name.

<img width="365" height="335" alt="itemicon" src="https://github.com/user-attachments/assets/6d127eea-fdb2-41c0-93a4-76f4bb6bab87" />

Icons are loaded from the Guild Wars 2 item data used by Copper&Claw's searchable Trading Post index.

---

## Watch List Sorting

Watched items can be reordered without changing their saved watch-list order.

Current sort options:

- Added Order
- Name A-Z
- Closest to Target
- Biggest Price Drop
- Biggest Price Rise
- Most Volatile
- Least Volatile

Price-rise, price-drop, and volatility sorting use the selected **Trend Window**.

**Closest to Target** evaluates both Sell Targets and Buy Targets and uses the closest proportional distance for each item.

Volatility sorting uses the same readiness rules as the **Market Volatility** badge. When Collector-backed history is available, the sample requirement scales with the selected Trend Window instead of always requiring 15 samples. Items still marked **DEVELOPING** are placed below items with mature volatility data.

---

## Watchlist Heatmap, Categories, Groups & Favorites

Copper&Claw's Watchlist uses the current compact card layout and also includes a **Heatmap** presentation for quickly scanning watched items.

Heatmap tiles show the item icon, current Sell price, selected-window percentage movement, volatility, current Prowl state when present, and Follow / Favorite indicators. Strong market states use the tile background/border for faster visual scanning, and active Prowl tiles receive a subtle pulse.

Heatmap interaction remains fully functional:

- left-click a tile to open that item in **Item Analysis**
- right-click a tile for Favorite, Follow, Move to Group, Refresh Price, and Remove actions

The Heatmap preference persists across Guild Wars 2 restarts.

The Watchlist can also be collapsed into a narrow icon rail. Watched-item icons remain selectable from the rail so Item Analysis can still be switched without reopening the full list.

The Watchlist uses two complementary organization layers:

- compact **automatic category filters** derived from Guild Wars 2 item metadata
- persistent **Watchlist Groups** for manual organization

Current category buttons:

- **ALL**
- **WEAP** — Weapons
- **ARMOR** — Armor and back items
- **DYES** — Dye unlocks
- **CONS** — Consumables
- **MATS** — Crafting materials and trophies
- **UPGR** — Upgrade components
- **TRINK** — Trinkets
- **OTHER** — Items that do not fit the categories above

Categories are display filters only. Group assignments are a separate persistent organization layer and survive Guild Wars 2 restarts.

The category filter is intentionally session-only and starts on **ALL** each launch. When a newly watched item is added while another category is selected, Copper&Claw switches to the new item's category so the item does not appear to vanish.

The compact category strip stays inside the Watchlist pane and does not add another tall section above Item Analysis or push the **Trend Window** farther down the page.

Items can still be marked as **Favorites** using the existing per-item star. The old top-level **All Items / Favorites** filter has been removed from the Watchlist toolbar because Watchlist Groups now provide the primary manual organization workflow; Favorite state remains available as a lightweight per-item marker.

### Prowl Follow List

Any item surfaced by Prowl can be followed without permanently adding it to the Watchlist.

Available follow durations are:

- **24 hours**
- **48 hours**
- **Until removed**

Followed items appear in the Prowl Radar **Following** view and can also display a Follow indicator on Watchlist Heatmap tiles. Timed follows expire automatically; permanent follows remain until manually removed.

Follow state is addon-side and persisted in `CopperAndClaw.ini`.

---

## Visual Themes

Copper&Claw includes a persistent Trading Post theme selector. Themes change the dashboard's visual identity while preserving the same layout, spacing, analysis behavior, and one-live-analysis performance model.

The selector follows Guild Wars 2 story chronology, with **Cyberpunk 2077** kept at the bottom as a bonus alternate style.

Available themes:

- **Guild Wars 2 Core** — crimson red / ink black / white
- **Scarlet's War (LWS1)** — Scarlet red / toxic lime / molten orange
- **Glint's Prophecy (LWS2)** — amber gold / sandstone brown / dry gold
- **Heart of Thorns** — jungle green / bright yellow / Auric gold
- **Glint's Legacy (LWS3)** — Bloodstone coral / ash grey / Bloodstone glow
- **Path of Fire** — Elonian purple / warm orange / desert gold
- **Rise of Kralkatorrik (LWS4)** — Brand fuchsia / crystal purple / branded cyan
- **The Icebrood Saga** — icy blue / blood red / frost white
- **End of Dragons** — jade teal / seafoam / cyan
- **Secrets of the Obscure** — astral gold / shadow purple / amber
- **Janthir Wilds** — forest moss green / deep blue / ochre
- **Visions of Eternity** — nebula pink / violet / ethereal blue
- **Cyberpunk 2077** — cyan-forward controls with acid yellow identity accents and red danger accents

The theme refresh uses the same visual identity across major panels, compact controls, category buttons, popups, hover/active states, and **My Bid** controls instead of allowing small controls to fall back to unrelated default ImGui colors.

The selected theme persists across Guild Wars 2 restarts. Existing saved numeric theme IDs remain compatible even though the selector presentation order changed.

Trading Post market-side colors remain semantic across every theme:

- **Sell** stays green
- **Buy** stays blue

This keeps Sell / Buy information immediately recognizable even when the surrounding dashboard palette changes.
---

## Market Analysis

Copper&Claw separates the two sides of the Trading Post market while keeping supporting context in the same Item Analysis view.

### Sell Listings

- Direction
- % movement
- selected-window average
- price position
- **Buyer View**

**Buyer View** summarizes the current lowest sell listing relative to the selected Trend Window average:

- **Favorable** — at least 1% below average
- **Typical** — within 1% of average
- **Caution** — at least 1% above average
- **Developing** — insufficient reliable selected-window data

Buyer View is descriptive context, not a prediction of where the price will move next.

### Buy Orders

- Direction
- % movement
- selected-window average
- price position
- **Suggested Bid**
- **My Bid**

### Suggested Bid

**Suggested Bid** is a data-derived reference price for placing a buy order. It is intended to expose the market context and leave the final decision to the player; it is not a prediction or a guaranteed fill price.

The model adapts to the history already available for that item:

- less than 7 days of usable history — **Developing**
- 7 to 29 days — **Recent 7d** model
- 30 to 89 days — **30d** model
- 90 days or more — full **30d + 3mo** model

The full model uses the 30-day average as the primary fair-value anchor, with 70% weight on the 30-day average and 30% on the 3-month average. A small 7-day momentum adjustment is applied at 20% of the measured move and capped at +/-2%. **ACTIVE** volatility trims 0.5% and **WILD** volatility trims 1.0%.

Suggested Bid uses history already available to Copper&Claw and **does not itself trigger the 1-year / GW2TP gap-fill request**.

If the model-derived value is above the current market, the displayed Suggested Bid is capped at **current highest buy order + 1 copper** so the addon does not suggest paying more than is needed to take the top visible bid. When this cap is used, the underlying model value remains visible in the hover tooltip. Suggested Bid is also kept below the current lowest sell listing.

The hover tooltip exposes the model basis, coverage, historical inputs, volatility adjustment, current highest buy order, lowest sell listing, spread, Prowl context, and the signed difference between the current top bid and the Suggested Bid.

### My Bid

**My Bid** is a manual, persistent per-item price reference. It is intentionally independent from **Suggested Bid**.

Use **EDIT** to enter a gold / silver / copper value, or use **Use Suggested** to copy the current Suggested Bid as a one-time starting value. After saving, My Bid remains fixed until the player edits or clears it; later changes to Suggested Bid do not change the saved My Bid.

The My Bid tooltip can compare the saved value with:

- the current highest buy order
- the current Suggested Bid
- the time the My Bid value was saved

Copper&Claw also shows a compact **My Bid Status** against the current highest visible buy order:

- **AT TOP** — My Bid exactly matches the current highest buy order
- **OUTBID** — the current highest buy order is above My Bid; the UI shows the coin amount My Bid is behind
- **ABOVE TOP** — My Bid is above the current highest buy order; the UI shows the coin amount My Bid is above it
- **NO MARKET DATA** — no usable current buy price is available for comparison

My Bid Status is a market comparison only. Copper&Claw does **not** claim that the corresponding Trading Post order still exists, is unfilled, or belongs to the player.

In-game validation confirmed My Bid persists when switching watched items, changing Trend Windows, and restarting Guild Wars 2. The **AT TOP**, **OUTBID**, and **ABOVE TOP** status states were also validated in-game.

### Avg & Roll Avg

The chart includes two different average references:

- **Avg** — the time-normalized average price across the entire selected Trend Window
- **Roll Avg** — a moving local average intended to follow the item's more recent short-term direction

The whole-window Avg is also used by the surrounding price analysis so the chart and analysis cards refer to the same reference level.

### Market Depth

The compact **Market Depth** strip shows current order-book size and the newest quantity change for:

- **Sell Listings**
- **Buy Orders**

Market Depth is **order-book depth, not completed trade volume**. A decrease means fewer units remain in the order book, but Copper&Claw cannot determine from public market snapshots whether those units were filled, cancelled, or removed for another reason.

### Prowl Radar

**Prowl Radar** compares current market behavior with each item's recent rolling normal and surfaces confirmed unusual movement from the Collector.

Radar has four high-level views:

- **Live** — currently active Prowl signals
- **New 1h** — active signals first detected within the last hour
- **Following** — Prowl items the user explicitly chose to follow
- **Resolved 24h** — signals that resolved during the previous 24 hours

Plain-language signal filters remain available:

- **Getting Pricier**
- **Getting Cheaper**
- **Fewer Listings**
- **Unusual**

Current sort options include:

- **Most Significant**
- **Biggest % Move**
- **Newest**
- **Name A-Z**

Radar cards show current Sell / Buy prices, movement, first-seen time, status, item details, and Follow state. Card actions can jump to an already watched item, add an item to the Watchlist, refresh its price, or follow it for 24 hours, 48 hours, or until manually removed.

Active and resolved signals are requested separately from the Collector. This prevents a large active result set from crowding resolved events out of the Radar feed.

The Collector `/signals` endpoint is paginated with `limit` + `offset`. Each individual request remains capped at 500 rows, while Copper&Claw automatically follows `hasMore` / `nextOffset` until the active or resolved feed is complete. Active and resolved feeds are paged independently, so Radar is no longer intentionally truncated at 500 rows and the old user-facing **FEED LIMIT** / `(cap)` presentation is no longer used.

Tiny-price percentage noise is also filtered from Radar presentation so economically trivial copper moves do not dominate the card set.

The Item Analysis snapshot continues to show the selected item's current Prowl state. When no active Prowl event is detected, the snapshot shows **NO SIGNAL — No unusual activity detected**. This is intentionally separate from **Market Volatility**: an item can be ACTIVE or WILD in volatility while Prowl still reports NO SIGNAL if no breakout, breakdown, supply squeeze, or dislocation is currently confirmed.

Confirmed Prowl events are drawn as purple historical markers on both the Sell and Buy charts so both sides of the market can be inspected at the event timestamp. The marker does not mean both sides moved; it marks the same market event on each chart for comparison.

Prowl is a live detector. Imported older price history can appear on long Trend Windows without retroactively creating Prowl events that were never detected live.

### Market Volatility

Copper&Claw summarizes overall market instability with a colored badge:

- **CALM** — movement score below 0.25%
- **NORMAL** — 0.25% to below 0.75%
- **ACTIVE** — 0.75% to below 2.00%
- **WILD** — 2.00% or higher
- **DEVELOPING** — insufficient reliable selected-window data

Market Volatility measures scan-to-scan percentage movement rather than direction or dispersion around an average. Sell Listing and Buy Order movement are calculated separately, and the more active side determines the single overall badge.

A market can therefore be **WILD** even if it eventually returns near its starting price. Repeated large swings matter more than one isolated jump followed by stable prices.

Hovering the Volatility badge shows the current badge meaning, selected Trend Window, movement score, thresholds, and an explanation that volatility measures instability rather than price direction.

### Confidence & Coverage

Copper&Claw also reports:

- Confidence
- selected-window coverage
- sample count inside the selected Trend Window

When Collector-backed snapshots are available, coverage uses completed Collector scan timestamps so unchanged prices still count as real observations.

<img width="674" height="208" alt="collector statusup" src="https://github.com/user-attachments/assets/19b6452c-a33d-453b-9884-242508d960a1" />

Without the Collector, Copper&Claw uses its gap-aware local-history fallback.

<img width="706" height="214" alt="collector statusdown" src="https://github.com/user-attachments/assets/ca837810-c4ae-4bd7-a33e-378fa63983c1" />

Selected-window analysis stays **DEVELOPING** when the requested time window is not sufficiently covered, preventing partial history from being presented as a complete longer-window conclusion.

---

## Target Alerts

Copper&Claw supports independent **Sell Targets** and **Buy Targets** for each watched item.

### Sell Targets

A Sell Target is useful when you are waiting for the lowest Trading Post sell listing to reach a price you are willing to pay.

The alert triggers when:

`Current lowest sell listing <= Sell Target`

### Buy Targets

A Buy Target is useful when you are waiting for buyers to offer enough for an item you want to sell.

The alert triggers when:

`Current highest buy order >= Buy Target`

Sell and Buy targets are stored independently and can be enabled, changed, or cleared separately.

The normal watch list keeps target information compact. Use **Edit Targets** when you need to change the gold, silver, or copper values.

<img width="722" height="237" alt="targetalert" src="https://github.com/user-attachments/assets/d96b2118-75e9-4ba6-ba68-d9c2396d6628" />

Alerts include:

- **TARGET REACHED!**
- item name
- current Trading Post price
- your target price
- **CURRENT SELL** or **CURRENT BUY** depending on the alert
- confetti / celebration effects
- **Dismiss Party**

Sell Target hits use Sell-side visual styling while Buy Target hits use Buy-side visual styling so the triggered side is immediately clear.

Multiple triggered alerts are queued and shown one at a time.

Target values and their alert state persist across restarts. An already-visible celebration banner itself is not restored after restarting Guild Wars 2.

---

## Variant-Aware Item Search

Some Guild Wars 2 items share the same visible name while using different item IDs and separate Trading Post listings.

Copper&Claw can distinguish variants such as:

- Power
- Precision
- Toughness
- Vitality
- Healing Power
- Concentration
- Expertise
- Condition Damage

<img width="341" height="214" alt="varientsearch" src="https://github.com/user-attachments/assets/f352a56f-e1df-4e14-9b0b-5d0120f96611" />

This makes it easier to select the exact Trading Post listing you intended to watch.

---

## Price History

Copper&Claw preserves its own local Trading Post history and can also import historical market data from the standalone Copper&Claw Collector when that service is available.

Current behavior:

- preserves successfully recorded local observations
- does **not** automatically discard or compact older local observations
- retains local history across game restarts
- merges Collector history into existing Copper&Claw history
- skips exact duplicate imported observations
- uses the combined available history for trends and analysis
- newly watched items can immediately gain earlier history already collected by the Collector
- continues working from local history if the Collector is unavailable
- can request additional 1-year history on demand when a **6 Months** or **1 Year** Trend Window needs older coverage

If the Collector has never observed an item, Copper&Claw begins with whatever history is available locally.

The old standalone History panel has been removed from the watch-list UI. Historical observations continue to power charts, trends, confidence, coverage, volatility, statistics, Market Prowl context, and rolling analysis.

### Trend Windows

- 15 Minutes
- 30 Minutes
- 1 Hour
- 6 Hours
- 24 Hours
- 3 Days
- 7 Days
- 30 Days
- 3 Months
- 6 Months
- 1 Year

The Sell and Buy charts use the currently selected **Trend Window**, so the graphs and surrounding market analysis refer to the same period.

Current chart presentation includes:

- timestamp-based horizontal spacing
- a continuous observed-price line designed to remain readable with dense Collector samples
- dynamically sized HIGH / AVG / LOW price references
- X-axis labels for the full selected window, midpoint, and **Now**
- dashed whole-window **Avg**
- solid **Roll Avg**
- current price shown beside the chart
- absolute coin change and percentage change for the selected Trend Window
- a small latest-price endpoint marker
- historical Market Prowl event markers
- hover tooltips for exact historical values and Prowl events

The observed-price renderer changes presentation only. Exact stored history remains the source for analysis, hover values, statistics, averages, and Prowl positioning.

Trend direction is strict to the selected window. If available history does not reach the requested boundary, Copper&Claw shows **Developing** rather than silently treating a shorter history period as complete.

### 1-Year / Hybrid History

The **1 Year** view remains Collector-first.

When the Collector already covers the requested year, Collector history is used directly. If older coverage is missing, the Collector can use its persistent cached GW2TP 1-year fallback to fill only the older uncovered portion. Collector observations remain authoritative wherever the two sources overlap.

When both sources are involved, Copper&Claw can identify the analysis source as **Hybrid History**.

---

## Copper&Claw Collector Integration

Copper&Claw can use the standalone **Copper&Claw Collector** as its preferred market-data source.

The current default connection is the hosted HTTPS Collector:

```text
https://copperclaw-api.duckdns.org/
```

The persisted connection settings are:

```text
collectorHost=copperclaw-api.duckdns.org
collectorPort=443
collectorUseHttps=1
```

Existing installations using the original local defaults are migrated automatically when they still contain either of these exact legacy configurations:

```text
localhost:5177 over HTTP
127.0.0.1:5177 over HTTP
```

Current integration behavior:

- current watched-item prices are requested from the Collector first
- current-price, history, Market Prowl, Market Depth, and completed-snapshot requests use the configured Collector connection
- historical Collector data can be imported for watched items
- bounded history requests are used instead of downloading unlimited lifetime history for routine analysis
- completed Collector scan timestamps are used to measure selected-window coverage and reconstruct unchanged-price observations
- completed-snapshot requests are time-bounded rather than downloading the Collector's entire scan timeline
- explicit long-window **6 Months / 1 Year** history requests are handled on demand
- the Collector checks its own 1-year coverage before considering the cached GW2TP fallback
- GW2TP is used only to fill older missing 1-year history and is not continuously polled by Copper&Claw
- Collector data remains authoritative where Collector and cached GW2TP history overlap
- existing Copper&Claw local history is preserved
- imported Collector history is merged with local history
- exact duplicate historical observations are skipped
- if the Collector is unavailable when Guild Wars 2 starts, history import can retry later automatically
- automatic refresh uses a 5-minute cadence while the Collector is available
- automatic refresh falls back to 60 seconds when the Collector is unavailable
- manual Refresh and Refresh All remain immediate when accepted, with a shared 10-second cooldown
- if the Collector cannot provide current pricing, Copper&Claw falls back to the official Guild Wars 2 API
- the Trading Post tab shows Collector connection state, active price source, active analysis source, last successful Collector contact, and last completed Collector scan age
- analysis source can report **Collector Snapshots**, **Local History**, or **Hybrid History** as appropriate
- completed scan age is shown normally while fresh, turns yellow at 10 minutes, and red at 15 minutes or older

The status line distinguishes service reachability from data freshness. `Contact` shows how long it has been since Copper&Claw last successfully requested data from the Collector, while `Scan` shows the age of the newest completed Collector market scan.

The Collector remains optional from the addon's point of view. If the hosted Collector cannot be reached, Copper&Claw continues using the Guild Wars 2 API for current pricing and its own local history for analysis.

---

## Backup & Restore

Copper&Claw includes a dedicated **Backup & Restore** tab for protecting locally collected data.

<img width="1500" height="1174" alt="backuprestore" src="https://github.com/user-attachments/assets/e2b355ba-1edb-479f-acb8-bc405b950aa8" />

Automatic backups are enabled by default.

Available backup intervals:

- 6 hours
- 12 hours
- 24 hours
- 3 days
- 7 days

Retention options:

- keep 5 backups
- keep 10 backups
- keep 20 backups

The default configuration is:

- automatic backups: **On**
- backup every: **24 hours**
- keep: **10 backups**

The tab also provides:

- **Back Up Now**
- a simple date/time list of available backups
- **Restore**
- **Delete**
- Last backup
- Next backup
- Last restored

Restore and Delete remain inactive until a backup is selected. Clicking the selected backup again deselects it.

Restores are applied safely on the next Guild Wars 2 startup. Copper&Claw creates an internal safety snapshot before replacing live data, but that recovery mechanism is hidden from the normal backup list so the interface stays simple.

After a successful restore, Copper&Claw records which backup was restored and shows it as **Last restored**.

Backups include:

- Copper&Claw settings, including persistent per-item My Bid references
- watched items
- Sell Targets
- Buy Targets
- local Trading Post price history

The item-search cache is not included because Copper&Claw can rebuild it automatically.

---

## First-Time Item Indexing

On first launch, Copper&Claw builds a searchable Trading Post item index using the Guild Wars 2 API.

<img width="543" height="56" alt="itemindex" src="https://github.com/user-attachments/assets/fe8e9588-9d5b-43ab-80d9-0b0cb555a305" />

While indexing is active, Copper&Claw displays a compact **ITEM INDEX // BUILDING** progress overlay directly on the game screen. The overlay does not require the Copper&Claw options panel to be open and shows processed / total item counts plus percentage once the total is known.

While the index is building:

- search results may be incomplete
- variant labels may not appear yet
- item icons may not appear yet
- hover details may not be available for every item yet
- automatic Watchlist category classification may be incomplete until item metadata is available
- some items may temporarily be missing from autocomplete

Allow the index build to finish before judging search or indexed-item metadata behavior. The progress overlay disappears automatically when indexing completes.

---

## Post-Update Item Index Notice

Some Copper&Claw updates may require the Trading Post item index to rebuild.

When this applies, Copper&Claw displays a one-time notice after the updated addon is loaded.

<img width="470" height="146" alt="updatenotification" src="https://github.com/user-attachments/assets/f6074c48-49f1-4e53-9b9c-3d53635ad518" />

The rebuild starts automatically and may take a couple minutes.

If an index build is active, the compact game-screen progress overlay appears beneath the update notice. Dismissing the notice does not stop the index build; the progress overlay remains visible near the upper center of the game screen until indexing finishes.

The notice only needs to be acknowledged once for each addon version.

---

## Persistence

Copper&Claw saves:

- watched items
- per-item My Bid references and saved timestamps
- Favorites
- Watchlist group assignments
- Heatmap presentation preference
- Prowl Follow List and follow expiration times
- Sell Targets
- Buy Targets
- Sell / Buy target alert state
- settings, including Collector host / port / HTTPS connection values
- selected Trend Window
- selected watch-list sort mode
- selected Trading Post visual theme
- backup settings
- last restored backup information
- acknowledged update-notice version
- local Trading Post history
- imported Collector history after it is merged into the local history file
- cached item-search index

Aurene's Bite is added as a **first-run starter item**, but it can be removed normally and stays removed after restart.

---
> [!IMPORTANT]
> If the hosted Collector loses connection, Copper&Claw falls back to the Guild Wars 2 API. During that time, local history continues to be recorded only for items currently on your watch list.

## Installation

1. Download `Copper & Claw.dll`
2. Place it in your Guild Wars 2 Nexus addon folder
3. Launch Guild Wars 2
4. Confirm **Copper&Claw** is loaded in Nexus
5. Open the Copper&Claw options panel
6. Allow first-time item indexing to finish if needed

Copper&Claw creates its local settings and data files automatically.

Current builds default to the hosted Collector at `https://copperclaw-api.duckdns.org/`. No separate local Collector setup is required for normal hosted-Collector use.

If the hosted Collector is unavailable, Copper&Claw falls back automatically to the Guild Wars 2 API for current prices and continues using local history for analysis.


---

## Author

**spectre.9510**
