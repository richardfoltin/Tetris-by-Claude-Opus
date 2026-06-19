# Tetris C-ben — power-up csavarral (Design / Spec)

- **Dátum:** 2026-06-19
- **Állapot:** jóváhagyásra vár
- **Nyelv:** C (egyetlen `.c` fájl)
- **Platform:** Windows 10 (de a kód hordozható marad, lásd Megkötések)

---

## 1. Cél

Egy játszható, valós idejű Tetris klón C-ben, amelyben a klasszikus
sortörlést egy **power-up rendszer** dobja fel: részben **eső speciális
elemekkel**, részben **sortörlésért töltődő, kézzel elsüthető készlettel**.

## 2. Megkötések (hard constraints)

1. **Nyelv:** C, **egyetlen forrásfájl** (`tetris.c`).
2. **Csak a C szabványkönyvtár**, két kivétellel, amelyeket a felhasználó
   kifejezetten engedélyezett:
   - **Nem-blokkoló billentyűzet:** `conio.h` (`_kbhit`, `_getch`).
   - **CPU-barát várakozás:** `windows.h` `Sleep()` a fő ciklusban.
3. **Nincs GNU extension** — `-std=c11 -pedantic` alatt is forduljon
   (a fenti két Windows-fejléctől eltekintve hordozható kód).
4. A teljes projekt külön mappában: `C:\Users\folti\Desktop\tetris\`.

> **Miért kell a 2-es kivétel?** A tiszta ISO C nem tud nem-blokkoló,
> Enter nélküli billentyű-beolvasást — e nélkül nincs valós idejű,
> eső-blokkos játék.

## 3. Mappa & fájlstruktúra

```
C:\Users\folti\Desktop\tetris\
├─ tetris.c            # a teljes játék (egyetlen fájl)
├─ README.md          # fordítás + futtatás + vezérlés
├─ highscore.txt      # csúcspont (a játék hozza létre, fopen)
└─ docs/superpowers/specs/2026-06-19-tetris-c-powerup-design.md
```

A `highscore.txt` mentés/betöltés szabványos `fopen`/`fprintf`/`fscanf` —
belefér a szabványkönyvtárba.

## 4. Alap-Tetris mechanika

- **Pálya:** 10 oszlop × 20 sor.
- **Elemek:** 7 klasszikus tetromino (I, O, T, S, Z, J, L).
- **Sorsolás:** 7-bag — minden hetes csomag tartalmazza mind a 7 elemet
  megkevert sorrendben (igazságos eloszlás).
- **Forgatás:** óramutató szerinti, egyszerű **fal-elrúgással**: ha a forgatott
  alak ütközne (fal vagy blokk), próbáljon 1, majd 2 cellát oldalra csúszni;
  ha úgy sem fér, a forgatás nem történik meg.
- **Szellemárnyék:** halványan jelzi, hová esne az aktuális elem.
- **Következő elem előnézet:** a pálya mellett.
- **Zárás (lock):** ha az elem nem tud lejjebb lépni, rögzül; rövid
  villanás után a teli sorok törlődnek.
- **Game over:** ha az új elem már a spawn-helyén ütközik.

**Hatókörön kívül (YAGNI):** hold-elem, teljes SRS forgatórendszer,
T-spin felismerés, mentés/visszatöltés, multiplayer.

## 5. Vezérlés

| Billentyű | Hatás |
|---|---|
| `←` / `A` | balra |
| `→` / `D` | jobbra |
| `↑` / `W` | forgatás |
| `↓` / `S` | puha esés (gyorsít) |
| `Space` | kemény dobás (azonnal le + lock) |
| `1` `2` `3` | készlet-power-up elsütése (1–3. slot) |
| `P` | szünet |
| `Q` | kilépés |

A nyílbillentyűk Windows alatt `_getch()`-csel két bájtként jönnek
(`0` vagy `224` prefix + kód) — ezt a bemenet-kezelő külön lekezeli,
és WASD-vel is megy.

## 6. A csavar — power-up rendszer

### 6.a Eső speciális elemek
Új elem sorsolásakor `SPECIAL_CHANCE` (alap: **8%**) eséllyel normál elem
helyett egy **egycellás speciális elem** jön, jól láthatóan jelölve:

- **Bomba** (`()` jellel, piros): lerakáskor a középpontja körüli **3×3**
  területet kitörli. A robbanás által teljessé vált sorok normál módon
  törlődnek és pontoznak.
- **Lézer** (`<>` jellel, cián): lerakáskor a celláján átmenő **teljes sort
  és teljes oszlopot** kitörli.

### 6.b Készlet power-upok (töltődő mérő)
- **Töltés:** minden ténylegesen törölt **teljes sor +1 töltést** ad egy
  mérőnek. `CHARGE_PER_POWERUP` (alap: **4**) töltésnél a mérő nullázódik,
  és egy **véletlen** power-up a készletbe kerül.
- **Készlet:** max `INVENTORY_SLOTS` (alap: **3**) tárolható; ha tele van,
  új power-up nem kerül be, amíg fel nem szabadul hely.
- **Elsütés:** `1` `2` `3` a megfelelő slotot süti el.

Power-up típusok:
1. **Lassítás** — `SLOWMO_MS` (alap: **8000 ms**) ideig az esési időköz
   `SLOWMO_FACTOR` (alap: **×2.5**) lassúbb.
2. **Sortörlő** — azonnal kitörli a **legalsó 2 sort**, a fölöttük lévőket
   lejjebb csúsztatja.
3. **Csere** — az aktuálisan eső elemet **`I` elemre** cseréli a tetején
   (kihúz a beragadásból); ha nem fér el, nem történik csere.

### 6.c Hangolható paraméterek (`#define`-ok a fájl tetején)
`SPECIAL_CHANCE`, `CHARGE_PER_POWERUP`, `INVENTORY_SLOTS`,
`SLOWMO_MS`, `SLOWMO_FACTOR`, valamint a bomba sugara és a kezdő esési idő.

