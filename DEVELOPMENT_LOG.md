# Copper&Claw Development Log

## Project Overview

Copper&Claw is a standalone Guild Wars 2 Nexus Trading Post watcher.

The project began by extracting the already-working Trading Post framework that had been developed inside FoodReminder-Nexus into a new standalone addon. Copper&Claw is now maintained as its own separate project.

Display name: `Copper&Claw`  
Filesystem/data prefix: `CopperAndClaw`  
Latest published release: `v0.1.4`  
Development: `Active`

Current development builds may contain features or improvements that have not yet been included in the latest published release.

---

## 2026-09-23 — Automatic Watchlist Categories, My Bid & Theme Refresh

Completed the next Copper&Claw UI/persistence checkpoint with automatic Watchlist category filtering, a persistent manual My Bid reference, and a full visual-theme refresh.

### Automatic Watchlist categories

Replaced the manual Watchlist folder/group workflow with compact automatic category filters derived from Guild Wars 2 item metadata in the local searchable item index.

Current filters:

- `ALL`
- `WEAP`
- `ARMOR`
- `DYES`
- `CONS`
- `MATS`
- `UPGR`
- `TRINK`
- `OTHER`

Behavior:

- categories are display filters only
- no manual Move to Group workflow is required
- no Ungrouped maintenance is required
- the filter is session-only and defaults to `ALL` at launch
- adding a new watched item switches to that item's category so the new item does not appear to disappear
- filtering works with the existing Watchlist presentation
- Favorites remain independent and can be combined with category filtering
- the category strip stays inside the Watchlist pane and does not push Item Analysis / Trend Window farther down the page

The item-index metadata/cache path was extended so automatic classification can use item type/subtype information and dye unlock metadata while preserving the existing indexed tooltip metadata.

A first category-header sizing pass clipped `ARMOR`. The strip was tightened without increasing height; in-game validation confirmed all category labels fit on one row and the Trend Window remains fully visible.

**Result: PASS — COMPACT AUTO-CATEGORY WATCHLIST VALIDATED IN GAME**

### My Bid

Added a persistent per-item **My Bid** row to the existing Buy Orders card.

My Bid is deliberately separate from Suggested Bid:

- player enters any gold / silver / copper value manually
- **Use Suggested** can copy the current Suggested Bid as a one-time convenience
- the saved value does not track later Suggested Bid changes
- **Edit** and **Clear** remain available
- hover context compares My Bid with the current highest buy order and Suggested Bid
- saved timestamp/age is retained
- the feature is local only and does not claim that a Trading Post order is active, filled, or owned by the player

In-game persistence validation included:

- switching watched items
- switching Trend Windows
- multiple Guild Wars 2 restarts

The saved My Bid remained unchanged through all tested cases.

**Result: PASS — MY BID PERSISTENCE VALIDATED**

### Theme refresh

Reworked the Trading Post visual-theme identities around the current Guild Wars 2 release/Living World color direction and added **Guild Wars 2 Core**.

Current selector order is story-chronological:

- Guild Wars 2 Core
- Scarlet's War (LWS1)
- Glint's Prophecy (LWS2)
- Heart of Thorns
- Glint's Legacy (LWS3)
- Path of Fire
- Rise of Kralkatorrik (LWS4)
- The Icebrood Saga
- End of Dragons
- Secrets of the Obscure
- Janthir Wilds
- Visions of Eternity
- Cyberpunk 2077

Existing stored numeric theme IDs were preserved; only the selector presentation order was remapped.

Theme direction now includes:

- Core — crimson / ink / white
- LWS1 — Scarlet red / toxic lime / molten orange
- LWS2 — amber / sandstone / dry gold
- Heart of Thorns — jungle green / bright yellow / Auric gold
- LWS3 — Bloodstone coral / ash grey / Bloodstone glow
- Path of Fire — Elonian purple / warm orange / desert gold
- LWS4 — Brand fuchsia / crystal purple / branded cyan
- Icebrood Saga — icy blue / blood red / frost white
- End of Dragons — jade teal / seafoam / cyan
- Secrets of the Obscure — astral gold / shadow purple / amber
- Janthir Wilds — moss green / deep blue / ochre
- Visions of Eternity — nebula pink / violet / ethereal blue
- Cyberpunk 2077 — cyan-forward controls with acid-yellow identity accents and red danger accents

Cyberpunk was specifically corrected so the interface is no longer visually dominated by yellow.

Sell remains green and Buy remains blue across every cosmetic theme.

**Result: PASS — THEME REFRESH VALIDATED IN GAME**

### Compact-control polish

Polished the small Watchlist action button and category strip after in-game review:

- quieter idle `...` action button
- theme-aware hover/interaction styling
- stronger selected-category treatment
- selected-category underline
- category spacing tuned without increasing row height
- `ARMOR` clipping corrected

**Result: PASS**

### Build / compatibility correction

The first automatic-category item-index header accidentally regressed existing indexed tooltip fields (`itemSubtype`, `rarity`, `level`, and `tooltipText`), producing compile errors in `TradingPostUI.cpp`.

The header/index implementation was corrected to preserve all existing tooltip metadata while adding the category metadata required by the new Watchlist filter.

Release x64 rebuild passed after the correction.

**Result: BUILD PASS**

### Current checkpoint

Validated:

- Release x64 build
- automatic category filtering
- compact one-row category strip
- no added Trend Window scrolling
- My Bid persistence
- My Bid independence from Suggested Bid
- refreshed theme palettes
- Cyberpunk no longer all-yellow
- story/LWS theme naming and selector order
- existing indexed item tooltip metadata retained

**Result: PASS — READY FOR SOURCE CONTROL CHECKPOINT**

---

## 2026-09-22 — Paginated Prowl Feed, Adaptive Suggested Bid & Market Snapshot Polish

Completed the next Copper&Claw market-analysis checkpoint with full Prowl pagination, improved Prowl strength differentiation, adaptive Suggested Bid pricing, and clearer wording between Prowl state and volatility.

### Collector `/signals` pagination

The Collector signal endpoint now supports:

```text
limit
offset
hasMore
nextOffset
```

The per-request safety limit remains 500 rows, but callers can page beyond it.

Local validation of the active feed returned:

```text
Page 1: 500 rows, hasMore=true,  nextOffset=500
Page 2: 116 rows, hasMore=false
Duplicate IDs across pages: 0
```

The updated Collector was deployed to the hosted VPS and the public HTTPS endpoint was validated with the new pagination metadata.

**Result: PASS — PAGINATED COLLECTOR SIGNAL API LIVE**

### Prowl Radar pagination

Copper&Claw now pages active and resolved Prowl feeds independently:

```text
/signals?status=active&limit=500&offset=...
/signals?status=resolved&limit=500&offset=...
```

The addon follows `hasMore` / `nextOffset` until each feed is complete.

The previous user-facing `500 (cap)`, `(cap)`, and **FEED LIMIT** states are no longer used because the addon is no longer intentionally stopping at the first 500 rows.

In-game validation exceeded the old cap, including active counts above 500, while the Radar continued to render the top matching cards normally.

**Result: PASS — RADAR NO LONGER TRUNCATED AT 500**

### Market Prowl strength scoring

The Collector-side Prowl strength calculation was adjusted to reduce saturation at `100` without changing signal types, detection thresholds, confirmation rules, or the 5-copper cheap-item floor.

Current behavior:

- quality scoring uses slower diminishing returns
- persistence begins after the normal 3-scan confirmation
- persistence adds +1 per additional scan, capped at +5
- ordinary signals cap at 99
- 100 is reserved for an exceptionally strong raw signal that persists for at least 9 consecutive scans

Fresh validation produced differentiated strengths in the high-60s instead of a wall of 100s, and later active-feed checks showed no newly generated signals at 100.

