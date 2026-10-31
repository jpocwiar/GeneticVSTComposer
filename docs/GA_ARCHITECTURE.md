# Architektura generowania melodii

Ten dokument opisuje wersję 2.7 i bieżący kod `ComposerEngine.cpp` oraz testy `MusicalArchitectureTests.cpp`. Funkcja fitness jest zestawem jawnych heurystyk muzycznych. Wyższy wynik oznacza lepsze dopasowanie do tych heurystyk i ustawień, nie obiektywnie lepszą muzykę.

## Inspiracja i różnice wobec pierwotnego projektu

W sąsiednim projekcie `Synthesize-music-from-description/generation/evolutionary.py` tryby `riff` i `short_riff` klasy **GeneticBassGenerator** optymalizują jeden takt, a `long_riff` dwa takty. Następnie powtarzają rytm i wysokości, dostosowując je do prym kolejnych akordów. Nie były to tryby bazowego generatora melodii. **GeneticMotifGenerator** ma osobne sposoby konstruowania motywu: cały takt, pół taktu z pauzą oraz krótszy fragment powtarzany dwa lub cztery razy. Aktywna funkcja `fitness_repeated_fragments` bazowego generatora nagradza powracające fragmenty obejmujące także pauzy i przedłużenia; mutacje kopiują fragmenty frazy.

Obecny silnik korzysta z tych zasad konstrukcyjnych, ale nie jest dosłownym portem klas Pythona. Nie używa znaczników `-1` i `-2`: chromosom zawiera jawne początki oraz długości nut. Funkcja oceny, architektura fraz i operatory są zaimplementowane w C++. Mikrotonowe geny oraz ich ocena są nową częścią silnika; wskazana implementacja referencyjna pracowała w 12-TET.

## Chromosom i pełny przebieg GA

Chromosom to `Clip`, zawierający do 512 uporządkowanych zdarzeń. Gen nuty przechowuje:

- początek i długość w ćwierćnutach;
- klawisz MIDI `pitch` oraz niezależny offset `cents`;
- velocity oraz flagę blokady;
- w otaczającym klipie: długość, metrum, siatkę i seed.

Rzeczywista wysokość używana do oceny interwałów wynosi `100 × pitch + cents`. Dwie nuty o tym samym klawiszu i różnych centach są różnymi zdarzeniami także przy porównywaniu podobieństwa fraz.

Preset Quick to 64 osobniki i 40 pokoleń, Balanced 128 i 100, Deep 256 i 300. Własny budżet pozwala ustawić 16–512 osobników oraz 1–2000 pokoleń. Przy co najmniej 64 osobnikach populacja dzieli się na cztery wyspy; mniejsza zostaje jedną. Ziarno każdej wyspy jest funkcją ziarna frazy i numeru wyspy. W każdym pokoleniu oceniana jest cała populacja, równolegle, a wynik nie zależy od kolejności wątków. Selekcja jest turniejowa. Każda wyspa zachowuje swojego najlepszego osobnika bez zmian, więc globalnie najlepszy wynik nie spada; nie ma już dwóch elit całej populacji. Potomstwo powstaje przez krzyżowanie i mutację. Domyślna szansa krzyżowania wynosi 90%. Bazowa mutacja to 0.25 przy nowej frazie i `mutation × 0.4` przy ewolucji. Gdy najlepszy wynik wyspy nie poprawia się przez 8 pokoleń, ten mnożnik rośnie do 1.5 i wraca do 1 po poprawie. Co 10 pokoleń najsłabszy osobnik wyspy jest zastępowany już ocenioną kopią lidera sąsiedniej wyspy, bez dodatkowej oceny, więc liczba ocen zostaje `populacja × (pokolenia + 1)`. Przy ewolucji przypięte frazy są dodatkową pulą do krzyżowania. Świeże generowanie ich nie używa. Zatrzymanie przez Cancel przerywa wyszukiwanie i nie zwraca częściowego banku.

Krzyżowanie wymienia fragmenty całych zdarzeń, więc dziedziczone są także centy. Przy blokadach, Keep rhythm lub Melody on rhythm zachowane są czasy, a wymieniane wysokości, centy i velocity odblokowanych nut. Mutacja może zmieniać klawisz i gen mikrotonowy; przy dozwolonej zmianie rytmu także długość lub podział nuty, wstawienie nowego początku, usunięcie nuty do pauzy, przesunięcie początku o krok siatki, połączenie sąsiednich nut i kopiowanie krótkiego fragmentu razem z przerwami. Naprawa ograniczeń następuje przed oceną potomka.

