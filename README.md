# AMVScope

VST3 osciloskop plugin za Windows i macOS, pravljen za Bitwig Studio, po uzoru na Oszillos Mega Scope.
Glavna namena je provera kicka i basa: kako se talasni oblici poklapaju u vremenu i fazi.

## Stanje (verzija 0.0.1, kostur)

- Plugin se učitava kao efekat (stereo ili mono), ne menja zvuk.
- Prikazuje talasni oblik ulaza uživo (poslednjih 50 ms, slobodan prikaz bez sinhronizacije).

## Plan

- [x] Kostur: CMake/JUCE projekat, build na GitHub-u, instaleri
- [ ] v0.1: talasni oblik sinhronizovan sa tempom (1/16 do 4 takta), zoom po amplitudi, freeze, trigger po nivou kad transport ne svira
- [ ] v0.2: više signala (sidechain ulaz ili deljenje između instanci), prikaz jedan preko drugog u bojama, bojenje po frekvenciji
- [ ] v0.3: promenljiva veličina prozora, tasteri, podešavanja koja se pamte u projektu

## Preuzimanje i instalacija

Od verzije 0.1.0 instaleri za Windows i Mac biće na stranici **[Releases](../../releases/latest)**.
Do tada se build svakog commita može preuzeti iz *Actions* (artifact `AMVScope-Windows` / `AMVScope-macOS`).
Detalji su u [docs/INSTALL.txt](docs/INSTALL.txt).

## Za razvoj

Potrebni su CMake 3.22+ i C++20 kompajler. JUCE se preuzima automatski.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- `src/dsp/`: jezgro u čistom C++-u, bez JUCE-a, testira se bez DAW-a (`tests/DspTests.cpp`).
- `src/plugin/`: JUCE plugin (procesor i prozor).
- `tests/PluginTests.cpp`: pokreće pravi procesor bez DAW-a.

Plugin koristi [JUCE](https://juce.com) (besplatan za ličnu upotrebu; za prodaju proveriti uslove licence).
