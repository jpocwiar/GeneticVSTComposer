# Porównanie z Synthesize-music-from-description — 2.6

Audyt z 27.10.2026 dotyczy rzeczywistego kodu `../Synthesize-music-from-description/generation/evolutionary.py`, a nie samego opisu projektu. Wtyczka przed zmianą: commit `40afa6f82762d40566586b7ac8a0aea4f51b0943` (2.5). Kod referencyjny pozostawiono bez zmian; uruchomiono jego trzy generatory. Dane pomiarowe znajdują się w `docs/audit-2.6/` w repozytorium.

## Co rzeczywiście było uproszczone za mocno

- **Gęstość.** Poprzedni cel liczby nut i inicjalizacja preferowały około czterech nut na takt. Samo dodawanie pokoleń utrwalało ten rezultat. Nowy cel opiera się na logarytmicznej długości rytmicznej, a Density jest ponownie widoczne.
- **Mutacja rytmu.** W Pythonie znaczniki `-1` (pauza) i `-2` (przedłużenie) można zamieniać na początki nut; operatory tworzą pauzy, wydłużenia i kopiują fragmenty. Wtyczka głównie zmieniała wysokość, velocity i długość oraz czasem dzieliła nutę. Przywrócono odpowiedniki wstawiania/usuwania, przesuwania początku, łączenia i kopiowania fragmentów razem z przerwami. Są oceniane wewnątrz GA, przed wyborem zwycięzcy. Keep rhythm i blokady pozostają nadrzędne.
- **Zbyt ścisłe przyciąganie do akordów.** Własna harmonia dociągała niemal wszystkie propozycje do akordu, również pomiędzy mocnymi częściami taktu. Referencja stosuje docelowy częściowy udział nut akordowych. Obecnie słabe części taktu dopuszczają ruch po skali, a mocne preferują stabilność. Jazziness = 0 nadal wymaga ścisłego dopasowania zwykłych nut; mikrotonowe passing notes mają osobne reguły.
- **Mały krok a bezruch.** Małe niezerowe interwały nadal są tanie. Dodano karę za nadmiar kolejnych identycznych wysokości oraz cel różnorodności zależny od Diversity.
- **Dwa takie same akordy.** Przy dwóch pozycjach i Resolve oba końce wymuszały tonikę, dając I–I. Teraz otrzymujemy V–I. Przy tylko jednej pozycji nadal naturalnie istnieje jeden akord; liczba pozycji i liczba różnych akordów są widoczne.
- **Ukryta długość harmonii.** Jednotaktowa melodia przy zmianie akordu co takt oznaczała jeden akord. Chords ma teraz własne jawne Length, domyślnie cztery takty. Follow melody zachowuje możliwość powiązania długości.

## Pomiar C++ przed i po

Ten sam program `scripts/probe-musical-engine.cpp`, te same 72 przypadki: trzy typy frazy × harmonia domyślna/własna × 12 ziaren. Quick: 64 osobniki, 40 pokoleń; cztery takty 4/4, pozostałe ustawienia domyślne. Oceniono pierwszy wariant każdego banku. Średnie z 24 przypadków na typ:

| Typ | Nuty/takt przed | Nuty/takt po | Nuty do 0.5 ćwierćnuty przed/po | Kolejne równe wysokości przed/po | Czas przed/po |
| --- | ---: | ---: | ---: | ---: | ---: |
| Riff | 4.17 | 7.08 | 31% / 83% | 32% / 23% | 26.0 / 39.2 ms |
| Motif | 4.00 | 7.67 | 50% / 86% | 11% / 19% | 27.4 / 39.2 ms |
| Melody | 4.39 | 7.26 | 33% / 88% | 31% / 22% | 25.1 / 34.6 ms |

W każdym przebiegu było **2624 ocen = 64 × (40 + 1)**. To pomiar gęstości, struktury i czasu na tej maszynie, nie ocena atrakcyjności muzycznej. Motif nie poprawia każdej metryki: jego udział identycznych kolejnych wysokości wzrósł. Nie dobierano ziaren do prezentowanych wyników. Gęstsze wyniki wymagają oceny słuchem.

## Uruchomienie niezmienionego Pythona