**Result: PASS — MOST SIGNIFICANT SORT HAS BETTER DIFFERENTIATION**

### Suggested Bid

Added **Suggested Bid** to the existing Buy Orders card by replacing the older seller-facing signal row rather than adding another panel.

Suggested Bid is a data-derived reference price, not a fill guarantee or prediction.

The model is adaptive so new Collector installations do not require 30 or 90 days of locally collected data before the feature becomes useful:

- less than 7 days usable history → **Developing**
- 7 to 29 days → **Recent 7d**
- 30 to 89 days → **30d**
- 90 days or more → full **30d + 3mo**

The full model uses:

- 70% of the 30-day average buy price
- 30% of the 3-month average buy price
- 20% of the measured 7-day trend as a momentum adjustment, capped at +/-2%
- -0.5% adjustment for **ACTIVE** volatility
- -1.0% adjustment for **WILD** volatility

Suggested Bid builds its own history view and is independent of whichever Trend Window the player is currently viewing.

It uses history already available to Copper&Claw and **does not automatically invoke the 1-year / GW2TP gap-fill request** merely because Suggested Bid is visible.

If the model value is above the current market, the displayed suggestion is capped at:

```text
current highest buy order + 1 copper
```

This avoids suggesting that the player pay more than is needed to become the top visible bidder. When the cap is applied, the tooltip still exposes the underlying model-derived value.

The tooltip also shows:

- model basis
- model coverage
- relevant historical average(s)
- 7-day trend and applied momentum adjustment
- volatility and any volatility adjustment
- current highest buy order
- signed **Top vs suggested** coin / percentage difference
- current lowest sell listing
- spread
- Prowl context

Testing with high-value and low-value items confirmed the suggestion updates when market conditions change without simply mirroring the current top bid.

**Result: PASS — ADAPTIVE SUGGESTED BID VALIDATED IN GAME**

### Buyer View wording

The older **BUY SIGNAL** label in Sell Listings was renamed to **BUYER VIEW**.

The underlying selected-window interpretation remains:

- **Favorable** — at least 1% below average
- **Typical** — within 1% of average
- **Caution** — at least 1% above average
- **Developing** — insufficient reliable data

A hover tooltip now explains the labels and explicitly describes Buyer View as descriptive context rather than a prediction.

**Result: PASS — CLEARER NON-DIRECTIVE WORDING**

### Market Prowl idle wording

The Item Analysis snapshot previously used:

```text
CALM — Nothing unusual
```

for the absence of an active Prowl event.

That wording could look contradictory beside **Volatility: ACTIVE** or **Volatility: WILD**, even though Prowl and volatility measure different things.

The idle Prowl state is now:

```text
NO SIGNAL — No unusual activity detected
```

and uses a neutral / muted treatment rather than a positive green state.

The same **NO SIGNAL** wording is used in the Watchlist Heatmap Prowl fallback and the Suggested Bid tooltip. **Volatility: CALM** remains unchanged because CALM is still a valid volatility classification.

**Result: PASS — PROWL STATE AND VOLATILITY ARE NOW SEMANTICALLY DISTINCT**

### Minor tooltip polish

The Prowl card order-book tooltip value column was widened so longer labels such as **SELL LISTINGS** and **BUY ORDERS** no longer crowd their values.

No market calculations were changed by this spacing adjustment.

### Current checkpoint

Validated in-game:

- paginated hosted Prowl feed
- active Radar counts above the old 500-row boundary
- no user-facing FEED LIMIT state
- adaptive Suggested Bid
- current-top-bid + 1c cap
- Suggested Bid model value retained in tooltip when capped
- Buyer View wording and hover explanation
- Market Prowl **NO SIGNAL** idle wording
- Release x64 builds and current Trading Post UI render normally

**Result: PASS — READY FOR SOURCE CONTROL CHECKPOINT**

---

## 2026-09-20 — Prowl Radar, Follow List & Watchlist Heatmap

Expanded the addon-side market intelligence workflow around Market Prowl and the Watchlist.

### Prowl Radar live scanner

Renamed and expanded the dedicated Market Prowl workspace into **Prowl Radar**.

Radar now supports four high-level views:

- **Live**
- **New 1h**
- **Following**
- **Resolved 24h**

The existing plain-language signal filters remain available:

- **Getting Pricier**
- **Getting Cheaper**
- **Fewer Listings**
- **Unusual**

Existing sort modes remain available:

- **Most Significant**
- **Biggest % Move**
- **Newest**
- **Name A-Z**

The scanner uses the existing horizontal two-column card layout and currently presents the top 12 matching items.

Each card shows the current Prowl status, price movement, Sell / Buy prices, first-seen time, and item icon.

**Result: PASS — RADAR UI VALIDATED IN GAME**

### Separate active / resolved signal feeds

The original `/signals?status=all&limit=500` request could be consumed entirely by active signals, making recent resolved events unavailable to the Radar even when they existed.

The addon now requests:

```text
/signals?status=active&limit=500
/signals?status=resolved&limit=500
```

separately and merges the results for presentation.

This preserves room for the **Resolved 24h** view even when the active side is very large.

At this 2026-09-20 checkpoint, the Collector still capped each query at 500 rows. Radar therefore treated that as an explicit presentation limitation:

- affected counts showed `(cap)`
- the header showed **FEED LIMIT**
- the UI did not imply that a capped count represented the complete market total

In-game validation showed the active query reaching `500 (cap)` while the Radar continued to render normally and expose the limitation.

**Result: PASS — FEED LIMIT MADE EXPLICIT AT THIS CHECKPOINT**

> **Superseded 2026-09-22:** Collector `/signals` pagination and addon-side paging now retrieve the complete active and resolved feeds. The user-facing FEED LIMIT / `(cap)` state was removed.

### Persistent Prowl Follow List

Added lightweight addon-side Prowl following.

A user can follow a Prowl item for:

- **24 hours**
- **48 hours**
- **Until removed**

Follow state is stored in:

```text
CopperAndClaw.ini
```

using the item's GW2 item ID and an expiration Unix timestamp. An expiration value of `0` means the follow remains active until manually removed.

Expired timed follows are cleaned up automatically by the UI.

Followed items appear in the Radar **Following** view. Active Radar cards can also show remaining follow time or `UNTIL REMOVED`.

The Follow system does not require permanently adding the item to the Watchlist.

In-game testing confirmed a followed Valkyrie Barbaric Helm appeared by itself in the **Following** view and displayed the `FOLLOWING` state.

**Result: PASS — FOLLOW WORKFLOW VALIDATED IN GAME**

### Watchlist Heatmap

Added a persistent **Heatmap** presentation to the current Watchlist workspace.

Heatmap provides a compact two-column market-state grid without documenting the older Watchlist density/view dropdown model that has since been superseded by the current UI.

Each tile can show:

- Guild Wars 2 item icon
- item name
- current Sell price
- selected Trend Window percentage movement
- current volatility state
- current Prowl state when present
- Follow indicator
- Favorite indicator

Tile backgrounds and borders communicate market state at a glance, and active Prowl states receive a subtle pulse.

Interaction remains fully functional:

- left-click opens the item in **Item Analysis**
- right-click opens the item action menu
- Favorite
- Follow / stop following
- Move to Group
- Refresh Price
- Remove from Watch List

The Heatmap preference persists through restart.

**Result: PASS — HEATMAP FUNCTIONALITY VALIDATED IN GAME**

### Heatmap readability polish

The first Heatmap pass placed percentage movement and some Prowl/status text on competing horizontal space, which caused overlap on narrow tiles.

The final tile layout uses fixed rows:

```text
Item name
Sell price
Trend %        VOL state
PROWL state
FOLLOW / FAV
```

The Heatmap-only SupplySqueeze label was shortened from `FEWER LISTINGS` to `THINNING`.

Tile height was increased enough to preserve the compact two-column layout while keeping trend, volatility, Prowl, and Follow/Favorite information separated.

