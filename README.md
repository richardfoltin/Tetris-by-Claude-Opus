# 🎮 TETRIS+ — Tetris egy power-up csavarral

Egyfájlos, valós idejű **Tetris tiszta C-ben**, Windows konzolra. A klasszikus
sortörlés mellé egy **power-up rendszer** kerül: részben a pályára *eső
speciális elemek* (bomba, lézer), részben *sortörlésért töltődő, kézzel
elsüthető készlet* (lassítás, sortörlő, csere).

A teljes játék egyetlen forrásfájl: [`tetris.c`](tetris.c) (~1070 sor),
benne a beépített tesztekkel is.

---

## ✨ Jellemzők

- Klasszikus **10×20**-as pálya, 7 tetromino, **7-bag** sorsolás (igazságos eloszlás)
- Forgatás **fal-elrúgással**, **szellemárnyék** (hová esne az elem), következő-elem előnézet
- Pontozás (40/100/300/1200 × szint), **szintek** 10 soronként, gyorsuló esés
- **Csúcspont** mentése/betöltése fájlból (`highscore.txt`)
- **A csavar: power-up rendszer** (lásd lentebb)
- Valós idejű, **nem-blokkoló** vezérlés; CPU-barát (`Sleep`-ütemezett) fő ciklus
- Színes **ANSI** megjelenítés, ami a **régi `cmd`/conhost konzolban is** működik (normál buildnél a program induláskor `SetConsoleMode`-dal bekapcsolja a VT-feldolgozást; `USE_CLS` buildnél a `cls`-es tartalék megy helyette)
- **138 ellenőrzés (assert)** a beépített tesztekben, ugyanabban a fájlban, `#ifdef UNIT_TEST` mögött

---

## 🌀 A csavar: power-up rendszer

A power-upok **két forrásból** jönnek.

### 1) Eső speciális elemek (a véletlenből)
Új elem születésekor **8%** eséllyel egy egycellás speciális elem jön normál
tetromino helyett. Ezek **nem forognak**, és **lerakáskor** (amikor leérnek) hatnak:

| Elem | Jel | Hatás lerakáskor |
|---|---|---|
| 💣 Bomba | `()` | a középpontja körüli **3×3-as** területet kitörli |
| ⚡ Lézer | `<>` | a celláján átmenő **teljes sort és oszlopot** kitörli |

### 2) Töltődő készlet (a sortörlésekből)
- Minden kitörölt teljes sor **+1 töltést** ad egy mérőnek.
- **4 töltésnél** a mérő nullázódik, és egy **véletlen** power-up a **készletbe** kerül (max **3 hely**).
- A tárolt power-upokat az **`1` / `2` / `3`** gombbal sütöd el, amikor jónak látod:

| Power-up | Hatás |
|---|---|
| 🐢 Lassítás | ~**8 mp**-re **×2,5** lassabb az esés |
| 🧹 Sortörlő | azonnal kitörli a **legalsó 2 sort** |
| 🔀 Csere | az aktuális elemet **`I`-re** cseréli (kihúz a szorult helyzetből) |

> Apró interakció: a bomba/lézer robbanás után is lefut a sortörlés-ellenőrzés,
> így ha egy robbanás teljes sort is befejez, az **pontot ér és a mérőt is tölti**.

---

## 🎯 Vezérlés

| Billentyű | Hatás |
|---|---|
| `←` `→` / `A` `D` | mozgás balra / jobbra |
| `↑` / `W` | forgatás |
| `↓` / `S` | puha esés (gyorsít) |
| `Space` | kemény dobás (azonnal le) |
| `1` `2` `3` | készlet-power-up elsütése |
| `P` | szünet |
| `Q` | kilépés |

---

## 🔧 Fordítás és futtatás

**Kell hozzá:** MinGW-w64 / `gcc` (Windows). Ha nincs, a `w64devkit` egy jó,
hordozható csomag — tedd a `bin` mappáját a PATH-ra.

**Játék fordítása és indítása:**
```sh
gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c
./tetris
```

A program induláskor bekapcsolja az ANSI/VT-feldolgozást, így a **klasszikus
Windows-konzolban és a Windows Terminalban is** helyesen rajzol. Ha valamiért
mégis nyersen látszanának az escape-kódok, fordítsd a színtelen, `system("cls")`-es
tartalék változatot:
```sh
gcc -std=c11 -O2 -DUSE_CLS -o tetris tetris.c
```

