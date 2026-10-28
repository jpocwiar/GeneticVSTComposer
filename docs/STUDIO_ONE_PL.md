# Genetic Composer 2.6 w Studio One

## Instalacja na tym Macu

1. Rozpakuj `dist/GeneticComposer-2.6.0-macOS-arm64.zip`.
2. W Finderze wybierz **Idź → Idź do folderu** i wpisz `~/Library/Audio/Plug-Ins/VST3/`. Jeśli folder nie istnieje, utwórz go.
3. Skopiuj do niego cały pakiet **Genetic Composer.vst3** z folderu VST3 w archiwum.
4. Uruchom ponownie Studio One lub ponownie przeskanuj wtyczki. Szukaj **Genetic Composer** w instrumentach.

Wtyczka ma odrębny identyfikator względem pierwotnej wersji 1.x i nie zastępuje jej w dawnych projektach. Wersja w paczce jest przeznaczona dla Apple Silicon. Nie jest publicznym, notaryzowanym instalatorem.

Wersja 2.6 zachowuje identyfikator wtyczki oraz istniejące identyfikatory parametrów i odczytuje wcześniejsze banki 2.x. **Melody** i **Chords** są dwiema głównymi kartami wewnątrz jednego interfejsu; przełączenie karty nie otwiera dodatkowego okna i nie zatrzymuje odsłuchu.

Na Melody dostępne są zakładki **Edit region**, **Harmony**, **Groove**, **Arrange**, **Combine**, **Micro** i **Search**. Chords służy do tworzenia progresji, układania przewrotów i eksportowania akordów. Przycisk **? Help** otwiera instrukcję z rozdziałami po polsku lub angielsku, bez połączenia z internetem.

## Pierwszy motyw

1. Przeciągnij Genetic Composer na nową ścieżkę instrumentu.
2. Włącz odsłuch/monitorowanie ścieżki. **Internal sound** daje prosty dźwięk do sprawdzenia melodii.
3. Na **Melody** wybierz Root, Scale, Length, Meter i **Phrase type: Riff / Motif / Melody**, a potem **Generate melody bank**. Kliknij wariant i trójkąt **Play pod piano rollem**; dwuklik karty również uruchamia odsłuch. Kwadrat **Stop** go zatrzymuje.
4. Znak **+** przypina udany wariant i chroni go przed zastąpieniem.
5. Zaznacz wybrany motyw i użyj **Evolve selected**. Gałka **Evolution** reguluje stopień zmian, a **Keep rhythm** zachowuje rytm.
6. Shift-klik na nucie blokuje ją. Możesz zostawić początek melodii i zmieniać pozostałe nuty. Zablokowane nuty mają bursztynowy kolor.
7. Przeciągnij **piano roll** albo **Drag MIDI** na ścieżkę z wybranym instrumentem. Możesz też użyć **Export...** i wczytać zapisany plik MIDI do Studio One.

## Riff, motyw i charakter melodii

**Phrase type** określa strukturę nowego pomysłu:

| Typ | Charakter frazy |
| --- | --- |
| Riff | Krótki powtarzany rdzeń, dopasowywany do akordów |
| Motif | Rozpoznawalny pomysł z kontrolowanymi wariantami |
| Melody | Swobodniejsza linia z rozwinięciem i zakończeniem |

Przywrócono siedem oryginalnych nazw parametrów muzycznych oraz Evolution. To preferencje nowej funkcji fitness, a nie odtworzenie dawnych wag 1:1 ani obiektywne miary nastroju.

| Pokrętło | Znaczenie |
| --- | --- |
| Diversity | Różnorodność wysokości i interwałów |
| Dynamics | Ruch melodii i zmiany rejestru; nie głośność wyjścia |
| Jazziness | Swoboda chromatyczna i napięcie harmoniczne |
| Valence | Preferencja jaśniejszych współbrzmień i kierunku melodii w górę |
| Arousal | Energia rytmu, szybszy ruch oraz szerszy i wyższy rejestr |
| Weirdness | Niespodziewane pozycje rytmiczne i większe skoki |
| Pause Amount | Miejsce na pauzy między nutami |
| Evolution | Wielkość zmian przy Evolve selected |

**Fit to scale**, widoczne pod piano rollem, jest domyślnie wyłączone — tak jak w pierwotnej wtyczce. Włączone dopasowuje wysokości melodii do Scale na żywo, po transpozycji, podczas odtwarzania i eksportu. Nie przepisuje banku i zachowuje mikrotonowe odchylenia wybrane przez algorytm. Działa również przy własnej harmonii, więc może zmienić chromatyczne dźwięki pożyczonych akordów. Wyłącz je, by usłyszeć oryginalne wysokości generatora.