Long item names remain intentionally clipped/truncated in compact tiles rather than expanding tile width or height.

In-game validation confirmed the overlap was removed, including on strong Prowl items such as Soft Longbow Stave.

**Result: PASS — NO-OVERLAP HEATMAP**

### Cheap-item signal presentation

Radar and Heatmap keep the existing presentation-side filtering for economically trivial cheap-item percentage moves.

This complements the newer Collector-side 5-copper absolute price-move floor that was validated separately against Bronze Helmet Casing.

The addon presentation therefore avoids elevating stale or economically meaningless tiny-copper movement even when displaying persisted Prowl history.

**Result: PASS**

### Current checkpoint

Validated in-game:

- Prowl Radar loads and refreshes normally
- Live Radar cards render in the two-column layout
- New 1h / Following / Resolved 24h modes render
- Follow state works
- Heatmap mode works
- Heatmap item selection opens Item Analysis
- Heatmap action menu is available
- Heatmap state coloring and item icons render
- overlapping Heatmap text was corrected
- Release x64 build passes after the final Heatmap layout fix

Follow-ups from this checkpoint were completed on 2026-09-22:

- Prowl strength scoring was reworked to reduce `100` saturation and restore differentiation in **Most Significant**
- Collector `/signals` pagination plus addon-side paging removed the old 500-row Radar truncation

**Result: PASS — FOLLOW-UPS COMPLETED IN THE 2026-09-22 CHECKPOINT**

---

## 2026-09-19 — Market Prowl, 1-Year Analysis, Chart Reliability & Persistent Watchlist Groups

Completed a large post-v0.1.4 market-analysis and Trading Post UI development pass centered on Market Prowl, longer historical context, clearer chart interpretation, order-book diagnostics, and restart-safe Watchlist organization.

### Market Prowl live scanner and dedicated tab

Added the current **Market Prowl** experience backed by the Copper&Claw Collector.

Market Prowl compares live market behavior with each item's rolling recent normal and exposes confirmed unusual moves in plain language.

Current user-facing filters:

- **All**
- **Getting Pricier**
- **Getting Cheaper**
- **Fewer Listings**
- **Unusual**

Current sort modes:

- **Most Significant**
- **Biggest % Move**
- **Newest**
- **Name A-Z**

The Prowl view uses compact item cards and currently limits the main presentation to the strongest meaningful active candidates instead of flooding the user with every backend signal.

Card actions support adding an item to the Watchlist or jumping to an already watched item.

The Watchlist theme selector remains the single shared theme control; Market Prowl inherits the same active theme instead of maintaining a second independent theme selector.

**Result: PASS — MARKET PROWL LIVE UI**

### Market Prowl historical markers

Confirmed Prowl events are drawn as purple historical markers on the selected item's Sell and Buy charts.

The same event timestamp is drawn on both charts so the user can compare what each side of the market was doing when Prowl fired. A marker on the Buy chart does not imply that the Buy price itself moved.

Marker behavior includes:

- exact event timestamps in hover tooltips
- active/resolved event history
- screen-space clustering when markers are too close together
- cluster tooltips capped to the newest five events to prevent extremely tall tooltip stacks
- markers separating naturally as the user selects a shorter Trend Window

Prowl markers represent events actually detected by Prowl. Imported historical prices do not retroactively manufacture simulated Prowl events.

**Result: PASS — HISTORICAL PROWL CONTEXT**

### Cheap-item Prowl ranking normalization

Added presentation-side normalization so extremely cheap items do not dominate the Market Prowl card ranking merely because a one- or two-copper move creates a very large percentage.

The UI applies an absolute copper-movement floor that scales gently with item price and caps at one silver before an active signal can enter the ranked card set.

This normalization affects Market Prowl **presentation/ranking only**. It does not change the Collector-side analyzer that creates the underlying signals.

Stress testing later exposed that the analyzer itself can still create a mathematically valid but economically weak cheap-item signal. Bronze Helmet Casing produced an `OUT OF BALANCE` event from a roughly 4c -> 5c Buy move.

No Collector analyzer threshold change has been committed yet. Additional real examples are being collected before changing detection behavior.

**Status: OPEN VALIDATION ITEM — CHEAP-ITEM ANALYZER NOISE**

### Prowl diagnostic stress-test endpoint

Added a read-only Collector diagnostic endpoint for deliberately finding markets likely to expose hidden Prowl problems:

```text
/diagnostics/prowl-candidates
```

Default output returns five candidates in each of seven categories:

- `cheapNoise`
- `thinSellSide`
- `thinBuySide`
- `hugeSpread`
- `oneSidedMove`
- `depthCollapse`
- `priceSpike`

The diagnostic does not create Prowl signals or modify market data. It is a candidate finder for validation.

Initial stress-test controls included:

- **Bronze Helmet Casing** — cheap percentage-noise case
- **Carrion Tribal Warhorn of Vision** — transient Buy-depth collapse that recovered
- **Medallion of the Soldier** — major Buy-depth change with little price movement

The Warhorn and Medallion returned to **CALM** rather than leaving stale active Prowl conditions, while Bronze Helmet Casing remains the strongest current example of possible cheap-item analyzer noise.

A separate Valkyrie Barbaric Helm test produced a strong **LISTINGS THINNING** event where Sell price rose dramatically while Buy remained essentially flat, matching the intended Prowl interpretation.

**Result: DIAGNOSTIC WORKING — ANALYZER VALIDATION IN PROGRESS**

### Avg and Roll Avg chart references

Expanded chart interpretation with two clearly separated average references.

- **Avg** is the time-normalized average across the entire selected Trend Window.
- **Roll Avg** is a local moving average intended to follow more recent short-term direction.

The same whole-window Avg remains the canonical average used by Item Analysis so the chart and analysis metrics stay coherent.

Hover tooltips were added to explain both Avg and Roll Avg without adding permanent UI clutter.

**Result: PASS**

### Market Depth order-book diagnostics

Added a compact **Market Depth** strip beneath the Sell / Buy charts.

It displays:

- current Sell Listing quantity
- latest Sell quantity change
- current Buy Order quantity
- latest Buy quantity change
- a plain-language latest depth movement

The final presentation deliberately removed Market Depth sparklines after in-game testing showed they added visual ambiguity and unnecessary alignment problems.

The UI explicitly describes Market Depth as **order-book depth, not trade volume**. Public market snapshots cannot determine whether disappearing orders were filled, cancelled, or otherwise removed.

Market Depth history is fetched separately from the existing price-only local history representation so quantity support does not require changing the stored `TradingPostHistoryPoint` format.

**Result: PASS — COMPACT DEPTH STRIP**

### Price-chart renderer reliability

Dense Collector-backed charts exposed a rendering issue where the observed Sell / Buy line could visually break into small colored specks.

Several rendering approaches were tested:

1. denser ImGui polyline rendering
2. render-only point reduction
3. step-chart rendering
4. custom continuous solid-ribbon rendering

The step-chart version was rejected because it looked unnaturally blocky.

The accepted renderer:

- retains the normal continuous market-chart shape
- resamples only the visible line in screen space
- draws solid filled ribbon segments with overlapping joins
- avoids dependence on ImGui polyline join behavior for the observed-price line
- leaves the exact raw historical observations untouched for hover values, Prowl positioning, statistics, Avg, Roll Avg, and analysis calculations

Mithril Ore 24-hour testing confirmed the broken/speckled appearance was eliminated while Sell, Buy, Avg, and Roll Avg remained visually distinct.

**Result: PASS — SOLID RIBBON OBSERVED-PRICE RENDERER**

### Coin-denomination display polish

Updated coin formatting so zero-value denominations are omitted where possible.

Examples:

```text
0g 56s 87c -> 56s 87c
0g 0s 87c -> 87c
1g 0s 5c -> 1g 5c
```