Końcowy wybór najpierw bierze po jednym liderze z wyspy, potem resztę rankingu, odrzucając frazy podobne powyżej 0.85. Jeśli zabraknie dostatecznie różnych wyników, pozostałe sloty otrzymują ocenione osobniki. Silnik nie tworzy wtedy nieocenionych mutacji i nie losuje nowych centów po zakończeniu GA. `SearchStats::microtonalEvaluations` liczy oceny osobników zawierających faktyczne niezerowe offsety; nie jest liczbą nut ani obietnicą, że każda wynikowa fraza będzie mikrotonowa.

## Riff, Motif i Melody

`phraseType` jest niezależny od trybu operacji Generate, Rhythm only i Melody on rhythm.

### Riff

Komórka ma długość jednego taktu lub całej frazy, jeżeli jest krótsza od taktu. Pierwsza komórka jest powielana wraz z początkami, długościami, wysokościami i centami. Przy aktywnej harmonii kolejne kopie są dostosowywane do akordów. Granice własnych akordów mogą dodatkowo dzielić nutę.

Krzyżowanie przecina komórkę, a mutacja zmienia jej zdarzenia. Pozostałe kopie są aktualizowane **przed oceną** potomka. W chromosomie nadal znajduje się cała fraza, dzięki czemu fitness widzi również przejścia między kopiami i powrót do początku pętli. Nie jest to dopiero końcowe powielenie zwycięzcy.

### Motif

Komórka ma maksymalnie pół taktu i maksymalnie jedną czwartą długości całej frazy, z dolną granicą wynikającą z siatki. Cała fraza jest podzielona na cztery części:

1. **A** — powracająca krótka komórka tematu.
2. **A′** — wariant, w którym druga połowa komórki jest skierowana o dwa półtony wyżej, po dopasowaniu do skali lub harmonii.
3. **B** — kontrast: odbicie konturu wokół pierwszej nuty komórki z przesunięciem o cztery półtony.
4. **A** — powrót tematu.

W dłuższej części może występować kilka kopii krótkiej komórki. Fitness uwzględnia powrót tematu, kontrast środka i powtarzalność rytmu. Kontur motywu wynika z rzeczywistych zdarzeń, a nie tylko z innej nazwy presetu.

### Melody

Melodia rozwija całą frazę bez przymusowego powielania jednej komórki. Inicjalizacja proponuje dłuższy łuk rejestru i zakończenie na dźwięku docelowym harmonii. W kolejnych pokoleniach ocena preferuje łuk, czytelną kadencję, małe kroki i mniejszą liczbę identycznych powtórzeń. Kontur i kadencja są preferencjami fitness, a nie niezmiennymi nutami narzuconymi każdemu wynikowi.

### Ewolucja rodzica i ograniczenia

Evolve dla Riff/Motif może przebudowywać kopie komórki, jeśli rodzic nie ma zablokowanych nut, Keep rhythm jest wyłączone, operacja nie jest Melody on rhythm, a Evolution jest większe od zera. Rodzic musi też zawierać materiał w pierwszej komórce. W pozostałych przypadkach pierwszeństwo mają blokady i rytm źródła; typ frazy nadal wpływa na ocenę, ale nie przepisuje chronionych czasów.

## Gęstość i swoboda harmoniczna w 2.6

Cel długości nuty to `max(grid, 0.25 × 2^(1.65 − 1.5 × activity))`, gdzie `activity = clamp(Density + 0.65 × (Arousal − 0.5), 0, 1)`. Cel liczby nut wynosi `length × (1 − Pause amount) / targetDuration`. Fitness karze względną odległość od tego celu z wagą 3.5. Gęstość nie jest dopisywaniem nut po optymalizacji: wpływa na inicjalizację i ocenę kolejnych pokoleń.

