# Copper&Claw
 <img width="75" height="75" alt="logo" src="https://github.com/user-attachments/assets/855fe874-597b-4c22-84a1-2bb9ccd30118" />

Copper&Claw is a standalone **Guild Wars 2 Nexus addon** for tracking Trading Post prices, building local price history, analyzing market movement, and alerting you when watched items reach a target price.

<img width="250" height="250" alt="main" src="https://github.com/user-attachments/assets/17c29794-13a7-4eda-80b6-66db93de544c" />



## Status

**Latest Release:** v0.1.3  
**Development:** Active

> **Development Note:** Copper&Claw is actively developed. Some recently added features or improvements may be available in the source or development build before they are included in the latest published release.

---

## Main Features

- Watch multiple Trading Post items
- Official Guild Wars 2 item icons beside watched items
- Collector-aware automatic refresh: 5 minutes when the local Collector is available, 60 seconds when falling back to the Guild Wars 2 API
- Manual per-item refresh and Refresh All
- Current **lowest sell listing**
- Current **highest buy order**
- Per-item **Sell Targets**
- Per-item **Buy Targets**
- Independent Sell and Buy target alerts
- Compact target display with dedicated **Edit Targets** controls
- Persistent watch list, targets, settings, and price history
- Optional integration with the standalone Copper&Claw Collector for broader historical data
- Search items without requiring manual item IDs
- Variant-aware search for duplicate-name items
- Separate **Sell Listings** and **Buy Orders** market analysis
- Gap-aware confidence and coverage
- Colored **Volatility** classification
- Sortable watch list
- **Normal / Slim / Ultra Slim** watch-list views
- Optional per-item **Favorites** with an instant Favorites-only view
- Sell / Buy price-history sparklines that follow the selected **Trend Window**
- Historical minimum / average / maximum prices
- Dragon Bash-style **Target Reached** alerts
- Built-in automatic **Backup & Restore** for local Copper&Claw data
- One-time post-update notice when an item-index rebuild may be required

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

Volatility sorting uses the same gap-aware history requirements as the Volatility badge. Items still marked **DEVELOPING** are placed below items with mature volatility data.

---

## Watch List Views & Favorites

Copper&Claw includes three watch-list density options:

- **Normal** — roomier rows with additional item metadata
- **Slim** — the default balanced layout
- **Ultra Slim** — a compact Item / Sell / Buy / Actions layout

All three views keep the core watch-list actions available.

Items can also be marked as **Favorites** using the star beside the item. The normal **All Items** view remains one continuous watch list, while **Favorites** instantly filters the list to starred items only.

Favorites are optional. Newly watched items do not require any organization or setup, and favorite state persists across restarts.

---

## Market Analysis

Copper&Claw separates the two sides of the Trading Post market.

### Sell Listings

- Direction
- % movement
- Recent average
- Price position
- Buyer-facing signal

### Buy Orders

- Direction
- % movement
- Recent average
- Price position
- Seller-facing signal

### Volatility

Copper&Claw summarizes sell-listing price movement with a simple colored badge:

- **CALM**
- **NORMAL**
- **ACTIVE**
- **WILD**
- **DEVELOPING**

The badge uses the selected Trend Window and stays **DEVELOPING** until enough reliable history has been collected.

### Confidence & Coverage

Copper&Claw also reports:

- Confidence
- Gap-aware selected-window coverage
- Sample count inside the selected trend window

This helps prevent limited or interrupted local history from being presented as mature market data.

<img width="1497" height="1179" alt="marketanalysis" src="https://github.com/user-attachments/assets/57479002-ec6f-4e55-a33d-b898ccd273da" />


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

<img width="541" height="214" alt="varientsearch" src="https://github.com/user-attachments/assets/f352a56f-e1df-4e14-9b0b-5d0120f96611" />

This makes it easier to select the exact Trading Post listing you intended to watch.

---

## Price History

Copper&Claw preserves its own local Trading Post history and can also import historical market data from the standalone Copper&Claw Collector when that service is available.

Current behavior:

