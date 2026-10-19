# Genetic Composer 2.2 w Studio One

## Instalacja na tym Macu

1. Rozpakuj `dist/GeneticComposer-2.2.0-macOS-arm64.zip`.
2. W Finderze wybierz **Idź → Idź do folderu** i wpisz `~/Library/Audio/Plug-Ins/VST3/`. Jeśli folder nie istnieje, utwórz go.
3. Skopiuj do niego cały pakiet **Genetic Composer.vst3** z folderu VST3 w archiwum.
4. Uruchom ponownie Studio One lub ponownie przeskanuj wtyczki. Szukaj **Genetic Composer** w instrumentach.

Wtyczka ma nowy identyfikator i nie zastępuje poprzedniej wersji w dawnych projektach. Wersja w paczce jest przeznaczona dla Apple Silicon. Nie jest publicznym, notaryzowanym instalatorem.

Wersja 2.2 zachowuje identyfikator wtyczki i odczytuje banki oraz projekty wersji 2.0 i 2.1. Funkcje rozwijania frazy obsługuje pięć zakładek przy piano rollu: **Region**, **Harmony**, **Groove**, **Arrange** i **Combine**. Przycisk **? Help** otwiera instrukcję wewnątrz wtyczki, z rozdziałami oraz wyborem języka polskiego lub angielskiego. Nie wymaga internetu.

## Pierwszy motyw

1. Przeciągnij Genetic Composer na nową ścieżkę instrumentu.
2. Włącz odsłuch/monitorowanie ścieżki. **Internal sound** daje prosty dźwięk do sprawdzenia melodii.
3. Kliknij dwukrotnie jeden z 12 wariantów albo naciśnij **Play**. Przyciskiem **Generate bank** utworzysz nowy bank.
4. Znak **+** przypina udany wariant i chroni go przed zastąpieniem.
5. Zaznacz wybrany motyw i użyj **Evolve selected**. Gałka **Evolution** reguluje stopień zmian, a **Keep rhythm** zachowuje rytm.
6. Shift-klik na nucie blokuje ją. Możesz zostawić początek melodii i zmieniać pozostałe nuty. Zablokowane nuty mają bursztynowy kolor.
7. Przeciągnij **Drag MIDI** na ścieżkę z wybranym instrumentem. Możesz też użyć **Export...** i wczytać zapisany plik MIDI do Studio One.

## Poprawianie jednego fragmentu

Przeciągnij po linijce z numerami taktów nad nutami, aby zaznaczyć fragment. W zakładce **Region** wybierz:

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

Własna harmonia ma pierwszeństwo przed gotową progresją. **Scale snap** jest wtedy pomijany, aby wyjściowe MIDI nie zmieniało nut należących do akordów spoza wybranej skali. **Clear** usuwa własną harmonię i przywraca działanie wybranej progresji oraz Scale snap.

## Groove i akcenty

1. Otwórz **Groove**. Użyj **Import groove MIDI**, aby pobrać wzorzec z pliku, albo wybierz wariant i kliknij **Use selected**.
2. Wybierz melodię, do której chcesz zastosować wzorzec.
3. Ustaw **Timing** — siłę przeniesienia odchyleń czasowych — oraz **Velocity** — siłę przeniesienia akcentów.
4. Kliknij **Apply groove**. W razie potrzeby cofnij wynik przez Undo.

Referencja jest przechowywana osobno; jej import nie zastępuje melodii. Zablokowane nuty pozostają bez zmian. W dolnym pasie velocity możesz też kliknąć lub przeciągnąć, aby ręcznie zmienić głośność najbliższej czasowo, odblokowanej nuty. **Swing** i **Gate** nadal działają dodatkowo podczas odsłuchu i eksportu.

## Dłuższa fraza i łączenie wariantów

**Arrange → Preview arrangement** tworzy sekwencję **A–A′–B–A″**: oryginalny motyw, rozwinięcie, odpowiedź i zakończenie. Wynik ma cztery razy większą długość. Przykładowo z czterech taktów powstaje szesnaście. Materiał wejściowy może mieć najwyżej 32 ćwierćnuty i 128 nut; także wynik po przekształceniach musi mieścić się w limicie 512 nut. Przekroczenie limitu powoduje komunikat zamiast obcięcia aranżacji.