Gold, silver, and copper chunks now use subtle denomination-specific coloring across the Trading Post UI while Sell / Buy semantic colors remain intact.

**Result: PASS**

### Longer Trend Windows and Hybrid History

The current Trend Window set is now:

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

The **1 Year** path remains Collector-first.

When Collector coverage is incomplete, the Collector can use its persistent cached GW2TP 1-year fallback to fill only the older missing portion. Collector observations remain authoritative where the two sources overlap.

Copper&Claw can display **Hybrid History** when the selected long-window analysis combines those sources.

Market Depth avoids treating GW2TP fallback rows as real depth observations because the external OHLC fallback does not provide Collector order-book quantity history.

**Result: PASS — 1Y / HYBRID HISTORY AVAILABLE**

### Watchlist layout and grouping

The accepted Watchlist presentation now includes:

- stacked Sell / Buy prices on watched-item cards
- a right-edge Watchlist collapse control
- a narrow icon rail while collapsed
- watched-item icon selection from the collapsed rail
- persistent Watchlist groups:
  - Ungrouped
  - Investments
  - Consumables
  - Materials

The first grouping implementation kept assignments only in a local in-memory map and therefore reset after a full game restart.

Group assignments were moved into `CopperAndClawSettings` and are now serialized to `CopperAndClaw.ini`.

Current behavior:

- moving an item to a group saves immediately
- moving back to **Ungrouped** removes the saved assignment
- removing a watched item removes any stale saved group assignment
- a full Guild Wars 2 restart restores the group assignments

Restart testing with three items in **Investments** confirmed all three returned to the correct group.

**Result: PASS — WATCHLIST GROUP PERSISTENCE**

### Tooltip / interpretation pass

Added or expanded contextual hover help for:

- Market Prowl legend
- Market Prowl item details
- Avg
- Roll Avg
- Market Depth
- Market Volatility

The Volatility tooltip now explains the current badge rather than merely defining the word volatility.

It shows:

- the current classification
- selected Trend Window
- actual movement score
- CALM / NORMAL / ACTIVE / WILD thresholds
- that volatility measures instability, not direction
- why repeated large swings matter more than one isolated jump followed by stable prices

The main UI remains compact; the explanatory detail appears only on hover.

**Result: PASS — CONTEXTUAL HELP WITHOUT PERMANENT CLUTTER**

### Current validation state

Recent in-game checks confirmed:

- Market Prowl cards and historical markers render correctly
- Prowl marker clustering remains readable
- Market Depth is compact and aligned
- Avg / Roll Avg / Prowl legend tooltips work
- the custom observed-price renderer remains continuous under dense 24-hour data
- Watchlist group assignments survive a full Guild Wars 2 restart
- the 1 Year Trend Window remains available after the settings persistence update
- no blocking regression was observed after the latest build fixes

Current known follow-up:

- continue observing cheap-item Prowl signals before changing Collector-side analyzer thresholds

**Result: PASS — READY FOR COMMIT CHECKPOINT**

---

## 2026-09-14 — Persistent Expansion & Living World Theme System

Added a persistent visual-theme system to the current #5K/#5N Trading Post split-dashboard without changing the accepted layout, spacing, analysis model, or one-live-analysis performance constraint.

### Theme persistence foundation

Added `tradingPostThemeIndex` to Copper&Claw settings and persistence.

Behavior:

- Janthir Wilds remains the default / release-candidate baseline
- theme selection is saved in `CopperAndClaw.ini`
- the selected theme survives a full Guild Wars 2 restart
- existing Janthir and Cyberpunk theme indices were preserved while additional themes were appended

A full restart with Cyberpunk selected confirmed persistence.

**Result: PASS — THEME PERSISTENCE**

### Available themes

Current theme set:

- **Janthir Wilds**
- **Cyberpunk 2077**
- **Heart of Thorns**
- **Path of Fire**
- **End of Dragons**
- **Secrets of the Obscure**
- **Visions of Eternity**
- **Scarlet's War (LW1)**
- **Glint's Prophecy (LW2)**
- **Glint's Legacy (LW3)**
- **Rise of Kralkatorrik (LW4)**
- **The Icebrood Saga (LW5)**

The expansion / Living World palettes alter dashboard identity colors such as borders, headers, accents, selected-item treatment, panel surfaces, telemetry accents, and ACTIONS styling while retaining the same underlying workspace structure.

Visions of Eternity was moved to a more magenta-forward identity so it reads more distinctly from Path of Fire's orange/crimson desert palette.

### Semantic Sell / Buy colors locked across themes

Testing exposed that cosmetic theme colors could make watch-list Sell and Buy prices too similar.

The theme system was corrected so market-side colors remain semantic across every visual theme:

- **Sell = green**
- **Buy = blue**

The surrounding theme palette can change freely without changing the meaning of Trading Post price colors.

**Result: PASS — CONSISTENT MARKET-SIDE COLORS**

### Cyberpunk ACTIONS contrast polish

The initial Cyberpunk ACTIONS button used a bright accent treatment that made the label difficult to read, especially on hover.

The button was adjusted so:

- normal and hover states retain the Cyberpunk identity
- text uses strong contrast against cyan / acid-yellow button states
- active-state contrast remains readable
- Janthir and the other theme layouts remain unaffected

In-game testing confirmed the revised ACTIONS button is substantially clearer.

**Result: PASS — CYBERPUNK ACTIONS CONTRAST**

### Theme-set checkpoint

The full theme dropdown was tested in-game and the current palette set was accepted for now.

No public release was published during this checkpoint. Latest published release remains `v0.1.4`.

---

## 2026-09-13 — Release-Candidate UI, Item-Index Overlay & Debug Updater Validation

Continued post-v0.1.4 development with a Trading Post layout/performance pass, richer indexed-item hover information, a true game-screen item-index progress indicator, and Debug-only Nexus GitHub update-provider validation.

### Single-live Item Analysis performance model

Testing showed that keeping multiple heavy watched-item Details/Analysis bodies live at the same time caused cumulative UI degradation as more items were expanded.

A one-live-analysis diagnostic confirmed the source of the problem.

Current constraint:

- only one heavy selected-item Analysis body remains live at a time
- the existing details-analysis cache is retained
- the current split-dashboard presentation uses one selected watched item and one live analysis pane
- do not return to multiple simultaneously live heavy analysis bodies without a separate virtualization/suspension design

**Result: PASS — ONE-LIVE-ANALYSIS MODEL RETAINED**

### Janthir split-dashboard release-candidate presentation

The Trading Post workspace was refined into the current Janthir-themed split-dashboard release candidate.

Current presentation includes:

- wider compact watch list on the left
- one selected-item Analysis pane on the right
- stacked item icon row above the watched-item name so long names have more horizontal room
- minimal selected-item header with an `ACTIONS` control instead of repeating price/target information
- additional vertical breathing room between Sell Listings and Buy Orders
- Confidence / Coverage / Samples / Market Volatility / Spread kept below the primary Sell / Buy analysis
- only one heavy selected-item analysis remains live

The accepted spacing/presentation baseline was preserved before the item-index progress work.

**Result: PASS — RELEASE-CANDIDATE BASELINE**

### Indexed-item hover tooltips

Indexed Guild Wars 2 item metadata is now reused for hover information on watched-item icons.

Behavior:

- hovering the item icon can show a brief item description / indexed stats when available
- tooltip data comes from metadata already downloaded for the local item index
- no new Guild Wars 2 API request is made merely because the user hovers an icon
- rebuilt index metadata is allowed to populate before judging tooltip completeness

In-game testing confirmed the hover tooltips work after the item index has rebuilt with the richer metadata.

**Result: PASS**

### True game-screen item-index progress overlay

Several progress-indicator placements were tested.