## Poprawianie jednego fragmentu

Przeciągnij po linijce z numerami taktów nad nutami, aby zaznaczyć fragment. W zakładce **Edit region** wybierz:

- **Simplify** — uproszczenie frazy.
- **More movement** — zwiększenie ruchu w zaznaczonym fragmencie.
- **Answer** — przekształcenie fragmentu w odpowiedź na motyw.
- **Resolve** — skierowanie zakończenia w stronę dźwięku docelowego wynikającego z harmonii lub skali.

Operacja obejmuje odblokowane nuty, których początki mieszczą się w zaznaczonym zakresie. Nuta zaczynająca się przed zaznaczeniem pozostaje zachowana, nawet jeśli jej koniec wpada do środka zakresu. **Whole phrase** zaznacza całą frazę. **Undo** cofa operację.

Przykład: zaznacz ostatni takt dobrego motywu, kliknij **Resolve**, a następnie porównaj obie wersje przez A/B.

## Porównanie A/B i zmiana wariantu w pętli

1. Wybierz motyw, który chcesz zachować jako punkt odniesienia, i kliknij **Capture A**.
2. Edytuj wariant, wygeneruj rozwinięcie albo utwórz podgląd aranżacji lub kombinacji.
3. Użyj **Hear A / Hear B**, aby porównać zapisany punkt odniesienia z bieżącą wersją.

Przy włączonym **Switch at loop** przełączenie A/B lub kliknięcie innego wariantu podczas odtwarzania czeka do początku następnej pętli. Możesz od razu edytować wybrany wariant; oznaczenia odtwarzanej i oczekującej wersji wskazują, co słychać. Wyłączenie tej opcji umożliwia natychmiastową zmianę. Klawisze MIDI wyzwalają warianty od razu, niezależnie od tej opcji.

**Capture A** zapisuje jedną kopię frazy. Ponowne użycie zastępuje poprzednie A. Ten punkt odniesienia jest zachowywany w projekcie i banku; nie stanowi oddzielnej pozycji historii Undo.

## Harmonia utworu

W zakładce **Harmony** wpisz np. `Cm:4 Fm:2 G7:2` i kliknij **Apply chords**. Liczba po dwukropku to długość w ćwierćnutach: powyższa progresja trwa osiem ćwierćnut. Możesz w ten sposób zmieniać akordy w środku taktu. Progresja powtarza się podczas generowania dłuższej frazy.

**Import harmony MIDI** wczytuje osobną referencję i próbuje oszacować jej akordy. Nie zastępuje melodii w banku. Sprawdź wynik w tekście i na pasku akordów; automatyczna interpretacja wieloznacznych współbrzmień może wymagać poprawki. Zaimportowanie harmonii nie przepisuje istniejącej melodii — użyj jej podczas kolejnej generacji lub odpowiedniej edycji fragmentu.

Własna harmonia ma pierwszeństwo przed gotową progresją. **Fit to scale** pozostaje dostępne: włączone dopasowuje bazowe wysokości melodii do Scale także dla akordów spoza tej skali. Wyłącz je, aby zachować oryginalne nuty chromatyczne. **Clear** usuwa własną harmonię i przywraca wybraną progresję.

## Kreatywne progresje i osobne MIDI akordów

Wybierz główną kartę **Chords**. Skróty **Chord composer...** w Chord text również przełączają na tę kartę. Piano roll akordów, ich nazwy, odsłuch i eksport znajdują się wewnątrz tego samego interfejsu.