## 7. Pontozás & szintek

- **Sortörlés pont:** 1/2/3/4 sor → 40 / 100 / 300 / 1200, szorozva
  (szint + 1)-gyel.
- **Szint:** minden 10 törölt sor után +1.
- **Sebesség:** az esési időköz a szinttel csökken (gyorsul), egy alsó
  korlátig.
- **Csúcspont:** `highscore.txt`-be mentve, induláskor betöltve.

## 8. Megjelenítés

- **Alapért.: ANSI escape kódok** `printf`-fel: minden képkockán a kurzort a
  bal felső sarokba visszük (`\033[H`), és újrarajzoljuk a teljes képet a régi
  fölé (nincs teljes törlés → villódzásmentes). Színes blokkok ANSI
  színkódokkal.
- **Cella-rajz:** 2 karakter/cella, csak ASCII a hordozhatóságért:
  - üres: `· ` (vagy két szóköz), kitöltött: `[]` az elem színével,
  - bomba: `()`, lézer: `<>`, szellemárnyék: `::`.
- **Tartalék:** `#define USE_CLS` esetén `system("cls")` + teljes
  újranyomtatás minden képkockán (mindenhol megy, de villog). Ez a megoldás
  azoknak, akiknek a terminálja nem értelmezi az ANSI-t.
- **Feltevés / kockázat:** az ANSI-mód VT-képes terminált igényel
  (Windows Terminal igen). A régi `conhost.exe` alapból nem biztos —
  ilyenkor a `USE_CLS` tartalék a megoldás (mert a VT explicit
  engedélyezése `SetConsoleMode`-ot, azaz további `windows.h`-hívást
  igényelne, amit a megkötés nem enged).

## 9. Időzítés (fő ciklus)

- Fix lépésű hurok kis `Sleep(ms)`-szel (pl. ~16–30 ms), így nem pörög a CPU.
- Külön számláló méri az **esési időközt**: ha letelt, az elem egy sort esik.
- Lassítás power-up alatt az esési időköz szorzódik a `SLOWMO_FACTOR`-ral.
- Minden iterációban: bemenet leolvasása (`_kbhit`/`_getch`, nem blokkoló) →
  állapot-frissítés → kirajzolás.

## 10. Kódstruktúra (egy fájlon belül)

Globális állapot helyett egy `Game` struct, amit a függvények paraméterként
kapnak. Logikai blokkok:

- **Típusok/állapot:** `Game` (pálya, aktuális elem + pozíció + forgás,
  következő elem, készlet, mérő, pont, szint, slowmo-időzítő, futás-flagek).
- **Elemek:** tetromino-alakzatok és forgatásaik, 7-bag generátor.
- **Mozgás/ütközés:** `can_move`, `rotate`, `lock_piece`, `hard_drop`.
- **Sorkezelés:** `clear_lines`, `shift_down`.
- **Power-up:** `spawn_piece` (speciális esély), `explode_bomb`,
  `fire_laser`, `add_charge`, `grant_powerup`, `use_powerup`.
- **Bemenet:** `read_input` (nyilak + WASD + akciók).
- **Kirajzolás:** `render` (+ `render_cls` ág a `USE_CLS`-hez).
- **Mentés:** `load_highscore`, `save_highscore`.
- **`main`:** init → fő ciklus → game over → csúcspont mentés.

## 11. Fordítás & futtatás

A gépen jelenleg **nincs C fordító** (sem MinGW/gcc, sem Visual Studio).
Ezért:

- Ajánlott: **w64devkit** vagy **MinGW-w64** (hordozható, ingyenes), vagy
  `winget install` a Visual Studio Build Tools-hoz. A telepítésben segítek.
- Fordítás (MinGW/gcc): `gcc -std=c11 -O2 -Wall -Wextra -o tetris tetris.c`
- Futtatás: `./tetris` (lehetőleg Windows Terminalban az ANSI miatt).

## 12. Verifikáció / tesztelés

C-ben nincs szabványos beépített tesztkeret, és a játék interaktív, ezért
**kézi végigjátszás** a jóváhagyási kritérium fordítás után:

- [ ] `-Wall -Wextra -pedantic` figyelmeztetés nélkül fordul.
- [ ] Mozgás, forgatás (fal-elrúgással), puha + kemény esés működik.
- [ ] Sortörlés + pontozás + szintlépés helyes.
- [ ] Eső **bomba** 3×3-at, eső **lézer** sor+oszlopot töröl.
- [ ] Mérő tölt, power-up a készletbe kerül, `1/2/3` elsüti.
- [ ] Lassítás, sortörlő, csere a leírtak szerint hat.
- [ ] Szünet, kilépés, game over, csúcspont mentés/betöltés.
- [ ] Hosszabb játék közben nincs 100% CPU (Sleep miatt).

## 13. Nyitott kockázatok

- **ANSI a terminálon:** ha a felhasználó terminálja nem VT-képes, a
  `USE_CLS` tartalékkal kell fordítani (villog). Windows Terminal ajánlott.
- **Fordító hiánya:** implementáció előtt telepíteni kell egyet.