Earlier versions placed the progress UI inside the Trading Post workspace or reused an ImGui window associated with Copper&Claw's update-notice rendering. Those versions were rejected because the progress indicator still read as part of the addon window.

The accepted implementation deliberately keeps item-index progress UI out of `TradingPostUI.cpp`.

Current behavior:

- progress rendering lives in the global `RT_Render` / `AddonRender` path in `entry.cpp`
- rendering uses `ImGui::GetForegroundDrawList()` instead of a child/options window
- the panel is therefore drawn directly on the game-screen foreground layer
- the compact Janthir-themed panel shows `ITEM INDEX // BUILDING`
- processed / total item counts and percentage are shown when the total is known
- while the `Copper&Claw Updated` notice is visible, the progress panel appears directly beneath it
- after the update notice is dismissed, the progress panel remains near the upper center of the game screen
- the user does not need to open the Copper&Claw options panel to see indexing progress
- the progress panel disappears automatically when the index build completes

Fresh-install testing confirmed the progress overlay appears independently on the actual game screen.

**Result: PASS**

### Fresh-install data restoration validation

The first-run indexing test used temporary fresh copies of Copper&Claw's normal local data files.

After the progress-overlay test completed:

- Guild Wars 2 was closed
- the newly generated test data files were removed
- the previously saved original Copper&Claw data files were restored
- normal settings, watched-item data, history, and item-index data were recovered successfully

**Result: PASS — ORIGINAL DATA RESTORED**

### Debug-only Nexus GitHub update provider

The current Nexus API header was checked before enabling the updater.

Confirmed API support:

- `EUpdateProvider::UP_GitHub`
- `AddonDefinition_t::Provider`
- `AddonDefinition_t::UpdateLink`

The updater is intentionally isolated to Debug builds for now.

Current `entry.cpp` behavior:

```cpp
#ifdef _DEBUG
    AddonDef.Provider = UP_GitHub;
    AddonDef.UpdateLink =
        "https://github.com/deadwrong03-ops/Copper-Claw-GW2-nexus";
#else
    AddonDef.Provider = UP_None;
    AddonDef.UpdateLink = nullptr;
#endif
```

This keeps existing non-Debug / Release behavior unchanged while allowing Nexus GitHub-release update handling to be tested safely with the development DLL.

The Debug build loaded successfully with the GitHub update provider configured.

**Result: PASS — DEBUG-ONLY UPDATER CONFIGURATION**

Updater behavior was then tested with a temporary Debug-only `0.1.3.0` version against the existing public `v0.1.4` GitHub release.

Nexus attempted to query:

```text
/repos/deadwrong03-ops/Copper-Claw-GW2-nexus/releases
```

but GitHub returned `404 Not Found` to the unauthenticated request. Direct unauthenticated checks against both the repository and the `deadwrong03-ops` user endpoint also returned `404`, while the signed-in GitHub UI showed the repository as Public.

Conclusion:

- Nexus is reaching the configured GitHub repository path
- the current updater code should remain unchanged
- the live update test is blocked by GitHub account/repository visibility to unauthenticated clients, not by Copper&Claw's update-provider wiring
- the temporary `0.1.3.0` test build was discarded afterward and normal `0.1.4.0` development state was restored

**Status: GITHUB VISIBILITY ISSUE BLOCKS LIVE UPDATE TEST**

### Temporary mouse-wheel diagnostics removed

The temporary raw mouse-wheel diagnostic code from the earlier UI input investigation has now been removed from `entry.cpp`.

Removed diagnostic-only pieces included:

- `CopperAndClawWndProc`
- raw `WM_MOUSEWHEEL` counters
- `WndProc_Register` / `WndProc_Deregister`
- the `WHEEL DEBUG` readout
- the `Reset Wheel Debug` control
- ImGui wheel-event counters used only for diagnosis

The cleanup intentionally preserved the accepted #5N game-screen item-index overlay and the Debug-only GitHub updater configuration.

In-game validation after rebuilding confirmed:

- Nexus reports Copper&Claw `0.1.4.0`
- the Copper&Claw footer reports `v0.1.4`
- the wheel-debug UI is gone
- Copper&Claw continues to load and render normally

**Result: PASS — TEMPORARY WHEEL DIAGNOSTICS REMOVED**

No new public release was published during this checkpoint. Latest published release remains `v0.1.4`.

---

## 2026-09-12 — v0.1.4 Release

Completed the final Trading Post chart/presentation pass, release smoke test, and public v0.1.4 release.

### Market Analysis card presentation

The left-side Sell Listings and Buy Orders analysis blocks are now visually boxed into separate lightweight cards.

Current presentation:

- Sell Listings uses the existing green Sell-side accent
- Buy Orders uses the existing blue Buy-side accent
- cards use subtle borders and stronger left accent lines
- no heavy filled background was added, keeping the analysis readable and compact

**Result: PASS**

### Sparkline chart presentation

The Sell and Buy sparklines were upgraded from plain mini-lines into compact market charts while keeping the underlying stored history and analysis calculations unchanged.

Current presentation includes:

- dynamically sized Y-axis price-label gutter so expensive items do not overlap the chart
- max / midpoint / min Y-axis price labels
- X-axis labels for the full selected Trend Window, exact half-window, and `Now`
- timestamp-based point placement
- faint horizontal and vertical midpoint guides
- current price displayed beside the chart
- selected-window coin change and percentage change displayed beside the chart
- thicker 2 px Sell / Buy chart lines
- current-price endpoint dot

A filled/glow-under-line experiment was implemented and tested in-game, but the result looked visually banded/jagged with real Collector-backed data. The glow/fill was removed.

Decision:

- keep the chart line clean and unfilled
- retain the endpoint dot
- retain the axes/current-price/change information

**Result: PASS**

### Live endpoint sparkle

Added a very small periodic animation to the newest-price endpoint marker.

Behavior:

- endpoint dot remains visible normally
- approximately every four seconds, a short faint ring expands from the dot
- a small cross-shaped glint appears briefly with the ring
- the effect fades in under one second
- Sell uses the Sell-side green
- Buy uses the Buy-side blue
- no animation is applied to the chart line itself

In-game screenshot validation confirmed the effect reads as a subtle live-data cue without distracting from the chart.

**Result: PASS**

### Release smoke test

Current in-game smoke-test status:

- Copper&Claw loaded successfully through Nexus — PASS
- game remained stable with the current build loaded — PASS
- hosted Collector connection active — PASS
- `Price: Collector` active — PASS
- `Analysis: Collector Snapshots` active — PASS
- Collector-unavailable fallback behavior — PASS from previously validated integration testing
- Sell Target behavior — PASS
- Buy Target behavior — PASS
- Favorites persistence across normal use/restarts — PASS
- Backup & Restore tab / ongoing backups — PASS
- final chart/card/sparkle presentation — PASS

The final v0.1.4 Release x64 DLL was rebuilt, installed, and verified in-game. Nexus reported `0.1.4.0`, the in-app footer reported `Copper&Claw v0.1.4`, the release-prep commit was pushed, and GitHub release `v0.1.4` was published as the latest normal release.

**Result: PASS — v0.1.4 PUBLISHED**

---

## 2026-09-11 — Hosted Collector Deployment

Moved Copper&Claw's default Collector connection from the original local-development service to the hosted HTTPS Collector.

### Hosted Collector defaults

`Settings.h` now defaults to:

```text
collectorHost=copperclaw-api.duckdns.org
collectorPort=443
collectorUseHttps=1
```

All existing Collector networking paths already use these persisted settings, so no `TradingPostPriceManager` or `TradingPostHistoryManager` networking rewrite was required.

### Legacy local-settings migration

Existing installations may still have the original local Collector configuration saved in `CopperAndClaw.ini`.

`Settings::Load` now recognizes both legacy local host forms when they use the original port and HTTP mode:

```text
localhost:5177
127.0.0.1:5177
```

