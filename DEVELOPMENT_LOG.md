# Copper&Claw

<img width="75" height="75" alt="logo" src="https://github.com/user-attachments/assets/42abe8a1-7768-448f-a85a-d2bab4e7b667" />
<img width="75" height="75" alt="SELLBUYstats" src="https://github.com/user-attachments/assets/486adbda-3460-4ca9-a713-00dfa3567230" />
<img width="75" height="75" alt="Targethitboxlarge" src="https://github.com/user-attachments/assets/38b269cf-c077-450d-a564-d85a5d150cde" />
<img width="75" height="75" alt="targethitsmall" src="https://github.com/user-attachments/assets/5d5507b5-8693-45ed-b70a-63b8a88bd496" />
<img width="75" height="75" alt="markettrends new" src="https://github.com/user-attachments/assets/04835a47-f646-40a9-b5c7-f8c7cd88619c" />
<img width="75" height="75" alt="indexbuilding" src="https://github.com/user-attachments/assets/67b35d13-09af-4deb-9500-878b52da72b0" />

Copper&Claw is a standalone **Guild Wars 2 Nexus addon** for tracking Trading Post prices, building local price history, analyzing market movement, and alerting you when watched items reach a target price.

## Status

**Latest Release:** v0.1.1  
**Development:** Active

---

## Main Features

- Watch multiple Trading Post items
- Automatic price refresh every 60 seconds
- Manual per-item refresh and Refresh All
- Current **lowest sell listing**
- Current **highest buy order**
- Per-item **Sell Targets**
- Persistent watch list, targets, settings, and price history
- Search items without requiring manual item IDs
- Variant-aware search for duplicate-name items
- Separate **Sell Listings** and **Buy Orders** market analysis
- Gap-aware confidence and coverage
- Sell / Buy price-history sparklines
- Historical minimum / average / maximum prices
- Dragon Bash-style **Target Reached** alerts

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

### Confidence & Coverage

Copper&Claw also reports:

- Confidence
- Gap-aware selected-window coverage
- Sample count inside the selected trend window

This helps prevent limited or interrupted local history from being presented as mature market data.

<img width="75" height="75" alt="TPtrends" src="https://github.com/user-attachments/assets/7157a206-0656-4512-a5f6-2fa01192a7fa" />

---

## Target Alerts

Set a Sell Target for any watched item.

When the current lowest sell listing reaches or drops below that target, Copper&Claw displays a celebration alert.

<img width="75" height="75" alt="Targethitboxlarge" src="https://github.com/user-attachments/assets/38b269cf-c077-450d-a564-d85a5d150cde" />

Alerts include:

- **TARGET REACHED!**
- item name
- current sell price
- your target price
- confetti / celebration effects
- **Dismiss Party**

Multiple triggered alerts are queued and shown one at a time.

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

This makes it easier to select the exact Trading Post listing you intended to watch.

---

## Local Price History

Copper&Claw builds its own Trading Post history from observations collected while items are being watched.

Current behavior:

- preserves every successfully recorded observation
- does **not** automatically discard or compact older observations
- retains history across game restarts
- uses locally collected history for trends and analysis

New installations begin with **no historical market data from before tracking started**.

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

---

## First-Time Item Indexing

On first launch, Copper&Claw builds a searchable Trading Post item index using the Guild Wars 2 API.

<img width="554" height="270" alt="indexbuilding" src="https://github.com/user-attachments/assets/67b35d13-09af-4deb-9500-878b52da72b0" />

While the index is building:

- search results may be incomplete
- variant labels may not appear yet
- some items may temporarily be missing from autocomplete

If you see:

`Building item search index...`

allow it to finish before judging search behavior.

---

## Persistence

Copper&Claw saves:

- watched items
- Sell Targets
- settings
- selected Trend Window
- local Trading Post history
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

---

## Author

**spectre9510**