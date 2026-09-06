
<img width="75" height="75" alt="logo" src="https://github.com/user-attachments/assets/42abe8a1-7768-448f-a85a-d2bab4e7b667" />
<img width="75" height="75" alt="SELLBUYstats" src="https://github.com/user-attachments/assets/486adbda-3460-4ca9-a713-00dfa3567230" />
<img width="75" height="75" alt="Targethitboxlarge" src="https://github.com/user-attachments/assets/38b269cf-c077-450d-a564-d85a5d150cde" />
<img width="75" height="75" alt="targethitsmall" src="https://github.com/user-attachments/assets/5d5507b5-8693-45ed-b70a-63b8a88bd496" />
<img width="75" height="75" alt="markettrends new" src="https://github.com/user-attachments/assets/04835a47-f646-40a9-b5c7-f8c7cd88619c" />

<img width="75" height="75" alt="indexbuilding" src="https://github.com/user-attachments/assets/67b35d13-09af-4deb-9500-878b52da72b0" />



# Copper&Claw

Copper&Claw is a standalone **Guild Wars 2 Nexus addon** for tracking Trading Post prices, building local price history, and alerting you when watched items hit your target price.

## Status

**Version:** 0.1.1  
**Status:** Active pre-release / release-candidate build

---

## Screenshots

<img width="75" height="75" alt="Muliitemwatch" src="https://github.com/user-attachments/assets/d0616ef8-69db-49cb-a75d-2e8f8d1cc11f" />
<img width="75" height="75" alt="multiwatchitemsearchuncheckapi" src="https://github.com/user-attachments/assets/685f3ef5-37b4-487e-a2fe-441b372d2aa2" />
<img width="75" height="75" alt="targethitsmall" src="https://github.com/user-attachments/assets/5d5507b5-8693-45ed-b70a-63b8a88bd496" />

---

## Main Features

- Watch multiple Trading Post items
- Automatic price refresh every 60 seconds
- Manual per-item refresh
- Refresh All
- Current **lowest sell listing**
- Current **highest buy order**
- Per-item **Sell Target** using gold / silver / copper inputs
- Persistent watched items, targets, settings, and price history
- Cached searchable Trading Post item index
- Item search without requiring manual item IDs
- Variant-aware search for duplicate-name items
- Sell / Buy price-history sparklines
- Historical minimum / average / maximum values
- Dragon Bash-style **target reached** popup

---

## Market Analysis

Copper&Claw separates the two sides of the Trading Post market:

### Sell Listings
Shows:
- Direction
- % movement
- Recent average
- Price position
- Buyer-facing signal

### Buy Orders
Shows:
- Direction
- % movement
- Recent average
- Price position
- Seller-facing signal

### Confidence & Coverage
Copper&Claw also shows:
- Confidence
- Gap-aware coverage
- Sample count inside the selected trend window

This helps avoid misleading analysis when local history is still limited.

---

## Target Alerts

When a watched item’s lowest sell listing reaches or drops below your Sell Target, Copper&Claw shows a celebration popup.

<img width="75" height="75" alt="Targethitboxlarge" src="https://github.com/user-attachments/assets/38b269cf-c077-450d-a564-d85a5d150cde" />

Alert includes:
- **TARGET REACHED!**
- item name
- current sell price
- your target price
- confetti / celebration effects
- **Dismiss Party** button

Multiple alerts are queued and shown one at a time.

---

## Variant-Aware Search

Some Guild Wars 2 items share the same visible name but have different item IDs and separate Trading Post listings.

Before:

<img width="200" height="251" alt="preconfettifix" src="https://github.com/user-attachments/assets/779ad406-869c-40da-b44b-c354a54e129b" />

After:

<img width="200" height="251" alt="confettifix" src="https://github.com/user-attachments/assets/ea359ee5-82ba-4732-9ce5-aec38611f5c4" />

Copper&Claw adds variant labels such as:
- Power
- Precision
- Toughness
- Vitality
- Healing Power
- Concentration
- Expertise
- Condition Damage

This makes it easier to watch the correct Trading Post listing.

---

## Price History

Copper&Claw builds its own local Trading Post history.

<img width="75" height="75" alt="TPtrends" src="https://github.com/user-attachments/assets/7157a206-0656-4512-a5f6-2fa01192a7fa" />

Current behavior:
- preserves every successfully recorded observation
- does **not** automatically discard or compact older observations
- stores history locally for later trend analysis

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

## Persistence

Copper&Claw saves:
- watch list
- Sell Targets
- settings
- selected Trend Window
- local Trading Post history
- cached item-search index

These persist across game restarts.

---

## ⚠️ First-Time Item Indexing

On first launch, Copper&Claw builds a local Trading Post item-search index from the Guild Wars 2 API.

<img width="554" height="270" alt="indexbuilding" src="https://github.com/user-attachments/assets/67b35d13-09af-4deb-9500-878b52da72b0" />

While the index is building:
- search results may be incomplete
- variant labels may not appear yet
- some items may be temporarily missing from autocomplete

If you see:

`Building item search index...`

let it finish before judging search behavior.

---

## Current Notes

- Aurene’s Bite is used as a **first-run starter item**
- It can be removed normally
- Removed starter items stay removed after restart
- All historical analysis is based on **locally collected observations**
- New installs begin with **no prior historical market data**
- The current DLL is built as `Copper & Claw.dll`

---

## Installation

1. Download `Copper & Claw.dll`
2. Place it in your Guild Wars 2 Nexus addon folder
3. Launch Guild Wars 2
4. Open Nexus and confirm **Copper&Claw** is loaded
5. Open the Copper&Claw options panel
6. Allow first-time item indexing to complete if needed

Copper&Claw will create its settings, watch-list, history, and item-index files automatically.

---

## Author

**spectre9510**