When either exact legacy configuration is found, Copper&Claw migrates it to the hosted HTTPS Collector and persists the new values automatically.

Custom Collector configurations are not changed by this migration.

### Hosted runtime validation

Validated the hosted path end to end.

Infrastructure checks:
- `copperclaw-collector` systemd service active after VPS reboot
- Caddy active after VPS reboot
- public HTTPS health endpoint reachable at `https://copperclaw-api.duckdns.org/health`

Addon checks:
- addon build passed after hosted-default changes
- Copper&Claw connected successfully to `copperclaw-api.duckdns.org:443`
- HTTPS mode remained enabled
- Trading Post current-price data loaded from the Collector
- Collector snapshot-backed analysis remained available
- a full Guild Wars 2 restart still returned `Collector: Connected`
- corrected legacy migration build passed and remained connected in-game

The existing Guild Wars 2 API fallback remains available if the hosted Collector cannot provide current pricing.

**Result: PASS — HOSTED COLLECTOR ACTIVE**

## 2026-09-12 — Hosted Collector History Migration

Safely merged the historical data collected by the original Windows/local Collector into the production VPS Collector database so hosted collection could continue without losing the earlier market history.

### Migration safeguards

The live Collector service was stopped before database replacement.

Before any merge work:

- the production VPS SQLite database was checkpointed
- `PRAGMA integrity_check` returned `ok`
- a pre-merge backup was retained as `CopperClaw-before-merge-backup.db`
- the old Windows Collector database was uploaded separately
- all merge work was first performed against a disposable copy of the VPS database

### Import boundary

The old Windows Collector had continued running after the VPS Collector began, so the databases contained overlapping time ranges.

To prevent duplicate or conflicting production history, the migration imported only old Collector runs that were both:

- `COMPLETE`
- started strictly before the VPS Collector's first scan at `2026-09-11T05:38:44.1801029Z`

Migration selection:

- 273 old runs existed before the cutoff
- 264 were `COMPLETE` and imported
- 9 failed runs were excluded
- all old runs at or after the VPS start time were excluded

### Imported history

The successful migration added:

- 264 completed historical snapshot runs
- 305,162 `price_history` rows
- 27,987 legacy `price_snapshots` rows

Existing VPS `current_prices` and `item_metadata` were preserved.

The merged database contained 387 total snapshot runs immediately before the Collector was restarted.

### Validation

Validation passed at each stage:

- merge-test database integrity check: `ok`
- foreign-key check: clean
- live merged database integrity check: `ok`
- `copperclaw-collector` systemd service restarted successfully and returned `active`
- public HTTPS `/health` returned `status: ok`
- Copper&Claw connected in-game using the hosted Collector
- Collector-backed pricing and snapshot analysis remained active
- historical sample counts and graphs were populated after migration

The original pre-merge VPS database backup is being retained as a rollback copy.

**Result: PASS — LOCAL HISTORY MIGRATED TO HOSTED COLLECTOR**



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


## 2026-09-10 — Collector-Backed Market Analysis Coherence Pass

Completed a broad Trading Post analysis accuracy and coherence pass after the first working Copper&Claw ↔ Collector integration.

### Completed Collector snapshot coverage

Added Collector support for exposing completed market scans and using those timestamps inside Copper&Claw analysis.

Collector endpoint:

```text
GET /snapshots/completed
```

Copper&Claw now caches completed Collector snapshot timestamps and uses them to reconstruct sampled watched-item history.

For each completed Collector scan, the addon carries forward the most recently known watched-item price state at or before that scan timestamp.

This allows unchanged prices to count as real observations instead of disappearing simply because the Collector's item history stores change events rather than duplicate unchanged rows.

Collector-backed coverage now reflects completed scans inside the selected Trend Window.

Validation examples included:

- `30m / 30m (100%)`
- six or seven completed samples on a 30-minute window
- approximately `5h 50m / 6h (97%)`
- long windows correctly remaining incomplete when only partial Collector history exists

A 3-day test with only about 11h 35m of Collector coverage correctly showed:

- LOW confidence
- 16% coverage
- Direction: Developing
- Tracked Avg: Developing
- Buyer / Seller signals: Developing
- Market Volatility: Developing

**Result: PASS**

### Strict selected-window trend behavior

Removed the old fallback that could use the entire available history when the selected Trend Window did not have enough history.

Direction is now tied strictly to the selected window.

If history does not reach the requested window boundary, the UI displays:

```text
Developing
```

instead of silently presenting a longer-period trend as though it represented the selected 30m / 1h / 6h / longer window.

The trend boundary lookup was also improved to use the historical observation closest in real time to the requested boundary while still requiring the stored history to reach that boundary.

**Result: PASS**

### Time-aware sparklines

Sell and Buy analysis sparklines now position points according to their actual timestamps inside the selected Trend Window.

The previous sample-index spacing could make unevenly spaced observations look evenly distributed in time.

Current behavior:

- x-position is based on real timestamp within the selected window
- only positive Sell / Buy values are drawn
- graph data remains restricted to the selected Trend Window
- Sell and Buy graphs remain visually aligned with their corresponding analysis sections

In-game testing across 30m, 1h, and 6h windows confirmed the graphs change coherently with the selected time period.

**Result: PASS**

### Manual refresh abuse protection

Added a shared backend cooldown for manual Trading Post refresh actions.

Current behavior:

```text
Manual refresh cooldown: 10 seconds
```

The cooldown is shared by:

- per-item `Refresh`
- `Refresh All`

A manual refresh on either path prevents another manual refresh action from being accepted until the cooldown expires.

Automatic refresh cadence is unaffected.

This protects both the Collector and Guild Wars 2 API fallback path from accidental rapid repeated manual requests.

**Result: PASS**

### Closest to Target sorting corrected

`Closest to Target` previously considered only Sell Targets.

It now evaluates both:

- Sell Target distance
- Buy Target distance

Sell Target reaches zero distance when:

```text
Current Sell <= Sell Target
```

Buy Target reaches zero distance when:

```text
Current Buy >= Buy Target
```

When both targets exist, Copper&Claw uses whichever side is proportionally closer.

In-game sorting validation confirmed the watch list reorders correctly using the combined target logic.

**Result: PASS**

### Market Volatility redesigned

The original volatility model used relative standard deviation around the selected-window average.

Live testing exposed a misleading result: one large price step followed by a stable plateau could be classified as `WILD` even though the sparkline did not show repeated market swings.

Volatility was redesigned to measure RMS scan-to-scan percentage movement.

Current interpretation:

- `CALM` — very little scan-to-scan movement
- `NORMAL` — modest movement
- `ACTIVE` — meaningful movement
- `WILD` — repeated / large movement
- `DEVELOPING` — insufficient reliable selected-window data

Sell Listing and Buy Order movement are calculated separately. The more active side determines the single overall `Market Volatility` badge.

This keeps one simple market-level label while ensuring movement on either side of the Trading Post is represented.

Runtime examples:

- flat Sell + flat Buy → `CALM`
- small temporary Sell movement + flat Buy → `CALM` / `NORMAL`
- meaningful Sell move + flat Buy → `ACTIVE`
- broader 6-hour movement on one or both sides → `ACTIVE`

**Result: PASS**

### Buyer / Seller signal consistency

Live testing exposed combinations that were individually mathematically possible but confusing in the UI, such as:

```text
Price Position: Well Above Recent Avg
For Buyers: Typical
```

Buyer- and seller-facing signals were simplified so they cannot conflict with the displayed Price Position.

Current buyer-facing behavior follows Sell Listing position:

- below recent average → `Favorable`
- near recent average → `Typical`
- above recent average → `Caution`

Current seller-facing behavior follows Buy Order position:

- above recent average → `Favorable`
- near recent average → `Typical`
- below recent average → `Weak`