1. **Root / Scale** ustaw na dowolnej karcie; są wspólne. **Meter** ustaw na Melody. Karta Chords ma własne **Length**, domyślnie 4 takty. **Follow melody** jawnie wiąże długość z Melody. Zmiana ustawień nie przepisuje istniejących nut.
2. Ustaw **Originality**. Niska wartość wybiera typowe progresje funkcyjne. Wyższe wartości stopniowo dodają zamiany diatoniczne, septymy, akordy pożyczone z równoległych skal, dominanty wtrącone i chromatyczne podejścia prowadzące do następnego akordu. To poziom swobody harmonicznej, nie liczba pokoleń algorytmu.
3. **Harmonic rhythm** określa częstotliwość zmian: **Two bars**, **One bar** lub **Half bar**. **Resolve to tonic** kończy progresję na tonice.
4. **New chords only** zmienia tylko akordy. **Melody from these chords** zachowuje widoczne akordy i generuje melodie na całą długość sekwencji. **New chords + melody** tworzy oba elementy. Dwie ostatnie akcje wracają na Melody i włączają With chords; przypięte warianty pozostają zachowane. **Generate melody bank** na Melody korzysta z aktywnej harmonii, ale długości ustawionej na Melody. **Evolve selected** rozwija wybrany wariant.
5. Wybierz układ **Smooth inversions**, **Root position** lub **Open voicings**, ustaw preferowany rejestr i kliknij **Revoice**. Pierwszy tryb ogranicza skoki głosów, drugi utrzymuje prymę w basie, trzeci rozsuwa głosy. Revoice zachowuje akordy i ich długości, zmieniając przewroty oraz oktawy. Oznaczenie `Cm/Eb` oznacza akord c-moll z Es jako najniższym dźwiękiem.
6. Trójkąt **Play pod akordowym piano rollem** odtwarza same akordy. **Play together** odtwarza je razem z wybraną melodią; ten sam odsłuch uzyskasz przez **With chords + Play** na Melody. Przeciągnij **piano roll akordów** albo **Drag chord MIDI** na osobną ścieżkę instrumentu. **Export...** zapisuje dokładnie te nuty. Melodię przeciągnij z Melody i ustaw obie partie od tego samego miejsca utworu.

Originality, rytm zmian i Resolve działają przy następnym **New chords only** lub **New chords + melody**. Samo przesunięcie suwaka nie przepisuje istniejącej progresji. Zmianę rejestru lub sposobu układania głosów zastosujesz przez **Revoice** albo nową generację. Akordy wpisane ręcznie lub odczytane z pliku harmonii również można tutaj przestawić i wyeksportować.

MIDI akordów zawiera dokładnie pokazane przewroty i pełne długości akordów. Nie stosuje Swing, Gate ani Fit to scale melodii. Zawsze używa zwykłego stroju **12-TET na kanale MIDI 1**, bez pitch bend i konfiguracji MPE — także przy włączonych mikrotonach lub MPE dla melodii. Odsłuch akordów używa osobnej wewnętrznej warstwy 12-TET, więc pitch bend/MPE melodii ich nie odstraja. **With chords / Play together dotyczy dźwięku wewnętrznego**: nuty akordów nie są dodawane do wyjścia MIDI melodii. Do instrumentu na innej ścieżce wyeksportuj je osobno.

Generator progresji korzysta z reguł harmonicznych i losowania o prawdopodobieństwach zależnych od Originality. Układ przewrotów jest dobierany dla całej sekwencji, tak aby ograniczyć ruch głosów. Właściwe melodie nadal powstają przez pełny algorytm genetyczny z ustawieniami populacji, pokoleń, krzyżowania i mutacji z **Search**.

Maksimum to 64 akordy i 128 ćwierćnut. Gdy wybrany rytm zmian wymaga ponad 64 akordów, sąsiednie odcinki są łączone; długość frazy zostaje zachowana. Pentatoniki korzystają z akordów pokrewnego dur lub moll, skala chromatyczna z dur, a Saba z moll dla zwykłego wyjścia akordowego.

**Undo/Redo** obejmuje generowanie harmonii i Revoice. Projekt DAW oraz **Save bank** zachowują zarówno sekwencję akordów, jak i dokładne nuty przewrotów. Po ponownym otwarciu otrzymujesz zapisany układ, nawet jeśli później zmieniono ustawienia rejestru. Starsze banki z własną harmonią otrzymują przewroty automatycznie, ponieważ wcześniej ich nie zapisywały.

## Groove i akcenty

1. Otwórz **Groove**. Użyj **Import groove MIDI**, aby pobrać wzorzec z pliku, albo wybierz wariant i kliknij **Use selected**.
2. Wybierz melodię, do której chcesz zastosować wzorzec.
3. Ustaw **Timing** — siłę przeniesienia odchyleń czasowych — oraz **Velocity** — siłę przeniesienia akcentów.
4. Kliknij **Apply groove**. W razie potrzeby cofnij wynik przez Undo.

Referencja jest przechowywana osobno; jej import nie zastępuje melodii. Zablokowane nuty pozostają bez zmian. Po włączeniu **Edit notes** w dolnym pasie velocity możesz też kliknąć lub przeciągnąć, aby ręcznie zmienić głośność najbliższej czasowo, odblokowanej nuty. **Swing** i **Gate** nadal działają dodatkowo podczas odsłuchu i eksportu.