W **Combine** wybierz numery wariantów dla **Rhythm**, **Pitches** i **Accents**, a następnie **Preview combination**. Rytm dostarcza początki i długości nut; pozostałe źródła dostarczają przebiegu wysokości i akcentów, dopasowanych do tego rytmu. Nie muszą mieć takiej samej liczby nut.

Obie funkcje tworzą tymczasowy podgląd. Możesz go odsłuchać, porównać przez A/B i wyeksportować MIDI. **Keep preview** zapisuje wynik w wybranym slocie; **Discard** porzuca go i zachowuje dotychczasowy bank. Podczas podglądu edycja czeka na tę decyzję. Niezatwierdzony podgląd nie jest zapisywany w projekcie DAW ani w pliku banku.

Aby wyeksportować samą część aranżacji, zaznacz ją na linijce piano rolla i wybierz **Export range**. Eksportowany fragment zaczyna się od zera, zachowuje długość zaznaczenia i przycina nuty przecinające jego granice. **Drag MIDI** i zwykły **Export...** nadal eksportują całą wyświetlaną frazę.

## Synchronizacja i routing

**Follow DAW** uruchamia i zatrzymuje frazę razem z transportem hosta oraz dopasowuje jej pozycję do pozycji utworu. W tym trybie ręczny przycisk Play jest nieaktywny. Bez Follow DAW możesz odsłuchiwać frazy niezależnie; tempo hosta nadal jest używane, jeśli host je udostępnia.

Do routingu na żywo utwórz drugi instrument i wybierz Genetic Composer jako źródło jego danych nutowych, jeśli Twoja wersja hosta udostępnia to źródło w selektorze wejścia instrumentu. Włącz monitorowanie odbiorcy i wyłącz **Internal sound** w generatorze. Nazwy i położenie tych opcji zależą od wersji Studio One/Studio Pro. Ten wariant połączenia wymaga jeszcze ręcznej weryfikacji w hostcie; eksport pliku MIDI nie zależy od routingu na żywo.

Klawisze MIDI **48–59** wyzwalają warianty podczas trzymania klawisza. **60–127** transponują; 60 oznacza oryginalną wysokość. Nazwy oktaw mogą być inne w Studio One niż we wtyczce — kieruj się numerem MIDI.

Projekt DAW zapisuje zatwierdzone melodie, blokady, przypięte warianty, parametry, własną harmonię, referencję groove i kopię A. **Save bank** dodatkowo zapisuje niezależny plik `.genetic`. **Undo/Redo** obejmuje do 24 edycji dokumentu, w tym edycje nut, importy, generacje, harmonię i referencję groove. Parametry automatyzacji oraz Capture A nie są częścią tej historii. Wczytanie projektu lub banku zatrzymuje ręczny odsłuch.

## Skróty i granice

**Spacja** — Play/Stop, **Cmd+Z** — Undo, **Cmd+Shift+Z** — Redo, **Esc** — Stop all, **L** lub Shift-klik — blokada zaznaczonej nuty. Skróty edycji działają, gdy fokus jest w edytorze; podczas wpisywania akordów klawiatura służy do wpisywania tekstu.

Bank zawiera 12 wariantów, każdy do 512 nut i 128 ćwierćnut. Import obsługuje MIDI z czasem zapisanym w tickach muzycznych; import głównej melodii zachowuje polifonię, ale generator jest ukierunkowany na pojedynczą linię melodyczną. Prosty dźwięk wewnętrzny służy do odsłuchu. Generowanie i transformacje nie gwarantują konkretnego stylu ani jakości muzycznej — porównuj wyniki w kontekście utworu.

## Nowy wygląd i aktywność

W wersji 2.2 górny panel **MIDI** pokazuje wysyłane zdarzenia nutowe, a **OUT** poziom audio wewnętrznego instrumentu. MIDI pozostaje widoczne po wyłączeniu Internal sound. Klawisze piano rollu reagują na wysyłane wysokości; aktywny wariant ma wskaźnik pozycji. Przy odsłuchu A lub oczekiwaniu na inny wariant playhead w edytowanej frazie B jest ukryty, aby nie sugerować, że to właśnie ją słyszysz.

**Motion** w dolnym pasku ogranicza animację logo, przewijanie historii MIDI oraz płynne przejścia kontrolek. Wskazania transportu i poziomu pozostają aktywne. Przełącznik dotyczy otwartego okna edytora. Instrukcja pod **?** nadal zawiera rozdziały po polsku i angielsku.