This keeps the recommendation line subordinate to the price-position metric instead of using a separate quartile rule that can disagree with it.

**Result: PASS**

### Dynamic volatility sort readiness

The main Market Analysis already used a dynamic Collector sample requirement for shorter windows, but `Most Volatile` / `Least Volatile` sorting still required a hardcoded 15 samples.

This caused a mismatch where a 30-minute item could have a mature volatility badge with six valid Collector samples but be excluded from volatility sorting.

Volatility sorting now uses the same dynamic Collector readiness calculation as Market Analysis.

Collector readiness:

- scales with expected 5-minute samples in the selected Trend Window
- requires at least 75% selected-window coverage
- minimum sample threshold is bounded between 3 and 15

In-game validation confirmed both:

- `Most Volatile`
- `Least Volatile`

correctly reorder watched items on a 30-minute window.

**Result: PASS**

### Multi-window coherence validation

Performed repeated in-game coherence testing across multiple watched items and Trend Windows.

Validated examples included:

- completely flat Sell and Buy lines
- temporary movement that returned to the starting price
- one-sided Sell movement while Buy remained flat
- sustained direction changes
- 30m / 1h / 6h comparisons
- incomplete 3-day history
- Most / Least Volatile sorting
- Closest to Target sorting

The following metrics were checked together:

- Direction
- Recent Avg
- Price Position
- For Buyers
- For Sellers
- Confidence
- Coverage
- Samples
- Market Volatility
- Spread
- Sell / Buy sparklines
- sorting behavior

Observed combinations were internally coherent.

Examples confirmed that seemingly different statements can both be valid when they answer different questions, such as:

- `Getting Cheaper` while still `Above Recent Avg`
- `Stable` endpoint direction while a small temporary bump produces non-zero volatility

No remaining contradictory Market Analysis presentation was identified during this validation pass.

**Result: PASS**

---


## 2026-09-10 — Collector Status & Data-Freshness UI

Added a compact Collector status line to the Trading Post tab so the addon can show both backend connectivity and the freshness of Collector-backed market data without adding another large panel.

### Compact status presentation

The Trading Post tab now displays a single-line status summary containing:

- `Collector: Connected / Offline`
- `Price: Collector / GW2 API`
- `Analysis: Collector Snapshots / Local History`
- `Contact: ... ago`
- `Scan: ... ago`

The first status prototype used a larger multi-line block, but in-game testing showed too much unused vertical space. It was reduced to one compact line and re-tested successfully.

**Result: PASS**

### Last successful Collector contact

`TradingPostPriceManager` now records the Unix timestamp of the most recent successful Collector-backed price request.

The status UI displays the elapsed age as:

```text
Contact: 121s ago
```

This value represents the most recent successful Copper&Claw → Collector price request. It is not the age of the Collector's full market scan.

Behavior validated in-game:

- Collector running → contact age updates after successful requests
- Collector stopped → status switches to Offline / GW2 API while the last successful contact age continues increasing
- Collector restarted → the next successful request restores Connected state and resets Contact close to zero

**Result: PASS**

### Last completed Collector scan

Copper&Claw now reads the newest timestamp already available from the completed-snapshot cache and displays its age in the same status line.

Examples:

```text
Scan: 45s ago
Scan: 6m ago
```

No additional Collector endpoint or HistoryManager API was required because the UI already had access to the cached completed-snapshot timestamp list.

This distinguishes:

- Collector service reachability
- current-price request freshness
- underlying full-market snapshot freshness

**Result: PASS**

### Stale-scan warning colors

Added a lightweight visual warning for aging Collector snapshot data.

Final thresholds:

```text
Under 10 minutes: normal / muted
10 to under 15 minutes: yellow
15 minutes or older: red
```

The thresholds were temporarily lowered to 1 minute / 2 minutes for fast in-game validation.

Validation confirmed:

- the warning state turns yellow at the temporary first threshold
- the warning state turns red at the temporary second threshold
- a new completed scan resets the displayed age normally
- the final production thresholds were restored to 10 minutes / 15 minutes

**Result: PASS**

### Collector Status feature checkpoint

The completed Collector status feature now reports:

- Collector Connected / Offline
- active price source
- active analysis source
- last successful Collector contact age
- last completed Collector scan age
- stale-scan warning coloring

This remains display-only. Existing Collector pricing, history import, snapshot reconstruction, refresh cadence, fallback behavior, and Market Analysis calculations are unchanged.

**Result: PASS**

---


## 2026-09-10 — 90-Day Bounded Collector History Requests

Updated Copper&Claw's Collector history import path so watched items no longer request an item's entire lifetime history.

### Bounded history request

`TradingPostHistoryManager` now requests:

```text
/item/{itemID}/history?days=90
```

instead of:

```text
/item/{itemID}/history
```

The 90-day request matches Copper&Claw's current longest selectable Trend Window.

This reduces unnecessary data transfer and query volume as the Collector's central historical dataset grows, while preserving all history currently needed by the addon UI.

**Result: PASS**

### Pre-window state anchor

The Collector's `price_history` table stores market-change events rather than duplicate unchanged rows for every completed scan.

The Collector's bounded history endpoint therefore also returns the newest history event immediately before the requested cutoff when one exists.

Copper&Claw keeps and imports that anchor row along with the in-window history.

This allows the addon to reconstruct the price state that was already active when the 90-day window began and continue carrying that state forward across completed Collector snapshot timestamps.

No separate Copper&Claw-side anchor calculation was required.

**Result: PASS**

### End-to-end request validation

Temporary Collector request logging was enabled during validation.

After a full Guild Wars 2 restart, Copper&Claw startup traffic confirmed watched items were requesting the new bounded endpoint.

Observed examples included:

```text
API GET /item/41569/history?days=90
API GET /item/96356/history?days=90
API GET /item/20357/history?days=90
```

Normal current-price requests for those items also continued successfully afterward.

The temporary Collector request logger was removed after validation.

**Result: PASS**

### Backward compatibility

This Copper&Claw change only modifies the endpoint it requests.

The Collector still supports the unbounded `/item/{itemID}/history` path for older or other consumers, but current Copper&Claw development builds now use the 90-day form.

**Result: PASS**

---


## 2026-09-10 — 90-Day Bounded Collector Snapshot Timeline

Updated Copper&Claw's completed-snapshot request path so analysis no longer downloads the Collector's entire lifetime list of completed market scans.

### Bounded completed-snapshot request

`TradingPostHistoryManager` now requests:

```text
/snapshots/completed?days=90
```

instead of:

```text
/snapshots/completed
```

The 90-day request matches Copper&Claw's current longest selectable Trend Window.

This keeps Collector-backed confidence, coverage, stale-scan status, and unchanged-price reconstruction bounded to the maximum period Copper&Claw can currently analyze.

**Result: PASS**

### Runtime validation

Temporary Collector request logging was enabled to verify the actual addon traffic.

After installing the fresh Release x64 DLL and restarting Guild Wars 2, the Collector console showed:

```text
API GET /snapshots/completed?days=90
```

The same runtime session also continued to show watched-item history requests using:

```text
/item/{itemID}/history?days=90
```

and normal current-price requests continued successfully.

The temporary request logger was removed after validation.

**Result: PASS**

### Backward compatibility

This Copper&Claw change only modifies the endpoint form it requests.

The Collector continues to support unbounded `/snapshots/completed` requests for older or other consumers, while current Copper&Claw development builds use the 90-day bounded form.

**Result: PASS**

---


## 2026-09-10 — Configurable Collector Connection

Prepared Copper&Claw for eventual hosted Collector use by removing the hardcoded Collector address from its networking paths.

### Persistent Collector connection settings

Added Collector connection values to `CopperAndClawSettings` and `CopperAndClaw.ini`.

Current defaults:

```text
collectorHost=127.0.0.1
collectorPort=5177
collectorUseHttps=0
```