Przy dodatnim Jazziness słabe części taktu dopuszczają nuty skali poza akordem. Docelowy udział nut akordowych to `(0.8 dla Riff / 0.75 dla Motif / 0.6 dla Melody) − 0.25 × Jazziness`; przy Jazziness = 0 cel wynosi 1. Mocne części taktu dodatkowo premiują stabilne nuty. Poprawne mikrotonowe przejścia nadal otrzymują częściowy kredyt 0.75. Różnorodność wysokości ma cel zależny od Diversity; nadmiar kolejnych identycznych wysokości jest karany. Małe niezerowe kroki nadal są preferowane.

[Audyt pierwowzoru i pomiary przed/po](REFERENCE_AUDIT_2.6.md) wyjaśniają regresje, poprawki i pozostałe różnice względem Pythona.

## Koszt interwałów w centach

`intervalCost` przyjmuje bezwzględną wielkość rzeczywistego interwału. Kierunek nie zmienia kosztu. Uwzględnione są wszystkie kolejne nuty **oraz ostatnia → pierwsza**. Nie ma redukcji dużego skoku modulo oktawa.

Punkty krzywej są interpolowane liniowo. Pełne wartości co 50 centów:

| Interwał [c] | Koszt |
| ---: | ---: |
| 0 | 0.03 |
| 50 | 0.06 |
| 100 | 0.10 |
| 150 | 0.16 |
| 200 | 0.22 |
| 250 | 0.31 |
| 300 | 0.40 |
| 350 | 0.51 |
| 400 | 0.62 |
| 450 | 0.725 |
| 500 | 0.83 |
| 550 | 0.99 |
| 600 | 1.15 |
| 650 | 1.10 |
| 700 | 1.05 |
| 750 | 1.175 |
| 800 | 1.30 |
| 850 | 1.425 |
| 900 | 1.55 |
| 950 | 1.625 |
| 1000 | 1.70 |
| 1050 | 1.775 |
| 1100 | 1.85 |
| 1150 | 1.925 |
| 1200 | 2.00 |

Powyżej 1200 centów, dla `x = (abs(cents) − 1200) / 1200`, koszt wynosi `2 + 3x + x²`. Przykładowo 1500 c kosztuje 2.8125, 2400 c kosztuje 6, a 3600 c kosztuje 12. Kwinta ma niewielką preferencję względem trytonu, lecz nadal jest droższa od małego kroku.

Średni koszt jest mnożony przez `1.8 − 0.4 × Diversity − 0.6 × Weirdness`. Dodatkowo małe niezerowe kroki do 250 c otrzymują premię. Dzięki temu Diversity i Weirdness mogą dopuścić większy ruch, ale nie usuwają kosztu dużych skoków ani nie powodują utraty informacji o centach.

## Dodatkowa relacja do prymy

Oddzielna heurystyka ocenia współbrzmienie z prymą aktualnego własnego akordu, a przy braku własnej harmonii z głównym Root. Tutaj redukcja do klasy wysokości jest celowa: jest to relacja tonalna, nie wielkość skoku melodycznego.

| Odległość od prymy [półtony] | Koszt |
| ---: | ---: |
| 0 / 12 | 0 |
| 1 | 0.9 |
| 2 | 0.6 |
| 3 | 0.2 |
| 4 | 0.15 |
| 5 | 0.1 |
| 6 | 1.0 |
| 7 | 0.0 |
| 8 | 0.3 |
| 9 | 0.2 |
| 10 | 0.7 |
| 11 | 0.8 |

Wartości pośrednie, także mikrotonowe, są interpolowane. Średni koszt ma wagę `0.2 + 0.8 × Valence`. Ten składnik współistnieje z dopasowaniem do skali i akordów; nie jest samodzielnym modelem całej teorii harmonii.

## Mikrotony jako krótkie geny przejściowe

`microEnabled` jest przełącznikiem generowania. `tuning` wybiera paletę dopuszczalnych offsetów ±50 c i nie zastępuje wybranej Scale. W bezpośrednim API silnika tuning 0 przy włączonych mikrotonach oznacza paletę 24-EDO. Dawny parametr `microtones` nie wpływa na generację ani ewolucję; pozostaje wyłącznie dla zgodności starego pomocniczego `retuneRegion`.

Odblokowana mikrotonowa nuta wynikowa musi spełniać wszystkie warunki:

- jej długość nie przekracza `min(0.5, max(0.03125, clip.grid))` ćwierćnuty; przy zwykłych siatkach jest to `min(0.5, grid)`;
- nie jest pierwszą ani ostatnią nutą frazy;
- jej offset należy do wybranej palety;
- następna lub druga kolejna nuta jest kotwicą 12-TET, czyli ma `cents = 0`;
- rzeczywista odległość od kotwicy nie przekracza 250 c;
- kotwica zaczyna się nie wcześniej niż koniec nuty przejściowej; przerwa między nimi nie przekracza 0.5 ćwierćnuty.

Mutacja może zaproponować nowy mikrotonowy gen w pobliżu następnej kotwicy. Fitness widzi rzeczywiste centy, premiuje poprawnie rozwiązane nuty przejściowe, karze nierozwiązane oraz zbyt duży lub zbyt mały udział mikrotonów względem miękkiego celu **14% łącznego czasu brzmienia nut**. Udział to suma długości nut z niezerowymi centami podzielona przez sumę długości wszystkich nut. Dzięki temu osiem krótkich przejść po 0.25 ćwierćnuty w motywie nie jest traktowane tak samo jak osiem długich nut: przy 14 ćwierćnutach łącznego brzmienia daje to 14.3% czasu, nawet jeśli stanowią 8 z 24 zdarzeń. Nie oznacza to gwarancji dokładnie 14% w wyniku. Dla bardzo krótkiej frazy, określonych palet, harmonii i blokad poprawna mikrotonowa nuta może nie istnieć.

Krótka mikrotonowa nuta przejściowa może leżeć poza akordem także przy **Jazziness = 0**. Przykładowo D obniżone o 50 c z palety Bayati może prowadzić do kotwicy C lub E nad akordem C-dur. Dopasowanie harmoniczne nadal obejmuje zwykłe kotwice, a fitness uwzględnia harmonię całej frazy. Powielanie komórki i krzyżowanie nie dociągają takich genów automatycznie do dźwięków akordu przed sprawdzeniem ich czasu, palety i rozwiązania.

W składniku dopasowania harmonicznego zwykła nuta akordowa otrzymuje pełne zaliczenie, a poprawna krótka nuta przejściowa prowadząca do kotwicy należącej do jej akordu otrzymuje **0.75**. Pozostałe nuty nie dostają tego zaliczenia. Dzięki temu powtórzenie poprawnego przejścia w motywie nie powoduje takiej samej kary jak powtórzenie długiego, nierozwiązanego obcego tonu. Częściowa ocena nadal przechodzi przez pełny GA; nie wymusza liczby mikrotonów ani nie zmienia zwycięzcy po wyszukiwaniu.

Deterministyczna naprawa usuwa niewykonalne offsety. Jeśli odrzucona nuta przejściowa ma stać się zwykłą kotwicą przy własnej harmonii i Jazziness = 0, jej klawisz zostaje dopasowany do akordu. Naprawa nie losuje nowych centów po ocenie i nie skraca chronionego rytmu. Zablokowane nuty mają pierwszeństwo nad regułami generatora: zachowują również wcześniej zapisany, długi lub nietypowy offset.

Microtones OFF powoduje, że nowa generacja ma centy równe zeru, a Evolve zeruje offsety wszystkich odblokowanych nut rodzica. Zablokowane offsety są zachowane w dokumencie. Samo ustawienie flagi nie przepisuje istniejącego banku; dopiero uruchomienie operacji generowania/ewolucji tworzy nowy wynik. Warstwa odtwarzania może niezależnie od dokumentu odtwarzać materiał jako 12-TET przy wyłączonych mikrotonach.

Narzędzia edycji regionu nie dodają losowych offsetów. Zmieniona wysokość staje się kotwicą 12-TET, a istniejący offset może zostać zachowany wyłącznie, jeśli nadal spełnia ograniczenia. Nuty zablokowane i poza edytowanym zakresem pozostają nietknięte.

## Znaczenie przywróconych kontrolek

Wartości w poniższych wzorach mają zakres 0–1. Density jest ponownie widoczną kontrolką aktywności; historyczny Syncopation pozostaje dla zgodności parametrów.

