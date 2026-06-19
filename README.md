# TETRIS+ (C, power-up twist)

Egyfájlos Tetris C-ben, power-up csavarral: eső speciális elemek (bomba, lézer)
és sortörlésért töltődő készlet (lassítás, sortörlő, csere).

## Fordítás

MinGW-w64 / gcc szükséges. Ha nincs:
- `winget install BrechtSanders.WinLibs.POSIX.UCRT`  — vagy
- töltsd le a **w64devkit**-et, és tedd a `bin` mappáját a PATH-ra.

Játék:
```
gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c
```

A program induláskor maga kapcsolja be az ANSI/VT-feldolgozást, így a
klasszikus Windows-konzolban (conhost) és a Windows Terminalban is helyesen
rajzol. Ha valamiért mégis nyersen látszanának az escape-kódok, fordítsd a
színek nélküli, `system("cls")`-es tartalék változatot:
```
gcc -std=c11 -O2 -DUSE_CLS -o tetris tetris.c
```

Tesztek:
```
gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c
./tetris_test
```

## Vezérlés

| Billentyű | Hatás |
|---|---|
| ← → / A D | mozgás |
| ↑ / W | forgatás |
| ↓ / S | puha esés |
| Space | kemény dobás |
| 1 2 3 | power-up elsütése |
| P | szünet |
| Q | kilépés |

Bármely modern Windows-konzolban fut (a VT-t a program kapcsolja be); a
Windows Terminal ajánlott a legszebb színekhez.