## Dłuższa fraza i łączenie wariantów

**Arrange → Preview arrangement** tworzy sekwencję **A–A′–B–A″**: oryginalny motyw, rozwinięcie, odpowiedź i zakończenie. Wynik ma cztery razy większą długość. Przykładowo z czterech taktów powstaje szesnaście. Materiał wejściowy może mieć najwyżej 32 ćwierćnuty i 128 nut; także wynik po przekształceniach musi mieścić się w limicie 512 nut. Przekroczenie limitu powoduje komunikat zamiast obcięcia aranżacji.

W **Combine** wybierz numery wariantów dla **Rhythm**, **Pitches** i **Accents**, a następnie **Preview combination**. Rytm dostarcza początki i długości nut; pozostałe źródła dostarczają przebiegu wysokości i akcentów, dopasowanych do tego rytmu. Nie muszą mieć takiej samej liczby nut.

Obie funkcje tworzą tymczasowy podgląd. Możesz go odsłuchać, porównać przez A/B i wyeksportować MIDI. **Keep preview** zapisuje wynik w wybranym slocie; **Discard** porzuca go i zachowuje dotychczasowy bank. Podczas podglądu edycja czeka na tę decyzję. Niezatwierdzony podgląd nie jest zapisywany w projekcie DAW ani w pliku banku.

Aby wyeksportować samą część aranżacji, zaznacz ją na linijce piano rolla i wybierz **Export range**. Eksportowany fragment zaczyna się od zera, zachowuje długość zaznaczenia i przycina nuty przecinające jego granice. **Drag MIDI** i zwykły **Export...** nadal eksportują całą wyświetlaną frazę.

## Mikrotony (opcjonalnie)

Na Melody otwórz zakładkę **Micro**. Osobny przełącznik **Micro** domyślnie jest wyłączony: melodia używa wtedy zwykłego stroju 12-tonowego. Po włączeniu przesunięcia mikrotonowe stają się częścią materiału optymalizowanego przez algorytm: wyszukiwanie wybiera krótkie dźwięki przejściowe, ich czas trwania i rozwiązania, a krzyżowanie oraz mutacja mogą je zmieniać.

Paleta oferuje **Quarter-tone inflections**, **Maqam Rast**, **Maqam Bayati**, **Maqam Saba**, **Neutral blues pentatonic** i **Quarter tones (24-EDO)**. Są to źródła możliwych odcieni dźwięków przejściowych; nie zastępują głównej **Scale** ani nie oznaczają automatycznie pełnego wykonania tradycyjnego maqamu. Wybór Scale pozostaje aktywny. Nazwy nut są angielskie: B to polskie H, a Bb to polskie B.

Nie ma suwaka ilości mikrotonów, zbiorczego Apply/Clear ani ręcznych skrótów zmiany centów. Użyj **Generate melody bank** lub **Evolve selected**, aby algorytm szukał nowych rozwiązań. Nie jest to automatyczne przestrojenie wszystkich wystąpień danej klasy wysokości już po generowaniu.

W piano rollu nuty mikrotonowe są fioletowe i leżą między wierszami: `E4-50` to E4 obniżone o 50 centów. **Play** gra dokładne wysokości. **Fit to scale** dopasowuje bazową wysokość do wybranej Scale, zachowując mikrotonowe odchylenie. Przy **With chords / Play together** akordy pozostają w 12-TET na osobnej warstwie wewnętrznego dźwięku.

### Wyjście MIDI w Studio One

MIDI 1.0 nie ma numerów nut dla ćwierćtonów, dlatego wtyczka wysyła mikrotony jako pitch bend:

- **Pitch bend (mono)** — tryb domyślny. Wszystkie nuty idą na kanale 1, a przed każdą wysyłany jest pitch bend. Działa z prawie każdym instrumentem przy jednogłosowych riffach. Ustaw w instrumencie zakres pitch bend równy wartości **Bend** we wtyczce (domyślnie ±2); na początku odtwarzania wtyczka wysyła go też jako RPN 0, ale nie każdy instrument to odczytuje. Gdy nuty na siebie zachodzą, nowy pitch bend przesuwa też poprzednią nutę i jej wybrzmienie.
- **MPE** — każda nuta na własnym kanale (2–16), zakres ±48 na nutę. Wybierz ten tryb dla instrumentów obsługujących MPE, akordów, nakładających się nut i długich wybrzmień. W Studio One otwórz menu instrumentu odbierającego (przy nazwie wtyczki) i włącz **Enable MPE**; włącz też tryb MPE w samym instrumencie, jeśli go ma.