- **Diversity** wspiera więcej różnych wysokości i nieco większy zakres propozycji mutacji. Umiarkowanie zmniejsza wagę kosztu interwałów; nie wyłącza powtarzalnej architektury riffu.
- **Jazziness** (`tension`) reguluje ścisłość dopasowania do skali i akordu oraz dopuszczanie ruchu chromatycznego. Własne akordy mogą zawierać dźwięki poza główną skalą.
- **Pause amount** (`space`) wpływa na prawdopodobieństwo pauz podczas inicjalizacji i docelowy udział ciszy w ocenie.
- **Dynamics** reguluje zróżnicowanie długości oraz zakresu wysokości, a nie poziom audio. Cel odchylenia `log2(duration)` wynosi `1.1 × Dynamics`; docelowy zakres frazy wynosi `min(High − Low, 4 + 6 × Dynamics + 8 × Arousal)` półtonów.
- **Valence** zwiększa wagę relacji konsonansowej do prymy, preferowany rejestr i udział ruchu w górę. Cel ruchu w górę wynosi `0.3 + 0.4 × Valence` wśród niezerowych interwałów wewnątrz frazy.
- **Arousal** reguluje aktywność nutową i zakres rejestru. Efektywna gęstość to `clamp(Density + 0.65 × (Arousal − 0.5), 0, 1)`. Docelowy rejestr to środek Low/High, powiększony o `10 × (Valence − 0.5) + 4 × (Arousal − 0.5)` półtonów.
- **Weirdness** zwiększa udział przesunięć względem pulsu i szansę szerszych skoków. Cel udziału offbeatów wynosi `clamp(Syncopation + 0.55 × (Weirdness − 0.1), 0, 1)`. Twarde ograniczenia nut mikrotonowych pozostają aktywne.
- **Evolution** (`mutation`) reguluje mutację rodzica i docelową odległość od niego. Rytm i blokady nadal mają pierwszeństwo.

Te cele współdziałają. Przykładowo wysoka Dynamics może sprzyjać większemu zakresowi, ale duży, przypadkowy skok nadal kosztuje; wysoka Weirdness nie zamienia każdej nuty w chromatyczny skok. Zmiana gałki zmienia rozkład propozycji i/lub preferencje oceny, a nie gwarantuje konkretnego stylu.

## Sprawdzenia automatyczne

`MusicalArchitectureTests.cpp` sprawdza:

- rzeczywiste wartości centów w koszcie, symetrię i karę za skoki większe niż oktawa;
- uwzględnienie ostatniej i pierwszej nuty pętli;
- zmianę fitness po zmianie samych centów oraz karę za brak rozwiązania;
- zależność miękkiego celu mikrotonów od łącznej długości ich brzmienia, a nie samej liczby zdarzeń;
- obecność mikrotonowych genów w ocenianych populacjach dla ośmiu seedów;
- poprawne mikrotonowe nuty przejściowe z palety Bayati nad własnym C-dur przy Jazziness = 0, dla Riff, Motif i Melody;
- obecność poprawnych przejść w zwycięskim Motif dla konfiguracji przykładu odsłuchowego: seed 41, C harmonic minor, `Cm:4 Fm:4 Ab:4 G7:4`, zakres 60–81, 24-EDO i Balanced;
- zgodność wyniku zwycięzcy z ostatnio raportowanym fitness, bez późniejszego losowania centów;
- powtarzalność pełnych nut, rytmu, velocity i centów dla tego samego seeda;
- brak wpływu dawnego procentowego Microtones na generację;
- wyzerowanie odblokowanych centów przy OFF i zachowanie blokad oraz chronionego rytmu;
- faktyczne powtórzenia komórki Riff, powrót i kontrast Motif oraz dłuższy kontur Melody;
- utrzymanie struktury riffu w nieograniczonej ewolucji;
- zmianę preferencji fitness po zmianie Dynamics, Valence, Arousal i Weirdness na porównywalnych parach fraz.

Dotychczasowe testy nadal sprawdzają pełną liczbę pokoleń, ocen i krzyżowań, elityzm, anulowanie, metra, własną harmonię, blokady i zgodność stanu silnika. Bieżący engine-only build przeszedł **AddressSanitizer i UndefinedBehaviorSanitizer**, a CTest zakończył się **1/1 PASS**. Są to sprawdzenia implementacji i jej ograniczeń; nie zastępują odsłuchu ani oceny muzycznej przez użytkownika.