`scripts/probe-reference.py` ładuje plik referencyjny bez uruchamiania aplikacji ani usług zewnętrznych. Publiczne zależności zainstalowano lokalnie w `build/reference-python`. Ziarna 1 i 2, populacja 64, 40 pokoleń, cztery takty, Cm / Fm / Ab / G7, valence/arousal 0.5. Bass riff używa kroku 0.5, motif repeated i melody kroku 0.25. Licznik obejmuje każde faktyczne wywołanie fitness, także ponowną ocenę tego samego osobnika.

| Generator | Nuty/takt, ziarna 1 / 2 | Czas, ziarna 1 / 2 | Wywołania fitness na przebieg |
| --- | ---: | ---: | ---: |
| GeneticBassGenerator, riff | 5 / 6 | 1.81 / 1.84 s | 10304 |
| GeneticMotifGenerator, repeated | 16 / 16 | 0.31 / 0.30 s | 2560 |
| GeneticMelodyGenerator, melody | 8.75 / 7.25 | 7.62 / 7.22 s | 10304 |

To sześć ilustracyjnych uruchomień referencji, a nie kontrolowane porównanie jakości z 72 przypadkami C++. Reprezentacja, rejestr, część ustawień i fitness różnią się. Python riff optymalizuje krótką komórkę, potem powiela i dostosowuje ją do akordów. Obecny C++ aktualizuje kopie przed oceną całej frazy. Kod Pythona wielokrotnie przelicza fitness przy selekcji; C++ zapamiętuje wynik każdego osobnika w danym pokoleniu. Różnice reprezentacji, funkcji oceny, wielokrotnego liczenia i języka wyjaśniają szybszy przebieg, ale ten pomiar nie rozdziela ich udziałów. Czasu nie wydłużono sztucznie.

To nadal nie jest port 1:1 funkcji fitness ani wszystkich operatorów referencji. Mikrotonowe geny, ich koszty i ograniczenia są rozszerzeniem C++; pierwowzór był 12-TET. Ustawienie większego Search faktycznie zwiększa budżet, lecz nie gwarantuje lepszego riffu.

## Jasny przepływ pracy

- **New chords only**: nowe akordy, istniejący bank melodii pozostaje.
- **Melody from these chords**: widoczne akordy pozostają, nowy bank obejmuje całą ich długość i metrum.
- **New chords + melody**: nowa sekwencja, następnie bank melodii na niej.
- Obie akcje tworzenia melodii wracają na Melody i włączają With chords; przypięte warianty są zachowane.
- **Generate melody bank** na Melody używa aktywnej harmonii i długości wybranej na Melody. **Evolve selected** rozwija wybraną frazę.
- **Revoice** zmienia tylko przewroty/rejestr. Zmiana Root, Scale czy Originality nie przepisuje istniejących nut.
- Space uruchamia partię widocznej karty; kolejne naciśnięcie zatrzymuje odsłuch. Search zachowuje po zakończeniu liczniki i rzeczywisty czas.

## Powtórzenie pomiaru

Z głównego katalogu po zbudowaniu silnika:

```sh
c++ -std=c++17 -O2 -I JUCE/GeneticVSTComposer-JUCE/Source scripts/probe-musical-engine.cpp build/libcomposer_engine.a -o build/probe-musical-engine
build/probe-musical-engine > build/current-musical-engine.csv
PYTHONPATH=build/reference-python python3 scripts/probe-reference.py > build/reference.csv
```

Python wymaga lokalnego projektu referencyjnego oraz numpy, mingus i MIDIUtil. Dla pomiaru „przed” należy skompilować ten sam program przeciwko silnikowi z podanego wyżej commitu. Nie trzeba zastępować bieżącego drzewa roboczego.

## Weryfikacja

Testy obejmują gęstość dla trzech typów i wielu ziaren, reakcję Density, pełne budżety, nowe początki rytmiczne przy ewolucji, zachowanie Keep rhythm, rozróżnienie dwóch akordów we wszystkich dziesięciu skalach, pełną długość harmonii (także ułamkową), przyciski obu operacji i spację na Chords. Istniejące testy mikrotonów, MIDI, stanu sesji, transportu i blokad nadal obowiązują. Stan wykonanych walidacji znajduje się w `DEVELOPMENT.md` w repozytorium.