**Drag MIDI** i **Export...** zapisują te same komunikaty: konfigurację na początku pliku i pitch bend przed każdą nutą. Dla jednogłosowego riffu najprościej przeciągnąć plik w trybie **Pitch bend (mono)** — pitch bend trafi do partii jako zwykłe zdarzenia pitch bend. Czy Studio One zachowuje kanały MPE przy przeciąganiu pliku, nie zostało jeszcze sprawdzone; w razie problemów użyj routingu na żywo do instrumentu z włączonym MPE.

Przy włączonym **Micro**, **Import MIDI** zamienia pitch bend na początku każdej nuty na jej przesunięcie mikrotonowe. Zakres odczytywany jest z RPN 0 lub z konfiguracji MPE; bez nich przyjmowane jest ±2. Wyłączenie **Micro** przywraca zwykłe MIDI 12-tonowe: pitch bend wraca do środka, a strefa MPE jest kasowana.

## Synchronizacja i routing

**Follow DAW** uruchamia i zatrzymuje frazę razem z transportem hosta oraz dopasowuje jej pozycję do pozycji utworu. Kliknięcie Play włącza brzmienie wewnętrzne i wyłącza Follow DAW. Bez Follow DAW możesz odsłuchiwać frazy niezależnie; tempo hosta nadal jest używane, jeśli host je udostępnia.

Do routingu na żywo utwórz drugi instrument i wybierz Genetic Composer jako źródło jego danych nutowych, jeśli Twoja wersja hosta udostępnia to źródło w selektorze wejścia instrumentu. Włącz monitorowanie odbiorcy i wyłącz **Internal sound** w generatorze. Nazwy i położenie tych opcji zależą od wersji Studio One/Studio Pro. Ten wariant połączenia wymaga jeszcze ręcznej weryfikacji w hostcie; eksport pliku MIDI nie zależy od routingu na żywo.

Klawisze MIDI **48–59** wyzwalają warianty podczas trzymania klawisza. **60–127** transponują; 60 oznacza oryginalną wysokość. Nazwy oktaw mogą być inne w Studio One niż we wtyczce — kieruj się numerem MIDI.

Projekt DAW zapisuje zatwierdzone melodie, blokady, przypięte warianty, parametry, własną harmonię, dokładne przewroty akordów, referencję groove i kopię A. **Save bank** dodatkowo zapisuje niezależny plik `.genetic`. **Undo/Redo** obejmuje do 24 edycji dokumentu, w tym edycje nut, importy, generacje, harmonię, Revoice i referencję groove. Parametry automatyzacji oraz Capture A nie są częścią tej historii. Wczytanie projektu lub banku zatrzymuje ręczny odsłuch.

## Skróty i granice

**Spacja** — Play/Stop, **Cmd+Z** — Undo, **Cmd+Shift+Z** — Redo, **Esc** — Stop all, **L** lub Shift-klik — blokada zaznaczonej nuty. Skróty edycji działają, gdy fokus jest w edytorze; podczas wpisywania akordów klawiatura służy do wpisywania tekstu.

Bank zawiera 12 wariantów, każdy do 512 nut i 128 ćwierćnut. Import obsługuje MIDI z czasem zapisanym w tickach muzycznych; import głównej melodii zachowuje polifonię, ale generator jest ukierunkowany na pojedynczą linię melodyczną. Prosty dźwięk wewnętrzny służy do odsłuchu. Generowanie i transformacje nie gwarantują konkretnego stylu ani jakości muzycznej — porównuj wyniki w kontekście utworu.

## Nowy wygląd i aktywność

W wersji 2.6 panel **MIDI** w dolnym pasku pokazuje wysyłane zdarzenia nutowe, a **OUT** poziom audio wewnętrznego instrumentu. MIDI pozostaje widoczne po wyłączeniu Internal sound. Klawisze piano rollu reagują na wysyłane wysokości; aktywny wariant ma wskaźnik pozycji. Przy odsłuchu A lub oczekiwaniu na inny wariant playhead w edytowanej frazie B jest ukryty, aby nie sugerować, że to właśnie ją słyszysz.

**Motion** w dolnym pasku ogranicza animację logo, przewijanie historii MIDI oraz płynne przejścia kontrolek. Wskazania transportu i poziomu pozostają aktywne. Przełącznik dotyczy otwartego okna edytora. Instrukcja pod **?** nadal zawiera rozdziały po polsku i angielsku.