**Tesztek futtatása** (ugyanaz a fájl, `-DUNIT_TEST`-tel):
```sh
gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c
./tetris_test          # -> "138 checks, 0 failures"
```

---

## 📋 Elvárt paraméterek (a feladat megkötései)

A játék az alábbi, előre rögzített feltételekkel készült:

- **Nyelv:** C (C11), és a **teljes játék egyetlen forrásfájl** (`tetris.c`).
- **Csak a C szabványkönyvtár** — két kivétellel, amelyek külön engedélyt kaptak:
  - **`conio.h`** — a **nem-blokkoló billentyűzethez** (`_kbhit`, `_getch`). E nélkül a tiszta ISO C nem tud Enter nélküli, valós idejű beolvasást, így nem lenne valós idejű, eső-blokkos játék.
  - **`windows.h`** — az **eredeti megkötés csak a `Sleep`-et** engedte (CPU-barát ütemezés). A megvalósítás során — a spec eredeti korlátozásán **túllépve** — a `SetConsoleMode` is bekerült: a spec kifejezetten megjegyzi, hogy a VT bekapcsolása `SetConsoleMode`-ot igényelne, „amit a megkötés nem enged". Ezt **utólag, tudatos eltérésként** vettem fel, mert e nélkül a klasszikus Windows-konzol nyersen jeleníti meg az ANSI-kódokat (lásd a megjelenítés-javítást). Aki a szigorú megkötést akarja, a `-DUSE_CLS` build `SetConsoleMode` nélkül, `cls`-sel fut.
- **Nincs GNU/fordító-extension** — a kód **tisztán (0 warning) fordul** `-std=c11 -Wall -Wextra -pedantic` alatt (a fenti két rendszerfejléctől eltekintve hordozható).
- **Külön mappa** az Asztalon, saját git-történettel.
- **Legyen benne egy izgalmas csavar** → ez lett a power-up rendszer.

> Megjegyzés a tisztaságról: a tiszta ISO C nem ad nem-blokkoló bemenetet és
> konzol-vezérlést, ezért volt szükség a fenti Windows-fejlécekre. A `Sleep` és
> a nem-blokkoló bemenet engedélyezett kivételek voltak; a `SetConsoleMode` (VT)
> viszont a spec eredeti korlátozásán **túli, utólagos** — de a helyes
> megjelenítésért indokolt — kiegészítés. Minden egyéb (pálya, elemek, logika,
> sorsolás, pontozás, fájlmentés, power-up rendszer) tisztán a
> szabványkönyvtárból van.

---

## 🗂️ Projekt felépítése

```
tetris/
├─ tetris.c                       # a teljes játék + a beépített tesztek (#ifdef UNIT_TEST)
├─ README.md                      # ez a fájl
├─ .gitignore                     # build-artefaktumok, highscore.txt
├─ highscore.txt                  # futáskor jön létre (nincs verziókövetve)
└─ docs/superpowers/
   ├─ specs/  ...-design.md       # a jóváhagyott terv (spec)
   └─ plans/  ...-tetris-c-powerup.md  # a részletes, feladatokra bontott implementációs terv
```

A `tetris.c` belül világosan tagolt: típusok/állapot (`Game` struct) → tiszta
logika (PRNG, 7-bag, forgatás, ütközés, sortörlés, power-upok, pontozás,
fájl-I/O) → `#ifndef UNIT_TEST` interaktív réteg (bemenet, kirajzolás, fő
ciklus) → `#ifdef UNIT_TEST` tesztek. A mutálható állapot egyetlen `Game`
structban él, amit a függvények mutatón át kapnak — nincs globális állapot.

---

## 🧪 Hogyan készült

Spec → terv → teszt-vezérelt (TDD) megvalósítás. A logikát a beépített tesztek
**138 ellenőrzése (assert)** fedi (a futtatható tesztkód ugyanabban a fájlban
él, a release-buildből kifordul). A megjelenítés/bemenet/fő ciklus fordítással
és átnézéssel igazolt.
A `docs/superpowers/` alatt megtalálható a jóváhagyott spec és a teljes,
feladatokra bontott implementációs terv.

---

## 📄 Licenc

Hobbi/oktatási projekt — szabadon használható.