The defaults preserve the existing local Collector behavior.

`Settings::Load` reads the three values when present, validates the port range, and keeps safe defaults when values are missing or malformed.

`Settings::Save` persists the values with the rest of Copper&Claw's existing settings.

Startup now performs a save immediately after loading settings so newly introduced default keys are materialized in existing INI files without requiring a later addon unload.

**Result: PASS**

### Collector networking updated

Both Collector HTTP paths now use the persisted connection values:

- `TradingPostPriceManager` — current watched-item pricing
- `TradingPostHistoryManager` — 90-day item history and 90-day completed-snapshot timeline

The connection host is read from `collectorHost`.

The port is read from `collectorPort`.

`collectorUseHttps=1` applies `WINHTTP_FLAG_SECURE`; `0` keeps the existing plain-HTTP local behavior.

The official Guild Wars 2 API path is unchanged.

**Result: PASS**

### End-to-end validation

Validated the new configuration in-game using controlled INI changes.

Tests:

- default `127.0.0.1:5177` over HTTP → Collector connected normally
- `collectorPort=5178` while the Collector remained on 5177 → Collector went Offline and Copper&Claw fell back to the Guild Wars 2 API
- restoring `collectorPort=5177` → Collector connection returned
- `collectorHost=localhost` → Collector remained connected
- `collectorUseHttps=1` against the plain-HTTP local Collector → Collector went Offline and fallback activated
- restoring `collectorUseHttps=0` → Collector connection returned

The status line confirmed the expected source transitions:

```text
Collector: Connected | Price: Collector | Analysis: Collector Snapshots
```

and during deliberate connection failures:

```text
Collector: Offline | Price: GW2 API | Analysis: Local History
```

This confirms the host, port, and HTTP/HTTPS settings are actually controlling all Collector communication.

**Result: PASS**

### Hosted-Collector readiness

Copper&Claw is still using localhost defaults during development.

This change does not deploy or expose the Collector remotely. It removes the addon-side hardcoded address so a future VPS/HTTPS Collector can be adopted by changing configuration rather than rewriting the networking code.

**Result: PASS**

---

## 2026-09-08 — v0.1.3 Release Preparation (Superseded by 2026-09-12 Final Release Pass)

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

### Historical Watchlist density experiment

Earlier development builds included **Normal / Slim / Ultra Slim** Watchlist density modes.

That control belonged to an older stage of the Trading Post UI and has since been superseded by the current Watchlist design. The old density dropdown is no longer documented as part of the current user interface.

**Status: HISTORICAL / SUPERSEDED**

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

- Show: All Items / Favorites

The earlier density/view control from this development phase was later superseded by the current Watchlist UI.

### In-game validation

Verified in-game:

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

Behavior at this historical checkpoint originally used Sell Listing movement and a fixed 15-sample readiness floor.

This logic was later superseded by the 2026-09-10 coherence pass:

- Market Volatility now considers both Sell and Buy scan-to-scan movement
- Collector-backed volatility readiness scales dynamically with the selected Trend Window
- at least 75% selected-window coverage is still required
- items still classified as `DEVELOPING` remain below items with mature volatility data
- the sort remains display-only and does not rewrite the saved watch-list order
- the selected sort mode continues to persist in `CopperAndClaw.ini`

Live ranking behavior has now been validated in-game for both Most Volatile and Least Volatile.

**Result: PASS — SUPERSEDED BY 2026-09-10 VOLATILITY MODEL**

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

This historical implementation originally used Sell Listing relative price dispersion and a fixed 15-sample floor.

The calculation was later superseded by the 2026-09-10 Market Analysis coherence pass.

Current behavior:

- measures RMS scan-to-scan percentage movement
- evaluates Sell Listings and Buy Orders separately
- uses the more active side as one overall `Market Volatility` label
- uses dynamic Collector sample readiness for shorter windows
- requires at least 75% selected-window coverage
- otherwise displays `DEVELOPING`

The badge remains intentionally qualitative rather than adding another raw percentage to the UI.

**Result: PASS — SUPERSEDED BY 2026-09-10 VOLATILITY MODEL**

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

Copper&Claw has since published releases through `v0.1.4`.

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

The hosted Collector integration is working and validated.

Current default service address:

```text
https://copperclaw-api.duckdns.org/
```

Collector connection settings are persisted in `CopperAndClaw.ini`:

```text
collectorHost=copperclaw-api.duckdns.org
collectorPort=443
collectorUseHttps=1
```

Existing installations using the original `localhost:5177` or `127.0.0.1:5177` HTTP defaults migrate automatically to the hosted configuration. Custom Collector settings are left unchanged.

All Collector current-price, item-history, and completed-snapshot requests use the configured values.

Current behavior:

- configurable Collector host / port / HTTP-vs-HTTPS settings with hosted HTTPS defaults
- Collector-first current pricing
- Guild Wars 2 API fallback
- Collector history import using `/item/{itemID}/history?days=90`
- pre-window anchor history event supplied by the Collector for correct 90-day state reconstruction
- completed Collector snapshot timestamps requested with `/snapshots/completed?days=90` for selected-window analysis coverage
- unchanged-price states reconstructed across completed Collector scans
- automatic retry if the Collector becomes available after Guild Wars 2 starts
- 5-minute refresh while Collector is available
- 60-second refresh while falling back
- shared 10-second cooldown for manual Refresh / Refresh All

Cleaner in-game Collector connection/status presentation has been implemented and validated. The status line reports connection state, price source, analysis source, last successful Collector contact, and completed-scan age with stale-data warning colors. Tighter synchronization to Collector snapshot timing remains a possible future refinement.

### Collector database size management — TODO

The hosted Collector now runs continuously, so long-term SQLite growth needs to be self-managed before the historical database becomes unnecessarily large.

Planned retention / rollup work:

- keep full change-level history for approximately 90 days
- roll older history into hourly summaries
- roll much older history into daily summaries for long-term trend retention
- automatically prune raw rows that have been superseded by rollups
- periodically reclaim unused SQLite space after pruning
- log / monitor database file size
- add warning thresholds such as 1 GB and 5 GB

Goal: preserve useful multi-year market history without requiring a very large database file to be manually managed.

**Status: TODO — HIGH PRIORITY COLLECTOR MAINTENANCE**

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
- current Collector imports request 90 days of history plus the Collector-supplied pre-window anchor event
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

## v0.1.4 Release Checklist

- [x] set `entry.cpp` version to `0.1.4.0`
- [x] set in-app footer to `Copper&Claw v0.1.4`
- [x] update `README.md` for v0.1.4
- [x] update `DEVELOPMENT_LOG.md` for v0.1.4
- [x] hosted Collector connection validated in-game
- [x] Collector-backed pricing / snapshot analysis validated in-game
- [x] Collector fallback behavior previously validated
- [x] Sell Target behavior verified
- [x] Buy Target behavior verified
- [x] Favorites persistence verified
- [x] Backup & Restore / ongoing backups verified
- [x] final Sell / Buy analysis-card presentation verified
- [x] final sparkline axes / current-price / change presentation verified
- [x] endpoint sparkle verified in-game
- [x] clean **Release x64** rebuild completed
- [x] exact final v0.1.4 Release x64 DLL installed and tested
- [x] Nexus reports `0.1.4.0`
- [x] footer reports `Copper&Claw v0.1.4`
- [x] no blocking regression found in final smoke test
- [x] final v0.1.4 release-prep changes committed
- [x] pushed
- [x] Git tag `v0.1.4` created
- [x] GitHub release `v0.1.4` published
- [x] GitHub shows `v0.1.4` as the latest normal release

---

## Development Rule

When making future Copper&Claw changes, use the current project files as the source of truth.

Do not substitute older copies of files from prior work.

When a current source file is needed for modification, request the current file first, even if that same file was edited or supplied earlier in the current development session.
