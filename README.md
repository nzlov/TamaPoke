# TamaPoke

English | [简体中文](README.zh-CN.md)

[![Flash in browser](https://img.shields.io/badge/flash-in%20browser-FF6B00?logo=googlechrome&logoColor=white)](https://nzlov.github.io/TamaPoke/)
[![MakerWorld](https://img.shields.io/badge/MakerWorld-3D%20case-00AE42?logo=bambulab&logoColor=white)](https://makerworld.com/es/models/2937822-tamapoke-a-pokemon-pokeball-tamagotchi)
![Board](https://img.shields.io/badge/board-ESP32--S3%20round%20AMOLED-E7352C?logo=espressif&logoColor=white)
![Firmware](https://img.shields.io/badge/firmware-v3.6-8A2BE2)
![Code](https://img.shields.io/badge/code-MIT-blue)
![Languages](https://img.shields.io/badge/languages-7-FFCB05)
[![Stars](https://img.shields.io/github/stars/nzlov/TamaPoke?style=flat&logo=github&color=yellow)](https://github.com/nzlov/TamaPoke/stargazers)

A gen-1-Pokémon-inspired tamagotchi for the
**Waveshare ESP32-S3-Touch-AMOLED-1.75** (round 466×466 AMOLED, CO5300 driver
over QSPI, CST9217 touch over I2C). Raise any installed species, evolve it, train it
and complete them all (shinies included).

> ### 🙏 Project lineage
>
> - **Current project:** [nzlov/TamaPoke](https://github.com/nzlov/TamaPoke)
> - **Upstream:** [DylanPDao/TamaPoke](https://github.com/DylanPDao/TamaPoke) by Dylan Dao
> - **Original upstream:** [socquique/TamaPoke](https://github.com/socquique/TamaPoke) by Quique Tortosa
>
> Quique created the original firmware, sprite pipeline, six-language UI and web
> installer. Dylan maintained and substantially expanded the next upstream
> branch. This repository continues from Dylan's upstream with its own runtime
> packs, Chinese UI and gameplay changes. If this project helps you, please also
> visit and support both upstream projects.
>
> The original code and derivative changes are available under the MIT License.

> **Personal, non-commercial fan project.** Code is MIT; the sprites are from
> PMD SpriteCollab (CC BY-NC, Pokémon © Nintendo/Game Freak), and the 3D case is
> CC BY-NC-SA. See **[License](#license)** and **Credits**.

🔴 **3D-printed Pokéball case + print profiles → [on MakerWorld](https://makerworld.com/es/models/2937822-tamapoke-a-pokemon-pokeball-tamagotchi)** · flash it in your browser → **[web installer](https://nzlov.github.io/TamaPoke/)**

## Latest release and package split

The supplied regional packs cover Kanto, Johto, Hoenn and Sinnoh and their
alternate-color forms, with a complete Simplified Chinese UI, names and
descriptions. The recommended installation path is the **[web
installer](https://nzlov.github.io/TamaPoke/)**: flash the firmware,
then deploy the selected languages and regions to the microSD. Arduino IDE is
not required.

The root installer is the latest published **stable release**. The separate
**[latest build](https://nzlov.github.io/TamaPoke/latest/)** follows `main` and
is intended for testing unreleased changes. Both pages show their channel,
commit and build date; use the stable release unless you specifically need to
test current development. Existing `/web/` links continue to redirect to the
stable installer.

The available species count is **determined by the region packs installed on
the device**; the Pokédex, eggs, battles and region selectors expose only that
available content.

Language, battle, move, item, regional and question-bank content now lives in independently deployable
runtime packages. Install only the languages and regions you need, and update
content without recompiling the firmware:

| Package | Contents | Required |
|---|---|---|
| UI (`.tui`) | UI strings, layout metrics and fonts; Simplified Chinese is `ui-zh-CN.tui` | At least one |
| Battle (`.tbattle`) | Shared abilities, type catalogue and effectiveness chart | Yes |
| Moves (`.tmove`) | Stable move IDs, mechanics, G-Max Move definitions, names and descriptions | Yes |
| Items (`.titem`) | Stable item keys, effects, names, descriptions and optional icons | Yes |
| Region (`.tregion`) | Species data, learnsets, breeding references, Mega/Gigantamax forms and G-Max Move references, names and descriptions, animated/alternate-color sprites, thumbnails, trainers, gyms and badges | At least one |
| Questions (`.tquiz`) | Locale-indexed multiple-choice questions; records are read directly by random index | No; arithmetic is the fallback only when enabled |

The installer reads `web/packs/index.json` and checks each selection's
dependencies against both the packages already on the device and those selected
for this deployment. If anything is missing, it names the dependency and lets
you cancel or explicitly force the partial deployment. Regions absent from the
card remain locked and out of the egg pool. At boot the firmware validates each
package's ABI and CRC. Missing required packages open the built-in USB recovery
screen instead of an incomplete Pokédex, and an interrupted upload cannot
replace a working package.

After connecting, the installer shows each Web package's content-derived version,
lists the version installed on the microSD, and marks whether they match. Unchanged
package content keeps the same version across repeated Pages builds. The installer
can also delete an individual package and format the microSD. Delete and format
operations require confirmation; restart the device after changing the card.

> **Upgrade note:** the first migration to runtime packages clears saves made by
> older firmware. For later same-schema updates, leave **Erase device** unchecked
> to preserve the current save; flashing does not delete packages on the microSD.
> ABI 7 separates shared battle rules, moves and items from regional species
> content and adds the Breeding Centre's Egg Group, offspring-family and Egg Move
> references to region packs. Redeploy `battle-core`, `moves-core`, `items-core`,
> every region pack and the desired UI packs; older package ABIs are rejected.
> Region packages are large (a 40 MB package normally takes 10–15 minutes over
> USB serial). Restart the device after deployment.

For package generation and validation, see [Generate and load the data packs
yourself](#generate-and-load-the-data-packs-yourself) and [Runtime data
packs](#runtime-data-packs).

## Screens

All shots are straight off the 466x466 round panel, rendered headlessly by the
emulator (`tools/emu/tamapoke-emu --shot <name>`), so they are exactly what the
hardware draws.

### Starting out

| After language: pick a region | ...then its starter | Johto's three |
|---|---|---|
| <img src="docs/screens/region.png" width="240"> | <img src="docs/screens/starter.png" width="240"> | <img src="docs/screens/starterj.png" width="240"> |

A new game first asks for the interface language, then which region you are
playing, and finally which creature you want. The region choice sets both: you
pick from that region's three starters, and it becomes where your eggs come from
afterwards (changeable later on the egg's region pill). Existing saves never see
this flow. Language names come from the installed UI packs; the first installed
pack drives the initial page, with English used when that choice cannot be loaded.

### Raising one

| Your creature | Its egg | Moves it knows |
|---|---|---|
| <img src="docs/screens/main.png" width="240"> | <img src="docs/screens/egg.png" width="240"> | <img src="docs/screens/moves.png" width="240"> |

The egg carries a **region pill** for Kanto, Johto, Hoenn, Sinnoh or all installed
regions. Switching keeps the rarity it was granted and remembers each region's
answer.

### Battling

| The fight | Choosing a team | Winning |
|---|---|---|
| <img src="docs/screens/btlmenu.png" width="240"> | <img src="docs/screens/pick.png" width="240"> | <img src="docs/screens/win.png" width="240"> |

Turn- and move-based, with the real type chart, ailments and STAB. Real Game Boy
battle music plays throughout. Each exchange automatically presents its turn
number, then plays both actions in speed order with move, impact, HP, status,
field, end-of-turn, faint and replacement beats before accepting the next input.
Contact moves carry the attacker into striking range, while non-contact attacks
travel across the field with distinct Fire, Water, Electric and other type effects.
When questions are enabled, every local move opens one before it acts: damaging
moves scale their final damage by the answer stage, while status and healing
moves take full effect after a correct answer. With questions disabled, moves
run directly at 100%. In LAN battles each player uses their own question settings,
the percentage travels with that move, and the host remains authoritative.

Each creature tracks proficiency separately for each move with base power. Moves
start at level 0 and reach level *n* at `3^n` total progress; an actually launched
move gains 1 progress, or 6 total when it directly knocks out its target. Misses
and immunities still count, while a turn stopped before launch does not. Move
power gains `ceil(level / 3)`. Status and other zero-power moves have no level.

### Four regions

| Pick a ladder | Johto's gyms | LAN battle |
|---|---|---|
| <img src="docs/screens/gympick.png" width="240"> | <img src="docs/screens/gymsj.png" width="240"> | <img src="docs/screens/lanready.png" width="240"> |

Kanto, Johto, Hoenn and Sinnoh each have eight leaders, an Elite 4 and a
champion, on easy and hard. The teams are the games' own, checked against the
pokecrystal, pokeemerald and pokeplatinum disassemblies -- **0 differences
across all 39 trainers**, re-checkable with `tools/verify_rosters.py`.

Sinnoh follows **Platinum**, where Fantina is the *third* gym rather than
Diamond/Pearl's fifth; the level ramp only runs 14/22/26/32/37/41/44/50 that
way. Same reasoning that makes Hoenn Emerald throughout.

### Collecting

| Pick a region | Kanto | Johto |
|---|---|---|
| <img src="docs/screens/dexpick.png" width="240"> | <img src="docs/screens/gallery.png" width="240"> | <img src="docs/screens/gallery2.png" width="240"> |

| Trainer card | Johto badges | The box |
|---|---|---|
| <img src="docs/screens/player.png" width="240"> | <img src="docs/screens/player2.png" width="240"> | <img src="docs/screens/box.png" width="240"> |

## Status

Running on hardware. Implemented: installed species + shinies animated from
regional packs; six persistent cultivation slots and a 24-slot Box; the full
life cycle
(starter or egg → care → evolution → farewell/release/runaway); bred-Pokédex with
gallery; turn-based trainer, wild and LAN battles; wild capture and shared item
inventory; battle stats (IVs + training); retention hooks (streak / bond /
medals / name); biome + real-time backgrounds; three training minigames;
care/battle questions backed by indexed banks or configurable arithmetic;
animated bath; RTC offline progression; battery (AXP2101), PWR key and
anti-burn-in dimming; **sound (ES8311)**; **7 UI languages (English default)**;
**starter choice on first run**; and a one-click **web installer**.

## Game manual (the actual numbers)

A quick reference to how the game really works (values straight from the code).

### Time & leveling
- **1 real minute = 1 in-game minute.** Your Pokémon gains **+1 level every hour**
  of real time. Leveling is purely time-based — caring well doesn't speed it up,
  but neglect *delays evolution*.
- **Level caps at 100**, reached 4 days 3 hours after level 1. Once a final form
  has actually been cultivated by this player for 3 days, the menu's Release
  row becomes Farewell. A wild creature's pre-capture age does not count.
- It keeps **aging while powered off** (the RTC runs), catching up for gaps of up to
  **2 weeks**. Larger RTC discontinuities are rebased without draining care stats.

### The four stats (0–100)
Needs: **FOOD**, **JOY**, **ENE** (energy), **HYG** (hygiene). Start 80 / 80 / 80 / 100.
While **awake**, per minute:

| Stat | Drain/min | Notes |
|---|---|---|
| FOOD | −2 | |
| ENE | −1 | −1 extra if overweight (weight > 50 → sluggish) |
| HYG | −1 | **−4 more per poop** on screen (max 3 poops) |
| JOY | −1 | **−2 extra** if FOOD < 30, **−2 extra** if HYG < 30 |

- ~**15 %/min** chance to poop (only if FOOD > 40). Poops tank hygiene fast.
- **Care slip-up** = letting any stat hit **≤ 10** (60-min cooldown so it counts once).
  Each slip-up **delays evolution by 1 level** and cools the bond by 1.

### Actions
- 🍎 **Berry** (3 flavors): +25 FOOD. Each species has a **hidden favorite flavor**
  → +35 FOOD, +10 JOY, ♥, bond, and it gets revealed.
- 🍬 **Candy:** +10 FOOD, +12 JOY, but **+12 weight** (fattening).
- ⚽ **Ball / defence training:** **+5 JOY, plus 2 per rally** (max +35), trains
  **DEFENCE** (2 rallies = one score step), −ENE and burns weight. Leaving
  early keeps the actual score and still proceeds to its question.
- 🎯 **Reaction test:** a target appears, tap it before it shrinks away. Trains
  **SPEED**; the window tightens as you go.
- 🥊 **Training bag:** trains **STRENGTH** (~4 hits = one score step), tires it.
- 🫧 **Bath:** clears poops, HYG → 100.

Each active trainer awards a percentage of that stat's IV-based training ceiling:
`floor(ceiling × 30%) × score progress × answer percentage`, rounded to the nearest
point and clipped at the ceiling. A full session with a 100% answer therefore adds
at most 30% of the ceiling.

Choosing feed, bath or petting opens the modal question first, and the matching
success animation starts only after a correct result settles. Ball, reaction and
training-bag sessions ask their question after the session. Multiple-choice and
arithmetic questions can be enabled independently, and only enabled types appear;
both are disabled by default, so the interaction runs directly at 100% with no modal
until a question type is enabled. Saved question settings remain in effect. A
locale-matching multiple-choice question is selected by a direct random index from
installed `.tquiz` packs. If none matches and arithmetic is enabled, the firmware
creates an exact-rational arithmetic question from the global operator, operand, digit,
decimal, fraction and parenthesis rules. Arithmetic answers are entered on the
small on-screen numeric keypad, and equivalent decimal/fraction forms are accepted.
Long question text can be scrolled.

The answer timer has six equal stages. A correct answer applies **100%, 83%, 67%,
50%, 33% or 17%** of the positive result according to the current stage; for
example, answering at 15 seconds with a 30-second limit applies 50%. A wrong or
timed-out answer applies 0%: feed, bath and petting produce no benefit or success animation. Training still
keeps the real session record, energy/fullness cost and weight burn, but grants no
positive stat, joy or bond gain; a battle move fails entirely. Care, training and
battle share one global type, time-limit and generation configuration. LAN peers
may use different switches: a side with questions disabled submits 100% immediately,
the answering side keeps the link alive, and the host settles once both actions arrive.
- 👆 **Pet it:** after a correct answer, up to +5 JOY + bond according to the stage.
- 🌙 **Sleep:** rest — ENE **+6/min**, needs drain ~**4× slower** with floors
  (FOOD 30 / JOY 35 / HYG 45). No poops, no slip-ups, can't run away while asleep.
  **Screen off + between midnight and 06:00 puts it to sleep**, and it stays
  asleep until you turn the screen back on -- it wakes when *you* do, not at a
  fixed hour. Re-checked every minute, so a device put down at 23:00 nods off
  when midnight comes. The
  evening is deliberately outside the window -- a six-hour night is the only part
  of the day you are not expected to be around. The light
  button beats both: a creature you sent to bed stays there.

### Eggs & who you get
- **First ever pet:** choose one of the three starters from the selected installed
  region.
- Hatch the egg: tap it **3×** (or wait — it hatches on its own).
- A safety egg rolls a **rarity tier** within the installed region pool:

| Tier | Base chance |
|---|---|
| ✨ Legendary | ~3 %\* |
| 🔵 Rare | ~27 % |
| ⚪ Common | the rest |

  \* Legendaries only start appearing once you've **registered ≥ 25** Pokémon.
- A daily **streak** and high **bond** push rare/legendary odds higher.
- Within a tier it favors species whose **evolution line you haven't finished** (so
  every species in installed region packs is completable).
- Every hatch rolls unique **IVs** (see below) — no two are identical.
- Endings normally create no egg. One safety egg is made only when all six
  cultivation slots and all 24 Box slots are empty; if the team is empty but
  the Box is not, its first creature is withdrawn instead.

### Breeding centre

- Swipe down and open **Breeding**. Move two living, hatched parents from the
  cultivation team or Box into its two dedicated slots. Opposite-sex parents
  share an Egg Group; Ditto follows the original exception.
- **Start** schedules one birth after a random **1–2 real hours**. RTC/offline
  catch-up continues the timer while the device is off. Both parents are frozen
  in the centre and cannot be removed or replaced until the offspring is ready.
- The offspring is the base form of the female/non-Ditto parent's family, with
  the original Nidoran, Volbeat/Illumise and Manaphy/Phione exceptions. It is
  collected directly as a level-one creature rather than as another waiting egg.
- The centre uses Gen-IX inheritance: nature and gender are
  rolled anew; three distinct positions from TamaPoke's four IVs come from either
  parent and the fourth is rolled at 8–31; compatible Egg Moves can come from
  either parent's active or reserve list. A normal ability slot passes 80% of the
  time and a Hidden Ability 60% of the time.
- The shiny roll uses the exact wild base rate and shared farewell bonus. Each
  shiny parent adds **5 percentage points**, so two add 10 points. A shiny bred
  offspring follows the wild rule that floors every IV at 20.
- Every offspring has a **5%** base chance to carry the persistent Gigantamax
  Factor, plus **10 percentage points per parent** that carries it (5% / 15% /
  25%). The factor stays dormant on an unsupported form and becomes effective
  if that individual later evolves into an eligible Gigantamax species.
- The centre shows the male parent on the left and female parent on the right,
  with the offspring area centred below. Tap an occupied parent for **Details /
  Remove**; tap an empty side to choose from the six cultivation slots or four
  Box pages. A ready offspring can also be tapped for its profile, stats and moves.
- **Take** stores the child in the first free cultivation slot, then the first
  free Box slot. The parents remain in place, so Start can be used again. If both
  stores are full when removing a parent, choose any cultivation or Box member:
  it swaps into the centre and the outgoing parent takes its slot, so nothing is
  silently deleted.

### Evolution

**Cross-generation and branching evolutions come from region packs.** Targets
such as CROBAT, STEELIX, BLISSEY, KINGDRA, SCIZOR and PORYGON2 stay linked even
though they live outside their source species' region. Branching lines in the
installed catalogue are represented, including Gloom,
Poliwhirl, Slowpoke, Eevee, Tyrogue, Wurmple, Kirlia, Nincada, Snorunt,
Clamperl and Burmy. Egg pools contain their base forms, and these targets are
reached through evolution.

Evolution targets are stable species IDs in `pokemon_data.json`. Adding a
cross-generation target is therefore an explicit catalogue edit reviewed with
the same ID-integrity checks as learnsets and forms.

- Triggers when **level ≥ its evolution level** (16 for most base forms; ~30 for
  stone-style, ~40 for trade-style) **and every stat ≥ 40** at that moment.
- **Never automatic** — a button appears and **you tap to witness it** (with a
  flicker between the old and new form). Each **slip-up delays it by 1 level**.
- You can **decline** ("keep form"); it re-offers at the next level.
- Branching species prefer an installed evolution you're still missing.

### Six cultivation slots and the Box
- The **six party slots are all cultivation slots**. Every occupied slot keeps its
  complete care, growth, move, IV, training and lifecycle state; all six advance
  together even though only one is shown on the main screen.
- Swipe horizontally to change the displayed creature. The indicators above
  the creature name show only occupied cultivation slots; tapping them opens
  the Box. Swipe down for the **bag / battle centre / badges / breeding / task
  centre** navigation page.
- The name menu can make the displayed creature the single **Lead**. That
  choice persists independently of which creature is displayed and sends the
  Lead out first whenever it is included in a battle squad.
- The **Box holds 24 creatures** across four pages of six. Box state is frozen;
  exchanging a creature with a cultivation slot resumes it with no state reset.
- The Box is the only cultivation-management screen. Tap an empty cell to store
  a chosen cultivation member. Tapping an occupied cell opens **View / Withdraw /
  Release**: View pages through its profile, stats and moves; Withdraw uses the
  first free cultivation slot or asks which member to exchange when all six are
  full; Release always asks for confirmation.
- Box cells and embedded cultivation-member cards use the same 2× thumbnails,
  vertically centred in each row.
- A **runaway leaves the roster permanently**.
- A wild capture uses a free cultivation slot first, then a free Box slot. If both
  are full, the player explicitly chooses a replacement or lets it go; nothing
  is overwritten silently.

### Daily task centre

- At device-clock midnight, all three tasks are replaced with three distinct
  non-legendary species drawn from the installed regions. Targets may be daytime
  or nighttime species, but legendary species never enter the task pool. Each
  slot draws from registered Pokédex species with **70%** probability and from
  unregistered species with **30%** probability, falling back only when its
  selected pool is empty.
- Before an ordinary wild species is rolled, a region with one or more unfinished
  matching tasks gets an exact **30%** task branch. That branch chooses uniformly
  among unfinished task species from the current region that are available in the
  current day/night period; completed tasks no longer boost encounters.
- Each task asks for one matching living, hatched creature from the six cultivation
  slots or the Box. Tapping the task card directly starts the submission picker;
  incomplete cards carry no separate Submit caption. When more than one individual
  matches, tap the exact creature, then choose **View** or **Submit**. Submitting permanently removes only that
  individual, and the final remaining cultivation member cannot be submitted.
- An unregistered target appears as a black silhouette labelled **Guess who I am**;
  its species name and region remain hidden until that Pokédex entry is registered.
- Capturing an unfinished task target offers immediate submission from the battle
  settlement before it occupies a cultivation or Box slot. Declining uses the
  normal storage flow; a normal-tier submission explicitly warns that the hard
  reward is better before confirmation. A catch above level 100 keeps its original
  level when submitted directly; accepting it instead lowers it to level 100
  before storage.
- Reward difficulty is inferred from the submitted creature. A creature at least
  **10 levels above the pre-submit average level of the living cultivation team**
  earns the hard reward; every other valid submission earns the normal reward.
  Normal and hard use the wild victory item counts (one or two weighted items,
  plus the same independent 30% bonus item). Hard draws are limited to items of
  rarity two or higher. Move stones in either tier carry a random valid move from
  the complete move catalogue. Task rewards do not grant battle training points.
- Wild opponents range from level 1 through at most **10 levels above the
  party's highest level on normal difficulty**, or **20 levels above it on hard**.
  Both limits stop at level 120.
- Bag items have distinct icons everywhere they appear. Poké, Great and Ultra Balls
  use 1×, 1.5× and 2× catch modifiers; the four-star Master Ball is a weight-1 wild
  victory drop and catches any valid wild target without fail. Run
  `python3 tools/fetch_item_icons.py` before generating packs to use PokeAPI's
  original 24×24/30×30 item sprites; missing artwork keeps the built-in icon.
  The capture throw uses the image of the ball actually consumed.
- The bag list scrolls vertically. Tapping a stack opens View, Use and Discard:
  View shows its description; field-use items open the six cultivation-slot picker
  and consume one only after a valid target is chosen. Discard asks for a quantity
  when a stack has more than one item, then always asks for confirmation.
- After a wild victory, a settlement page lists every item and training-stat gain.
  A normal-difficulty victory or capture guarantees one weighted item reward;
  hard difficulty guarantees two. Both then make one independent **30%** roll
  for one bonus item. Each weighted draw excludes every earlier result, so all
  ordinary rewards are different. Defeating an opponent assigned a special
  mechanic still grants its corresponding item on top, for maxima of three
  items on normal and four on hard. When rewards exceed five visible rows, the
  middle list scrolls vertically while the title and Back action stay fixed.
  A successful capture adds the caught creature to that same page and stores it
  automatically. A full collection continues to the replace-or-release picker.
- Selecting a capture ball arms the board's QMI8658 motion sensor for three seconds.
  Keep hold of the device, flick it forward by roughly 60 degrees, and hold the
  final pose briefly; returning immediately to the start is rejected as a shake.
  Tapping cancels, and a timeout returns to the bag without consuming the ball.
  If the IMU is unavailable, selecting the ball keeps the original touch-only
  throw as a fallback.
- Capture items hide the battle UI while the wild creature moves to centre for
  a non-blocking throw and three-shake sequence. Success finishes before the
  settlement page. After breaking free, the creature enters a non-stacking Angry
  state for the rest of the battle: its angry animation keeps playing, its five
  non-HP battle stats rise by 5%, and its escape chance gains 5 percentage points.
  The base escape chance is 10% at 40% HP, rises linearly to 20% at 20% HP,
  then falls linearly to 10% at 10% HP and remains there below 10% HP. HP is
  unchanged; it returns to its battle position before UI resumes. A wild creature
  that chooses to flee uses its action to do so instead of also attacking.
- A failed escape consumes the player's turn and lets the opponent act normally;
  battle damage then determines whether the active creature faints.
- Gym and linked battles select from exactly the six cultivation slots.

### Three ways a creature leaves
- 💛 **Farewell** — after a final form has actually been cultivated by this
  player for **3 days**, the menu's Release row becomes Farewell. It adds
  **1 percentage point** to the wild shiny odds, or **2 points**
  when the creature is level 100.
- 💔 **Run-away** — if you let **all four stats sit at 0 for a full hour**. A single
  act of care cancels it. It subtracts **2 points** from the shared wild rare
  bonus, never below zero.
- 👋 **Release** — the menu action before Farewell is earned; neutral.

The shared bonus is clamped to **0–15 percentage points**. After one leaves, the
next cultivation member is shown, then the first Box member; only a completely
empty roster receives a safety egg.

### Bonds, streaks, medals, Pokédex
- **Streak** (player-wide, survives across pets): first care each real day; milestones
  at **3 / 7 / 30 / 100** days; skipping a day breaks it.
- **Bond** (per pet, resets on hatch): grows with affection (**cap +20/day**), cools on
  neglect. In battle it scales all six player stats by `70% + Bond / 2`, from
  70% at 0 through 100% at 60 to 120% at 100. Streak and bond still improve a
  safety egg's rarity and IVs, but do not enter the wild shiny roll.
- **8 medals** (Lv10/25/50, favorite berry found, 7-day streak, max bond, final form,
  "fit" = weight 0 & no slip-ups), per-pet + a global counter.
- **Pokédex:** raising a species registers it; completion covers every species
  and alternate-color form supplied by the installed region packs. Browse **one installed
  region at a time**, swiping horizontally to page within it.
- **Region:** the pill under a waiting egg picks which generation it comes from —
  **Kanto / Johto / Hoenn / Sinnoh / All**. A first egg gives that region's starter,
  and installed region packs define the available choices and egg pool.

### Battle stats & IVs
Every creature has four **IVs** — ATK / DEF / SPD / VIT:

```
stat = base + level + (IV × level)/100 + training
```

The `(IV × level)/100` term is the **real formula from Gen III onward**. 31 remains
the traditional maximum for ordinary rolls, but is not a system cap. IVs also
cap training.

| | Effect |
|---|---|
| Innate bonus | IV 31 contributes **+22** at level 73; higher IV keeps scaling |
| Training ceiling | `70 + (30 × IV)/31` → **77** at IV 8, **100** at IV 31 |
| Total spread | ~**40 points**, about **15 %** between a great and a poor individual |

- Hatch rolls are **8–31**; streak and bond may still bias a safety egg upward.
- Each ordinary wild IV is rolled independently at the current region/difficulty
  baseline **±3**.
- Wild **shiny variants** floor every IV at 20 without imposing a 31 cap; an
  already higher IV is preserved.
- Each gym can reward a given creature once. It picks any of the four IVs
  uniformly and adds **+1**, even when that IV is already 31 or higher. The
  player-wide badge and that creature's IV reward are committed in one roster
  snapshot, so a restart cannot restore only one half of the victory.
- IVs are shown on the Battle page of the stat card; 31 and above are highlighted.

Training: **STRENGTH** ← the bag, **SPEED** ← the reaction test, **DEFENSE** ← the
ball game. Care state selects the training decay rate. Sleeping is maintained
time; while awake, FOOD, JOY, ENE and HYG must all be at least 40. Each fixed
60-minute cycle counts maintained and low-state minutes, settles once, and then
resets both counts.

Every hatch also rolls one of the 25 **natures**. The 20 non-neutral natures apply
the canonical +10%/-10% modifier to their final combat stats; HP is unaffected.
The five otherwise-neutral natures instead modify the training contribution and
its base decay:

| Nature | Training contribution | Base loss for affected training |
|---|---|---|
| Hardy | ATK +10% | ATK: 3% of its cap |
| Docile | DEF +10% | DEF: 3% of its cap |
| Serious | SPEED +10% | SPEED: 3% of its cap |
| Bashful | DEF +10%, ATK -10% | DEF: 3%; ATK: 7% (of each cap) |
| Quirky | ATK +10%, DEF -10% | ATK: 3%; DEF: 7% (of each cap) |

At each 60-minute settlement, a maintained majority makes every channel lose half
its nature-based decay; a low-state majority applies double decay; a 30:30 tie
applies base decay. Training with the standard 5% base rate therefore uses 2.5%,
10% or 5% of its IV-based cap respectively. Every loss uses the cap and rounds
up, both live and offline.
ATK training is also the special-attack training contribution; DEF training is
also the special-defence contribution. There are no separate special training
values.

Each hatch also uses its species' canonical gender ratio. Male creatures receive
+10% final Attack and -10% final Special Attack; female creatures receive the
reverse. Genderless species are neutral. The modifier is applied after nature,
and the chosen gender persists in the party and box. The UI shows a compact gender
icon, and region packs can provide female normal and shiny sprite variants; when
a variant is absent, the base sprite is used.

Creatures learn their natural level-up moves as they grow and use attributed
**Move Stones** for other compatible moves. In wild and trainer battles, each
participating creature can also observe one compatible opposing move per battle;
the chance scales from 0% to 30% with bond, and a successful observation can be
used immediately before being added to its learned moves after battle.

**Gym wins train too**: each gym raises one random IV for the current creature.
Changing difficulty or rematching cannot claim it twice for that creature, but
a different creature has its own claim map.

Wild opponents have a **5% chance on normal difficulty and 20% on hard** to use
one available special mechanic: a Z-Move, Dynamax, or Mega Evolution.

- A Z-Crystal works for any species that has a damaging move; the move's type
  determines the generic Z-Move.
- Dynamax works for eligible species. An eligible Gigantamax species uses its
  Gigantamax state when that individual
  has the persistent Gigantamax Factor. Eligible wild individuals have an
  independent **5%** chance to carry it, and Max Soup can grant it later. The
  profile marks the factor with a **G-MAX** badge. During Gigantamax, a damaging
  move of the species' signature type is converted transiently into its named
  G-Max Move; it is never added to the learned move slots. Urshifu selects
  G-Max One Blow from a Dark source move and G-Max Rapid Flow from a Water source
  move. Other damaging types remain generic Max Moves and status moves become
  Max Guard, so a species without a matching learned attack cannot use its
  signature move. The battle menu uses localized transformed move names and
  marks generic and signature transformations with separate **MAX** and
  **G-MAX** tags. Gigantamax uses its distinct form sprite in battle.
- Mega Evolution is limited to species with an official Mega form. The standard,
  X, Y and Z stones are distinct and select that exact branch. The untransformed
  Pokemon has one shared normal/shiny appearance; the branch appears only after
  Mega Evolution. Every supported Mega branch uses its distinct form sprite.

All 33 signature G-Max Move mappings apply their battle effects, including
residual damage, status, trapping, screens, hazards, Gravity, critical-hit stages,
healing, ability bypass and Protect bypass. G-Max Gold Rush also queues one extra
weighted item for each successful hit and awards it only after a wild victory.
G-Max Replenish restores the most recently consumed item in the current battle,
including a mechanic item, and clears that restore record to prevent duplication.
G-Max Depletion lowers all five battle stats by another 10% per successful hit,
to a 50% floor, until the battle ends; switching does not clear this reduction.

### Abilities

All 1,025 current species carry their canonical normal slot(s) and hidden slot
in the regional packs. The ability belongs to the individual: eggs and trainer
Pokemon receive one available normal slot uniformly, while a hard wild encounter
has an exact **5%** chance to use its hidden slot when that species has one.
Normal wild encounters use a normal slot. Existing saves migrate to
a deterministic normal slot, and the chosen slot survives evolution, cultivation,
Box storage, backup/restore, capture, and LAN transfer. Mega Evolution replaces
the active ability when the official form data publishes one.

The Battle page of the stat card shows the localized ability name and description.
The resolver currently applies **214** abilities through shared stat, damage,
accuracy, priority, status, hit, knockout, switch-out, and end-of-round hooks.
This includes weather- and terrain-dependent stats, ability-driven move types,
stat-change reactions, indirect-damage protection, and single-battle aura and
ruin effects in addition to immunities and absorptions. Move records now carry
contact, sound, punch, bite, pulse, ballistic, powder, dance, slicing, wind, and
reflectable tags rather than inferring them from names.

Accuracy and evasion use the original -6..+6 stage table. Reflect, Light Screen,
and Aurora Veil last five completed rounds; Spikes, Toxic Spikes, Stealth Rock,
and Sticky Web persist on one side until Rapid Spin or Defog clears them. Entry
hazards, entry abilities, trapping, manual switching, forced switching, pivot
moves, Wimp Out, and Emergency Exit all use one switch lifecycle, including LAN
state synchronization.

Species-exclusive battle forms are real combat state for Castform, Cherrim,
Darmanitan, Aegislash, Wishiwashi, Minior, Mimikyu, Cramorant, Eiscue, Morpeko,
and Palafin. Their weather, HP, move, hit, end-of-round, and re-entry triggers
change types or battle stats as appropriate. King's Shield and Aura Wheel are
append-only catalogue entries so saved move IDs remain stable.

### Day and night encounters

Wild encounter pools follow the device clock. Day runs from **06:00 through
18:59** and night from **19:00 through 05:59**, using the same boundary as the
scene lighting. Each species carries an explicit `day`, `night`, or `both`
value in `pokemon_data.json`; that value is packed with the species into its
region package. The initial catalogue marks 118 clearly diurnal species and
134 clearly nocturnal species by their original-region ecology, while the
remaining 773 stay available in both periods. Rarity weights and the post-league
legendary gate still apply inside the selected period's pool.

### Battle weather and terrain

Trainer and LAN battles start with a clear field. A wild battle instead rolls
the foe biome's persistent environment: **50% clear, 30% primary weather, 10%
secondary weather, and 10% thunderstorm**. Cave and snow biomes assign 20% to
secondary weather.

| Biome | Primary | Secondary |
|---|---|---|
| Meadow | Harsh sun | Rain |
| Beach / Forest | Rain | Harsh sun |
| Cave / Volcano | Harsh sun | Sandstorm |
| Mountain | Sandstorm | Snow |
| Snow | Snow | Harsh sun |

A thunderstorm combines rain with Electric Terrain. This environmental weather
and terrain does not count down. **Sunny Day, Rain Dance, Sandstorm, Snowscape,
Electric Terrain, Grassy Terrain, Misty Terrain, and Psychic Terrain** cover the
matching layer for five turns, then reveal the wild baseline again. Damaging Max
moves set the corresponding Fire/Water/Rock/Ice weather or
Electric/Grass/Fairy/Psychic terrain.

- Harsh sun boosts Fire by 50%, halves Water, prevents freezing, and lets Solar
  Beam fire immediately. Rain boosts Water, halves Fire, and makes Thunder hit.
- Sandstorm chips non-Rock/Ground/Steel creatures by 1/16 and raises Rock
  special defence by 50%. Snow raises Ice physical defence by 50% and makes
  Blizzard hit.
- Electric, Grassy, and Psychic Terrain boost a grounded attack of their type by
  30%. Electric prevents sleep; Grassy heals grounded creatures by 1/16 and
  halves Earthquake against them; Psychic blocks positive-priority attacks
  against them.
- Misty Terrain halves Dragon damage against grounded targets and prevents their
  status conditions and confusion. Flying types and creatures with Levitate
  count as airborne.

The active weather and terrain are shown as localized HUD pills and animated
overlays. In LAN play the host sends absolute field state with every result, so a
dropped packet cannot leave the guest on a different field.

### Wild shiny encounters

Each wild encounter makes one shiny roll. Its base chance is exactly **1/4096**;
each farewell bonus point adds **1 percentage point**, up to a +15-point bonus
(about **15.0244%** total). A shiny uses the alternate sprite and persistent
gold/white particles, and floors every IV at 20 without clamping values above
31. Lists use one `*` marker. The main screen keeps its normal mood text, and the
single state persists through cultivation, Box storage, saves, battle and LAN
transfer. Old saves merge either former color or sparkle flag into this state.

### Choosing your egg's region

The species is decided when the egg **appears**, not when it cracks, so changing
region moves the egg you are holding. Two rules stop that being a re-roll button:

| | Rule |
|---|---|
| Rarity | the tier the egg was granted is **kept** — only which species of that tier changes, so flipping can never fish for a legendary |
| Memory | each region's answer is **remembered** for the current egg, so switching back shows the same creature |

The region is first chosen at the **very start of a new game**, on the screen
before the starter -- so the creature you begin with and the eggs that follow
come from the same place. Everything below is about changing it afterwards.

A region is decided by the **base** species, and evolutions follow wherever they
lead — a Kanto run still reaches Crobat and Blissey.

## Hardware

- Board: [ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
  — get the **Standard** (no case) or **-G** (GPS, also fits) version; **not the "-B"**
  (ships with a protective case that won't fit). The separate "1.75**C**" is a different board.
- Round 466×466 AMOLED, **CO5300** driver (QSPI, 80 MHz)
- Capacitive touch **CST9217** (I2C, address 0x5A)
- **AXP2101** (power management + battery + PWR button), **PCF85063** (RTC),
  microSD slot, **ES8311** audio codec (→ amplifier → external speaker on the
  MX1.25 connector)
- Pins taken from the [official Waveshare repo](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75) (see `pin_config.h`)

## Libraries (Arduino IDE / arduino-cli)

| Library | Author | Use |
|---|---|---|
| GFX Library for Arduino (`Arduino_GFX`) 1.6.4+ | moononournation | CO5300 over QSPI + framebuffer in PSRAM |
| FreeType 2.14.3 (minimal vendored build) | FreeType Project / Espressif component | hinted OpenType rendering from UI packs |
| SensorLib | Lewis He | CST9217 touch + PCF85063 RTC + QMI8658 IMU |
| XPowersLib | Lewis He | AXP2101 PMU (battery, brightness, PWR button) |
| ESP_I2S (bundled in the ESP32 core) | Espressif | I2S to the ES8311 codec |

## IDE setup / build

- Board: **ESP32S3 Dev Module** · Flash **16MB** · PSRAM **OPI PSRAM**
  (required: the 466×466×16-bit framebuffer ≈ 434 KB lives in PSRAM) ·
  Partition Scheme with FAT (e.g. `16M Flash (3MB APP/9MB FATFS)`) ·
  USB CDC On Boot **Enabled**

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
TAMAPOKE_VERSION="$(python3 tools/firmware_version.py)"
FW_DEFINE="$(TAMAPOKE_VERSION="$TAMAPOKE_VERSION" python3 tools/firmware_version.py --cpp-define)"
arduino-cli compile --fqbn "$FQBN" \
  --build-property "compiler.cpp.extra_flags=$FW_DEFINE" .
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn "$FQBN" .

# or just, which finds the port itself and can open the console:
bash tools/flash.sh --monitor
```

Supported local and Pages latest-build scripts stamp the firmware with the
current short commit ID and UTC build time. The stable GitHub Pages build uses
the published GitHub Release tag verbatim.

### Run it on your computer

`tools/emu/` compiles the **real firmware** — `TamaPoke.ino`, `pet.cpp`,
`i18n.cpp`, `party.cpp`, unmodified — into a clickable desktop app, stubbing only
the hardware. Click to touch, drag to swipe, type serial commands in the
terminal, and `--fast 60` runs a whole 3-day life in about an hour.

```bash
brew install sdl2 freetype # Debian: apt install libsdl2-dev libfreetype-dev
bash tools/emu/build.sh
tools/emu/tamapoke-emu --scale 2 --fast 60
```

There's a headless `--shot` mode for screenshots too. See
[`tools/emu/README.md`](tools/emu/README.md). It won't tell you anything about
timing, DMA tearing, PSRAM or audio — those still need the board.

### Easiest install: the web installer

`web/index.html` flashes the firmware (ESP Web Tools) and deploys UI, battle,
move, item, region and question packs to the SD over Web Serial, no Arduino needed. The linked
`web/question-bank.html` imports banks from files or a connected device, searches and
paginates questions, edits, exports, builds and directly deploys indexed question banks.
Package/question IDs and the internal revision are maintained automatically; the page also
reads/writes the device's global answer rules. Serve it over HTTPS or `localhost`
(secure context) and open it in **Chrome/Edge**. See [`web/README.md`](web/README.md).

This runtime-pack migration intentionally resets saves created by older firmware.
Unknown save schemas are cleared instead of being interpreted as the new runtime
catalogue layout.

### Generate and load the data packs yourself

Most sprites come from **[PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab)**
(CC BY-NC). Its fixed-revision PNG/XML sources are stored under
`tools/pokemon_art/pmd/`. Independently generated transparent four-view sheets in
`tools/pokemon_art/ai/` fill all 48 missing base species, all 57 missing Mega forms
and all 32 Gigantamax forms. `pack_pmd.py` and `pack_ai_art.py` convert those local
sources without network access, and `gen_data_packs.py` combines the generated
TPK2/TPTH files with UI, species, move, description, trainer, battle and badge data.
Optional four-frame action atlases replace derived fallback motion when present.
No regional intermediate bundle is created. Newly packed sprites
also carry rear Idle/Hurt/Attack actions, so the player's battle Pokemon faces
away from the player. A missing rear action falls back to the matching front
action. Missing shiny form art preserves the individual's available base shiny
sprite instead of substituting the normal-colour form.

```bash
python3 tools/pack_pmd.py --report base-sprite-coverage.json
python3 tools/pack_pmd.py --mega --mega-report mega-sprite-coverage.json
# Mega outputs are pmNNN-{standard,x,y,z}[-shiny].bin
python3 tools/pack_ai_art.py # missing base, Mega and Gigantamax sources -> TPK2
python3 tools/make_thumbs.py    # Pokédex thumbnails (from the PMD sprites) -> thumbs.bin
python3 tools/fetch_species_descriptions.py # fill missing descriptions in pokemon_data.json
python3 tools/gen_data_packs.py # web/packs/*.{tui,tbattle,tmove,titem,tregion,tquiz} + index.json
python3 tools/check_data_packs.py
```

These packs use content ABI 8; firmware and regional packs must be updated
together when upgrading from an older ABI.

The checked-in Chinese font subset already covers every string, species, move,
type and regional description in the catalogue. When localized content gains
characters, rebuild it from Noto Sans SC Medium (or the compatible CJK SC face)
with FontTools before
regenerating the packs:

```bash
python3 tools/subset_ui_font.py /path/to/NotoSansCJK-Medium.ttc \
  tools/assets/fonts/NotoSansCJKsc-Medium-subset.otf --font-number 2
```

Then deploy the selected packs from the Web installer. The firmware only accepts
validated pack files under `/packs`; interrupted uploads use a temporary file and
cannot overwrite a working pack.

Question-bank authoring JSON lives under `tools/question_banks/`; each source is
compiled into an indexed `.tquiz` and added to the generated web catalogue. The
browser question-bank editor provides the same binary format without requiring the
Python pipeline.

## Controls

- The bottom arc opens feeding, ball/defence training, sleep and bath actions.
- Tap the companion to pet it; tap its name for Lead, Pokédex, Settings,
  Release/Farewell and power-off actions; tap the slot indicators to open the Box.
- Swipe horizontally between occupied cultivation slots, up for the four-page stat
  card, and down for the bag, battle centre, badges, breeding and task-centre
  navigation page.
- Short-press the physical PWR button to toggle the screen; hold it for four seconds
  to power off while the RTC continues tracking time.

## Runtime data packs

- **UI (`.tui`)** — one installed language per pack: strings, layout metrics
  and either a compact bitmap face or a hinted OpenType subset with package-defined
  pixel sizes. The language list is discovered at boot.
- **Battle (`.tbattle`)** — shared ability definitions, the type catalogue and
  the 18×18 effectiveness chart.
- **Moves (`.tmove`)** — stable move IDs, mechanics, G-Max Move definitions,
  names and localized descriptions.
- **Items (`.titem`)** — stable item keys, effects, names, localized descriptions
  and optional icons.
- **Regions (`.tregion`)** — species records, learnsets, Mega/Gigantamax metadata
  and species-to-G-Max-Move references,
  localized names and descriptions,
  PMD sprites, thumbnails, region metadata, trainers, regional battle configuration
  and badges.
- **Questions (`.tquiz`)** — language spans, fixed-width random-access indexes and
  variable-size question records, read on demand from the microSD.

Sprites are read lazily from the region pack into PSRAM. OpenType faces and their
bounded glyph cache also live in PSRAM. Package validation directs incomplete
installations to the built-in USB recovery screen.

## Pokédex and species data

`tools/pokemon_data.json` is the committed source for species names, slugs,
types, presentation metadata, evolution rules, rarity, base stats, abilities,
learnsets and Mega/Gigantamax forms. `tools/move_data.json`,
`tools/item_data.json` and `tools/battle_data.json` are the independent sources
for moves, items, and shared abilities/type rules. Pack generation is fully
offline; fetch scripts only refresh these committed catalogues when explicitly run.
Base stats use **current** values, not Gen 1 ones —
Pidgeot has 101 Speed here, not the 91 it had in Red/Blue. The generator emits
these records into region packs; `dex.h` contains only the stable runtime ABI and
limits. The pet's identity is its Pokédex number (persisted in NVS).

- **Evolution** gen-1 style (levels 16/36/…; stones ≈30, trade ≈40; Eevee
  branches to whichever evolution you're missing). Each slip-up delays it 1
  level; it won't evolve with any stat < 40 or while asleep.

## Types

Every species carries its real **typing** (one or two of the 18 types) and the game
ships the full **18×18 effectiveness chart** in the battle pack from
`tools/battle_data.json`.

The chart uses the **current Gen 6+ rules**, including Steel and Fairy typings and
Fairy's immunity to Dragon.

Seven of the original 151 differ from their Gen 1 typing: Magnemite and Magneton gained Steel
(Gen 2), and Clefairy, Clefable, Jigglypuff, Wigglytuff and Mr. Mime gained Fairy
(Gen 6) — the first two losing Normal entirely.

Typing is shown on the Battle page of the stat card and drives trainer, wild and
LAN battle damage.

## Battle stats and training

Each creature has ATK/DEF/SPD/VIT = **base stat** + level + **IV** (ordinary
rolls stop at 31, but stored values and gym rewards are not capped; `IV × level/100`) + **training**, followed
by its nature modifier and then its gender modifier (see
[Battle stats & IVs](#battle-stats--ivs)):
- SPEED ← the **reaction test** (~2 reactions = one score step)
- DEFENSE ← the ball game (~2 rallies = one score step)
- STRENGTH ← the training bag (~4 hits = one score step)
- VIT (vitality) derives from the base HP stat

### Moves

Each creature keeps **4 active moves and 4 reserve moves**. Battle selection normally
uses these eight learned moves:

- **Level-up moves** are gated: Charizard learns FLAMETHROWER at 34, WING ATTACK
  at 36, DRAGON RAGE at 54. A hatchling starts with **only** what its species
  knows at level 1. Crossing a gate fills active slots first, then reserve slots;
  when all eight are full, one learned move is replaced at random.
- In wild and trainer battles, each participating creature can observe at most one
  opposing move that appears in its species' complete original learnset and that it
  does not already know. The chance is `bond × 30% / 100` (0% at bond 0, 30% at
  bond 100). A successful observation adds a fifth move that can be used immediately
  for the rest of that battle. Once the current round finishes, a
  tap-to-dismiss notice pauses the battle before the next round or settlement. The
  move remains observed even if the creature later faints or the battle is lost.
  After battle it fills a free learned slot; if all eight are full, the player may
  keep it and randomly replace one learned move, or decline it.
  Evolving keeps the moves it already has, and the new form's gates take over —
  moves it would have learned *below* your current level are not backfilled,
  same as the real games.
- The bag has one attributed **Move Stone** item type. Each stone stores one move,
  and only stones storing the same move stack together. A stone can be used only
  when the current species' complete original learnset contains that move. The bag
  asks for confirmation when compatible and explains the refusal when incompatible
  or already known. A successful stone follows the same active-then-reserve fill
  order and random replacement rule.
- Each wild victory still grants one weighted ordinary drop. Move Stone has weight
  **10** in that pool; when selected, its stored move is chosen only from the defeated
  wild creature's distinct active and reserve moves.

The **MOVES** page manages the eight learned moves and swaps a selected move into
one of the four active slots.

Moves remain part of each creature's complete state. They continue with it
between cultivation slots and freeze only while that creature is in the Box.

**Special attack and defence** come off the species' own `bSpA`/`bSpD` base stats
(Alakazam is 50 Attack but 135 Special Attack), reusing the physical IV and
training: special attack runs off the ATK IV and training, and special defence
runs off the DEF IV and training. The physical/special split lives on the species.

The IV also sets **how far each stat can be trained at all** (77–100), so a
well-rolled individual has a genuinely higher ceiling, not just a head start.
See [Battle stats & IVs](#battle-stats--ivs) for the numbers.

When a player creature enters battle, bond multiplies max HP, Attack, Defense,
Special Attack, Special Defense and Speed by `70% + Bond / 2`. Bond 0 gives 70%,
Bond 60 gives 100%, and Bond 100 gives 120%; the stat card continues to show the
underlying values before this battle-only multiplier.

Shown on the Battle page of the stat card. The (hidden) weight goes up with candy
and burns off with training.

## Retention: streak, bond, medals, name

- **Streak** (the player's, persists across creatures): the first care of each
  real day advances the streak; 3/7/30/100 milestones are celebrated; skipping a
  day breaks it. Flame badge on the main screen.
- **Bond** (the creature's): rises slowly with care and petting, drops with slip-ups,
  and scales all six player battle stats from 70% to 120% (100% at Bond 60).
- **Medals** for the individual (level, berry, streak, bond, final form, fit) +
  a global counter. Medals page of the stat card.
- **Name**: nicknames appear in the header and stat card.

High streak and bond improve a safety egg's rarity and IVs. Wild shiny encounters
use only the shared farewell bonus.

## Life cycle, wild rare traits, languages

After a final form has been cultivated for **3 days**, it may Farewell; before
that it may be Released from the menu. All four bars at zero for 1 h causes a
Runaway. Farewell adds +1 point to the wild shiny odds (+2 at level 100), Runaway
subtracts 2, and Release is neutral; the shared bonus is clamped to 0–15.

Wild shiny odds start at exactly 1/4096. One roll controls both the alternate
sprite and persistent particles, and its IV floor is 20. Endings remove the
creature from the roster; an empty team and Box receive a safety egg.

**Languages:** the supplied pack set includes English (default), Spanish, French,
German, Italian, Portuguese and Simplified Chinese. Settings list the installed
`.tui` packs.

## Backgrounds: biome + real time

The idle screen paints the sky from the **RTC's real time** (dawn / day / dusk /
night with moon and stars) and the ground from the **type's biome** (meadow,
beach, forest, volcano, mountain, snow). Sleeping forces night.

## Layout

- `TamaPoke.ino` — init, game loop, render of every screen, gestures, serial console, audio
- `pet.h` / `pet.cpp` — pet state and logic (stats, evolution, life cycle, streak/bond/medals, NVS)
- `party.h` / `party.cpp` — six cultivation records, active switching, migration, the 24-slot Box and durable breeding-centre slots
- `content.h` / `content.cpp` — pack ABI, CRC validation, catalogues, descriptions and lazy assets
- `quiz.h` / `quiz.cpp` — global answer rules, exact arithmetic generation/input and timed settlement
- `sdmon.h` / `sdmon.cpp` — packed TPK2 sprites + thumbnails and atomic pack reception over USB
- `rtcbat.h` / `rtcbat.cpp` — PCF85063 RTC + AXP2101 PMU (battery, brightness, PWR button)
- `audio.h` / `audio.cpp` — ES8311 + I2S + Game-Boy-style tone synth (non-blocking task)
- `i18n.h` / `i18n.cpp` — dynamic installed-language selection and string IDs
- `dex.h` / `moves.h` — stable firmware ABI; catalogue records live on the SD
- `ui_art.h` — generated core UI icons/colours; pet sprites only exist in region packs
- `pin_config.h` — the board's official pins
- `tools/` — committed JSON catalogues plus `sprites.py` (workshop),
  `pack_pmd.py` / `make_thumbs.py`,
  `gen_data_packs.py`, `quiz_pack.py`, validators and `touch_log.py`
- `tools/pokemon_art/pmd/` — fixed-revision PMD PNG/XML sources, licence and
  per-artist credit data referenced by `pokemon_data.json`
- `tools/emu/` — desktop emulator (real firmware + stubbed hardware, SDL)
- `tools/sdcard/mons/` — generated sprite inputs (animated, alternate-color, PMD, thumbnails)
- `web/packs/` — deployable UI, battle, move, item, region and question packs plus their dynamic catalogue
- `web/` — the browser installer and question-bank editor (ESP Web Tools + Web Serial deployment/configuration)

## Serial console (115200, debug)

`STATS` (full state) · `SPEC <dex>` (change species) · `LVL <n>` ·
`IV <atk> <def> <spd> <vit>` (force individual values) · `HATCH` ·
`EGG <dex> [color]` (hatch a chosen species for debugging) ·
`SHINY` / `SPARKLE` (toggle the combined shiny state) · `NICK <x>` ·
`BYE` / `RUN` (farewell / runaway) · `ABANDON` (force the
runaway-ready state) · `WIPE` (factory reset → new game) · `BEEP` (audio test) ·
`REG` (Pokédex) · `EGGS` (simulate 20 eggs) · `GAL` (gallery) · `CAREDAY` ·
`PARTY` / `PARTY <dex>` / `PARTY CLEAR` (inspect and fill the party) ·
`TIME <epoch>` / `RTCSET <epoch>` · `HEALTH` (uptime + heap for the soak test) ·
`LS` / `GET` / `PUT` (list, read or deploy validated `/packs` files) · `QUIZCFG` (read global answer rules) ·
`QUIZSET <time> <ops> <terms> <operandDigits> <answerDigits> <decimals> <fractionDigits> <flags> <parenthesisDepth> <choiceWeight> <questionTypes>`, where `questionTypes` is a bit mask: multiple choice `1`, arithmetic `2`.

To test fast: lower `PET_TICK_MS`, `MINUTES_PER_LEVEL` and `FAREWELL_AGE_MIN` in `pet.h`.

## Credits

Most sprites: [PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab)
(community, CC BY-NC); catalogue gaps use the documented generated turnarounds.
Base stats: [PokéAPI](https://pokeapi.co). Pokémon is a ™ of
Nintendo / Game Freak / The Pokémon Company. Non-commercial, personal-use project.
Full list in [`CREDITS.md`](CREDITS.md).

## License

- **Source code** (firmware + tooling): **[MIT](LICENSE)**.
- **Sprites & names**: © Nintendo / Game Freak / The Pokémon Company; community
  pixel art from [PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab)
  (CC BY-NC 4.0), **non-commercial use only**. Independently generated gap art is
  documented in [`CREDITS.md`](CREDITS.md); no rights are claimed over the designs.
- **3D-printed case**: remix of *"Pokeball"* by **yoyothechicken**
  ([MakerWorld #839922](https://makerworld.com/es/models/839922-pokeball)),
  licensed **CC BY-NC-SA**, and shared here under the same terms.

This is an unofficial fan project, not affiliated with or endorsed by Nintendo.
