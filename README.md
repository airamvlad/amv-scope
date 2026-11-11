# AMVScope

VST3 osciloskop plugin za Windows i macOS, pravljen za Bitwig Studio, po uzoru na Oszillos Mega Scope.
Glavna namena je provera kicka i basa: kako se talasni oblici poklapaju u vremenu i fazi.

![AMVScope](docs/screenshot.png)

## Šta radi (verzija 0.1)

- **Prikaz sinhronizovan sa tempom:** dok Bitwig svira, talasni oblik se crta na mestu u taktu, pa slika stoji mirno dok se muzika ponavlja. Udarac kicka je uvek na istom mestu, a bas posle njega se vidi tačno u odnosu na kick.
- **Length:** dužina prikaza 1/16, 1/8, 1/4, 1/2, 1 takt, 2 ili 4 takta (prati takt projekta, npr. 3/4). Za kick i bas je obično najbolje 1/4.
- **Sync:** *Host* prati Bitwig dok svira, a kad je transport zaustavljen prelazi na okidanje po nivou. *Trigger* uvek okida po nivou.
- **Trigger:** nivo (u dB) na kom počinje crtanje kad se ne prati transport; narandžaste isprekidane linije ga prikazuju na ekranu.
- **Zoom:** uvećanje po visini (1x do 32x), za tihe signale ili detalje repa kicka.
- **Freeze:** zamrzava sliku da bi se mirno pogledala.
- Plugin ne menja zvuk; podešavanja se pamte u projektu.

## Plan

- [x] Kostur: CMake/JUCE projekat, build na GitHub-u, instaleri
- [x] v0.1: talasni oblik sinhronizovan sa tempom (1/16 do 4 takta), zoom po amplitudi, freeze, trigger po nivou kad transport ne svira
- [ ] v0.2: više signala (sidechain ulaz ili deljenje između instanci), prikaz jedan preko drugog u bojama, bojenje po frekvenciji
- [ ] v0.3: promenljiva veličina prozora, tasteri, podešavanja koja se pamte u projektu

## Preuzimanje i instalacija

Poslednja verzija sa instalerom za Windows i Mac nalazi se na stranici **[Releases](../../releases/latest)**.

- Windows: preuzmi i pokreni `AMVScope-...-Windows-Setup.exe`.
- Mac: preuzmi `AMVScope-...-macOS.pkg`, desni klik, pa *Open*.

Instaleri nisu potpisani, pa sistem prikaže upozorenje pri prvom pokretanju. Detalji i ručna instalacija su u [docs/INSTALL.txt](docs/INSTALL.txt).

## Za razvoj

Potrebni su CMake 3.22+ i C++20 kompajler. JUCE se preuzima automatski.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- `src/dsp/`: jezgro u čistom C++-u, bez JUCE-a (`SyncedCapture` raspoređuje uzorke po poziciji u taktu, `TriggerDetector` okidanje po nivou), testira se bez DAW-a (`tests/DspTests.cpp`).
- `src/plugin/`: JUCE plugin (procesor i prozor).
- `tests/PluginTests.cpp`: pokreće pravi procesor bez DAW-a.

Plugin koristi [JUCE](https://juce.com) (besplatan za ličnu upotrebu; za prodaju proveriti uslove licence).