- preserves every successfully recorded local observation
- does **not** automatically discard or compact older local observations
- retains history across game restarts
- merges Collector history into existing Copper&Claw history
- skips exact duplicate imported observations
- uses the combined available history for trends and analysis
- newly watched items can immediately gain earlier history already collected by the Collector
- continues working from local history if the Collector is unavailable

If the Collector has never observed an item, Copper&Claw still begins with only the history available locally from the time tracking started.

The old standalone History panel has been removed from the watch-list UI to keep Details focused on useful analysis. Historical observations are still collected normally and continue to power trends, confidence, coverage, volatility, statistics, and sparklines.

### Trend Windows

- 15m
- 30m
- 1h
- 6h
- 24h
- 3d
- 7d
- 30d
- 90d

The Sell and Buy sparklines in Analysis use the currently selected **Trend Window**, so the graph and the surrounding market analysis refer to the same period.

---

## Copper&Claw Collector Integration

Copper&Claw can optionally use the standalone **Copper&Claw Collector** as its preferred market-data source.

When the Collector is running locally, Copper&Claw connects to:

```text
http://127.0.0.1:5177/
```

Current integration behavior:

- current watched-item prices are requested from the Collector first
- historical Collector data is imported for watched items
- existing Copper&Claw local history is preserved
- imported Collector history is merged with local history
- exact duplicate historical observations are skipped
- if the Collector is unavailable when Guild Wars 2 starts, history import is retried later automatically
- no Guild Wars 2 restart is required when the Collector becomes available later
- automatic refresh uses a 5-minute cadence while the Collector is available
- automatic refresh falls back to 60 seconds when the Collector is unavailable
- manual Refresh and Refresh All remain immediate
- if the Collector cannot provide current pricing, Copper&Claw falls back to the official Guild Wars 2 API

The Collector is not required for Copper&Claw to function. Without it, the addon continues using the Guild Wars 2 API and its own local history as before.

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

- Copper&Claw settings
- watched items
- Sell Targets
- Buy Targets
- local Trading Post price history

The item-search cache is not included because Copper&Claw can rebuild it automatically.

---

## First-Time Item Indexing

On first launch, Copper&Claw builds a searchable Trading Post item index using the Guild Wars 2 API.


<img width="543" height="56" alt="itemindex" src="https://github.com/user-attachments/assets/fe8e9588-9d5b-43ab-80d9-0b0cb555a305" />

While the index is building:

- search results may be incomplete
- variant labels may not appear yet
- item icons may not appear yet
- some items may temporarily be missing from autocomplete

If you see:

`Building item search index...`

allow it to finish before judging search behavior.

---

## Post-Update Item Index Notice

Some Copper&Claw updates may require the Trading Post item index to rebuild.

When this applies, Copper&Claw displays a one-time notice after the updated addon is loaded.
<img width="470" height="146" alt="updatenotification" src="https://github.com/user-attachments/assets/f6074c48-49f1-4e53-9b9c-3d53635ad518" />


The rebuild starts automatically and may take a couple minutes.

The notice only needs to be acknowledged once for each addon version.

---

## Persistence

Copper&Claw saves:

- watched items
- Favorites
- Sell Targets
- Buy Targets
- Sell / Buy target alert state
- settings
- selected Trend Window
- selected watch-list sort mode
- selected watch-list view density
- backup settings
- last restored backup information
- acknowledged update-notice version
- local Trading Post history
- imported Collector history after it is merged into the local history file
- cached item-search index

Aurene's Bite is added as a **first-run starter item**, but it can be removed normally and stays removed after restart.

---

## Installation

1. Download `Copper & Claw.dll`
2. Place it in your Guild Wars 2 Nexus addon folder
3. Launch Guild Wars 2
4. Confirm **Copper&Claw** is loaded in Nexus
5. Open the Copper&Claw options panel
6. Allow first-time item indexing to finish if needed

Copper&Claw creates its local settings and data files automatically.

The standalone Copper&Claw Collector is optional. If it is running locally, Copper&Claw automatically detects and uses it without requiring additional in-game configuration.

---

## Author

**spectre.9510**