## Odsłuch i przeciąganie MIDI melodii

Trójkąt **Play pod piano rollem** uruchamia odsłuch, włącza Internal sound i wyłącza Follow DAW. W dolnym pasku wybierzesz Soft keys, Sine albo Warm organ. Jeśli Level był zerowy, Play przywraca poziom 65%. Kwadrat **Stop** kończy odsłuch. Dwuklik karty lub piano rollu również uruchamia odtwarzanie.

Po utworzeniu akordów na Chords włącz **With chords**, aby usłyszeć obie partie naraz. Bez aktywnej sekwencji opcja pozostaje nieaktywna. **Play together** na Chords uruchamia ten sam wspólny odsłuch. Jest to połączenie wewnętrznych warstw dźwięku; MIDI melodii i MIDI akordów nadal przeciąga się oddzielnie.

Chwyć piano roll i przeciągnij go na ścieżkę instrumentu. Eksport zawiera całą wyświetlaną frazę lub podgląd, także Swing, Gate, metrum i ewentualne komunikaty strojenia mikrotonowego. Zwykły klik nie dodaje nut. Shift-klik zachowuje blokowanie nut, a linijka taktów służy do zaznaczania zakresu. Ręczne rysowanie i edycję włącza opcjonalny przełącznik **Edit notes**. **Export range** zapisuje zaznaczony fragment, zwykłe przeciąganie całą frazę.

## Metrum, progresje i rzeczywista optymalizacja

**Meter** ma 27 gotowych opcji. Możesz wpisać własne, np. `11/8`: licznik 1–32, mianownik 1, 2, 4, 8, 16 lub 32. Metrum obowiązuje przy nowym generowaniu; Evolve zachowuje metrum źródła. Maksimum 128 ćwierćnut na frazę może wymagać zmniejszenia Length.

**Harmony** zawiera 25 progresji oraz Scale only. Stopnie są względne do wybranej skali, jeden akord na takt; sekwencja się powtarza. **Custom chords...** otwiera wpisywanie akordów i długości, np. `Cm:4 Ab:4 Eb:4 Bb:4`, a **Chord composer...** przełącza na główną kartę Chords do generowania harmonii i eksportu przewrotów. Liczby w tekście są ćwierćnutami, nie taktami. Wybór presetu usuwa aktywną własną harmonię.

**Quick**: 64 osobniki × 40 pokoleń. **Balanced**: 128 × 100. **Deep**: 256 × 300. W **Search** przełącznik **Custom budget** udostępnia 16–512 osobników i 1–2000 pokoleń. **Crossover** określa szansę krzyżowania, domyślnie 90%.

Każde pokolenie jest oceniane funkcją fitness. Algorytm wybiera rodziców turniejowo, zachowuje dwa najlepsze osobniki, krzyżuje fragmenty i mutuje potomstwo. Blokady i Keep rhythm chronią czas nut; wtedy krzyżowane są wysokości i velocity. Panel pokazuje faktyczne pokolenia, liczbę ocen, krzyżowań, fitness i czas. Populacja początkowa też jest oceniana: 128 × 100 oznacza 12 928 ocen.

To nowy silnik z inną funkcją oceny, nie kopia pierwotnego kodu. Stary miał 128 osobników, 100 pokoleń i krzyżowanie 90%; nowy zapisuje oceny na czas pokolenia zamiast wielokrotnie przeliczać je podczas selekcji i sortowania. Dlatego może być szybki mimo pełnego przebiegu. Więcej pokoleń i wyższy fitness nie gwarantują lepszego muzycznie pomysłu. W instrukcji wewnętrznej rozdział 10 wyjaśnia te ustawienia.

## Gęstość i porównanie z pierwowzorem w 2.6

**Density** bezpośrednio reguluje aktywność nutową. **Arousal** dodatkowo wpływa na aktywność i rejestr, a **Pause amount** na udział ciszy. Jeśli riff jest zbyt pusty, zwiększ Density lub zmniejsz Pause amount. Mutacje mogą teraz wstawiać nuty, tworzyć pauzy, przesuwać początki i kopiować krótkie fragmenty. Statystyki Search po zakończeniu nadal pokazują rzeczywiste pokolenia, oceny oraz czas. Pełne porównanie kodu i pomiarów: [REFERENCE_AUDIT_2.6.md](REFERENCE_AUDIT_2.6.md).
