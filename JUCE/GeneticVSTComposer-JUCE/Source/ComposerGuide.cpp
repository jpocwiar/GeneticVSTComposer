#include "ComposerGuide.h"
#include "EditorFormat.h"

ComposerGuide::ComposerGuide() {
        addAndMakeVisible(title);
        title.setText("Instrukcja / Guide", juce::dontSendNotification);
        title.setFont(font(23, true));
        title.setColour(juce::Label::textColourId, palette::text);
        addAndMakeVisible(language);
        language.addItemList({"Polski", "English"}, 1);
        language.setSelectedId(1, juce::dontSendNotification);
        language.onChange = [this] {
            updateChapters();
            if (onLanguage)
                onLanguage(language.getSelectedId() == 1 ? 1 : 0);
        };
        addAndMakeVisible(chapter);
        chapter.onChange = [this] { updateText(); };
        addAndMakeVisible(close);
        close.onClick = [this] {
            if (onClose)
                onClose();
        };
        addAndMakeVisible(body);
        body.setMultiLine(true, true);
        body.setReadOnly(true);
        body.setScrollbarsShown(true);
        body.setCaretVisible(false);
        body.setFont(font(16));
        body.setColour(juce::TextEditor::backgroundColourId, palette::panel);
        body.setColour(juce::TextEditor::textColourId, palette::text);
        body.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        body.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        body.setIndents(18, 14);
        body.onEscapeKey = [this] {
            if (onClose)
                onClose();
        };
        updateChapters();
        setWantsKeyboardFocus(true);
    }
void ComposerGuide::setLanguage(int languageIndex) {
        const int id = languageIndex == 1 ? 1 : 2;
        if (language.getSelectedId() == id)
            return;
        language.setSelectedId(id, juce::dontSendNotification);
        updateChapters();
    }
void ComposerGuide::showChapter(int number) {
        chapter.setSelectedId(juce::jlimit(1, 11, number), juce::dontSendNotification);
        updateText();
    }
bool ComposerGuide::keyPressed(const juce::KeyPress& key) {
        if (key.getKeyCode() == juce::KeyPress::escapeKey && onClose) {
            onClose();
            return true;
        }
        return false;
    }
void ComposerGuide::paint(juce::Graphics& g) {
        g.fillAll(palette::background.withAlpha(0.96f));
        g.setColour(palette::panel);
        g.fillRoundedRectangle(getLocalBounds().reduced(24).toFloat(), 12);
        g.setColour(palette::line);
        g.drawRoundedRectangle(getLocalBounds().reduced(24).toFloat(), 12, 1);
    }
void ComposerGuide::resized() {
        auto r = getLocalBounds().reduced(44);
        auto header = r.removeFromTop(38);
        close.setBounds(header.removeFromRight(76));
        header.removeFromRight(12);
        language.setBounds(header.removeFromRight(130));
        title.setBounds(header);
        r.removeFromTop(14);
        chapter.setBounds(r.removeFromTop(34));
        r.removeFromTop(12);
        body.setBounds(r);
    }
void ComposerGuide::updateChapters() {
        const int previous = std::max(1, chapter.getSelectedId());
        chapter.clear(juce::dontSendNotification);
        if (language.getSelectedId() == 1)
            chapter.addItemList(
                {"1. Pierwsza fraza", "2. Edycja fragmentu", juce::String::fromUTF8("3. Własne akordy"),
                 juce::String::fromUTF8("4. Porównanie A/B"), "5. Groove i velocity",
                 juce::String::fromUTF8("6. Dłuższa forma"), juce::String::fromUTF8("7. Łączenie wariantów"),
                 juce::String::fromUTF8("8. Studio One i skróty"), "9. Mikrotony", "10. Algorytm genetyczny",
                 juce::String::fromUTF8("11. Kreator akordów")},
                1);
        else
            chapter.addItemList({"1. Your first phrase", "2. Edit a region", "3. Your own harmony",
                                 "4. Compare A/B", "5. Groove and velocity", "6. Build an arrangement",
                                 "7. Combine variations", "8. Studio One and shortcuts", "9. Microtones",
                                 "10. Genetic search", "11. Chord Composer"},
                                1);
        chapter.setSelectedId(previous, juce::dontSendNotification);
        updateText();
    }
void ComposerGuide::updateText() {
        static const char* polish[]{
            R"(PIERWSZA FRAZA

1. Wybierz Root (tonikę), Scale (skalę), Length (długość) i Meter (metrum). Starting points ustawia punkt wyjścia dla charakteru melodii.

2. Kliknij Generate melody bank. Powstanie 12 propozycji. Kliknij kartę wariantu, a potem Play. Przycisk włącza Internal sound i wyłącza Follow DAW. Dwuklik karty lub piano rollu też uruchamia odsłuch. Obok wybierz Soft keys, Sine albo Warm organ; Stop kończy odsłuch.

3. Znalazłeś ciekawy motyw? Capture A zachowa go do porównania. Plus na karcie przypina wariant, aby przetrwał następne generowanie. Evolve selected tworzy rozwinięcia wybranej frazy; Evolution określa preferowaną wielkość zmiany.

4. Chwyć piano roll i przeciągnij na ścieżkę instrumentu. Zwykły klik nie tworzy nut. Możesz też użyć Drag MIDI albo Export. Plik zawiera aktualnie wyświetlaną frazę lub podgląd, wraz z przetwarzaniem Swing i Gate.

Diversity: różnorodność wysokości i interwałów. Dynamics: ruch melodii, nie głośność. Jazziness: swoboda chromatyczna. Valence: jaśniejsze współbrzmienia i kierunek melodii. Arousal: energia rytmu i rejestru. Weirdness: zaskakujące akcenty i skoki. Pause Amount: pauzy. Density: liczba nut, dodatkowo modulowana przez Arousal. To preferencje funkcji oceny; nazwy pochodzą z pierwotnej wtyczki, a nowy silnik nie odtwarza dawnych wag 1:1.

Panel MIDI u góry pokazuje rzeczywiste zdarzenia nutowe; OUT mierzy dźwięk wewnętrznego instrumentu. Podświetlenia klawiatury reagują na nuty wysyłane przez wtyczkę. Motion w dolnym pasku ogranicza animacje, zachowując wskazania transportu i poziomu.

? otwiera tę instrukcję. Wybierz rozdział z listy powyżej. Esc zamyka instrukcję.)",
            R"(EDYCJA WYBRANEGO FRAGMENTU

Przeciągnij po górnej linijce z numerami taktów, aby wybrać zakres czasu. Zielony pasek pokazuje zaznaczenie. Zakładka Region wyświetla jego początek i koniec w ćwierćnutach. Whole phrase zaznacza całość.

Zapętl fragment (Loop range): włącz tę opcję w Edycji fragmentu i naciśnij Play. Odtwarzane będą tylko nuty zaczynające się w zaznaczeniu; wybrzmiewanie jest ucinane na jego końcu. Zmiana zaznaczenia aktualizuje pętlę. Wyłączenie opcji przywraca odsłuch całości.

Optymalizuj fragment (Optimise range): uruchamia wyszukiwanie genetyczne tylko dla zaznaczonego fragmentu bieżącego wariantu, z aktualnymi ustawieniami ewolucji i harmonii. Reszta melodii, zablokowane nuty i inne warianty pozostają bez zmian. Wynik można cofnąć przez Undo. Opcja działa niezależnie od zapętlenia.

Simplify: przerzedź wybrany fragment.
More movement: zwiększ ruch melodii w zaznaczeniu.
Answer: utwórz wariant odpowiedzi.
Resolve: skieruj zakończenie do stabilnego dźwięku skali lub aktualnego akordu.

Operacje dotyczą odblokowanych nut, których początki leżą w zaznaczeniu. Nuty poza zaznaczeniem i nuty zablokowane pozostają zachowane. To muzyczne przekształcenia, a nie polecenia tekstowe dla modelu AI. Efekt można cofnąć przez Undo. Export range zapisuje zaznaczony zakres jako osobny plik MIDI; nuty przecinające jego granice zostają przycięte.

Przykład: wybierz ostatni takt czterotaktowej frazy i kliknij Resolve. Odsłuchaj zakończenie, porównaj z Capture A lub cofnij zmianę.

Piano roll domyślnie służy do przeciągania MIDI. Opcjonalnie włącz Edit notes: kliknij pusty obszar, aby dodać nutę; przeciągnij nutę, aby zmienić pozycję i wysokość; przeciągnij prawą krawędź, aby zmienić długość. Shift-klik lub L blokuje nutę. Delete albo prawy klik usuwa odblokowaną nutę. Strzałki góra/dół zmieniają wysokość zaznaczonej nuty. Keep rhythm zachowuje rytm podczas ewolucji.)",
            R"(WŁASNA HARMONIA

Otwórz Harmony. Wpisz akordy wraz z długością, np.:

Cm:4 Fm:2 G7:2

Kliknij Apply chords. Cm trwa cztery ćwierćnuty, Fm dwie, G7 dwie. Liczby oznaczają ćwierćnuty, NIE takty. W 4/4 pełny takt to 4; w 7/8 pełny takt to 3.5. Długości mogą być ułamkowe.

Dostępne jakości: C, Cm, Cdim, Caug, C7, Cmaj7, Cm7, Csus2, Csus4. Możesz używać krzyżyków i bemoli. Pasek nad nutami pokazuje zastosowaną progresję. Krótsza progresja powtarza się przez frazę.

Własne akordy mają pierwszeństwo przed gotową progresją. Wpływają na generowanie i operacje muzyczne; samo Apply chords nie przepisuje istniejących nut. Fit to scale działa również przy własnej harmonii: dopasowuje wysokości do wybranej skali podczas odtwarzania i eksportu. Może zmienić chromatyczne dźwięki akordowe; wyłącz go, żeby usłyszeć oryginalny wynik generatora. Clear przywraca gotową progresję.

Import harmony MIDI wczytuje osobny plik referencyjny bez zastępowania melodii. Rozpoznawanie akordów jest przybliżone. Sprawdź wynik w polu tekstowym, popraw go i zastosuj. Najczytelniejszy materiał wejściowy to pojedyncza ścieżka akordów bez perkusji.)",
            R"(PORÓWNANIE A/B

Capture A zapamiętuje aktualnie wyświetlaną frazę jako niezależny punkt odniesienia. Hear A przełącza odsłuch na zapisane A. Hear B wraca do aktualnego wariantu lub podglądu. Ponowne Capture A zastępuje punkt odniesienia. Clear A usuwa zapisane A i jego obrysy.

Gdy Switch at loop jest włączone i trwa odtwarzanie, wybór wariantu oraz przełączenie A/B czekają do końca aktualnej pętli. Gdy odtwarzanie jest zatrzymane, wybór działa od razu. Wyłącz tę opcję, jeśli potrzebujesz natychmiastowego przełączania.

Opis nad piano rollem wskazuje, co aktualnie słychać i czy kolejna zmiana oczekuje. Turkusowa kropka na karcie oznacza odtwarzany wariant, złota — oczekujący. Edytor nadal pokazuje wybraną frazę B, nawet podczas odsłuchu A.

Złote obrysy w piano rollu pokazują nuty punktu A, które różnią się pozycją, wysokością lub długością od bieżącej frazy. Obrysy nie przedstawiają różnic samego velocity.

Przykład: Capture A → zmień zakończenie przez Region → Play → Hear A / Hear B. Dzięki przełączaniu na granicy pętli porównasz całe frazy.

A/B przełącza wyjście odsłuchowe. Export i Drag MIDI zapisują frazę wyświetlaną w edytorze, więc przed eksportem sprawdź jej tytuł.)",
            R"(GROOVE I EKSPRESJA

Otwórz Groove. Import groove MIDI wczytuje osobny wzorzec rytmiczny i akcenty bez zastępowania melodii. Use selected zapamiętuje aktualnie wybrany wariant jako wzorzec. Następnie wybierz wariant, który chcesz zmienić.

Timing określa siłę dopasowania pozycji nut do wzorca. Velocity określa siłę przenoszenia akcentów. 0% zachowuje daną cechę, 100% stosuje pełną proponowaną zmianę. Apply groove zapisuje wynik w wybranej frazie; Undo cofa operację.

Przykład: wczytaj groove perkusji, ustaw Timing na 30% i Velocity na 70%. Odsłuchaj, czy melodia lepiej współgra z akcentami. Algorytm przenosi odchylenia najbliższych pozycji siatki, a nie całą strukturę rytmu. Krótszy wzorzec zapętla się; z dłuższego używany jest odpowiedni początkowy fragment. Wysokości i długości nut pozostają zachowane, podobnie jak całe nuty zablokowane.

Velocity pojedynczej nuty: włącz Edit notes, kliknij jej słupek w dolnym pasku VEL piano rollu i przeciągnij pionowo. Jaśniejszy słupek wskazuje wybór. Gdy kilka nut zaczyna się równocześnie, najłatwiej najpierw rozsunąć je albo edytować po eksporcie.

Swing i Gate są osobnymi ustawieniami wykonania. Ich efekt jest uwzględniony w odsłuchu i eksporcie, więc przy ocenie groove sprawdź również te wartości.)",
            R"(DŁUŻSZA FORMA: A — A′ — B — A″

Wybierz dobry motyw i otwórz Arrange. Preview arrangement tworzy cztery kolejne części: oryginalny motyw A, rozwinięcie A′, odpowiedź B oraz zakończenie A″. Punktem wyjścia jest aktualnie wybrana fraza.

Całość pojawia się w piano rollu jako podgląd. Przy frazie czterotaktowej otrzymasz szesnaście taktów. Odsłuchaj całą sekwencję; Export i Drag MIDI mogą wyeksportować ją od razu.

Keep preview zastępuje wybrany slot banku gotową sekwencją. Discard wraca do dotychczasowej frazy. Podczas podglądu edycja nut i generowanie są wyłączone: najpierw zachowaj albo odrzuć wynik. Podgląd jest tymczasowy; przed zamknięciem projektu zachowaj go lub wyeksportuj.

Po zachowaniu możesz zaznaczyć pojedynczą część w linijce i poprawić ją operacjami Region. Undo przywraca stan przed zachowaniem sekwencji. Aby wyeksportować jedną część, zaznacz jej czas w linijce i wybierz Region → Export range. Całą sekwencję zapisuje zwykły Export.

To kompozycja jednej partii MIDI, nie aranżacja wielu instrumentów. Motyw źródłowy może mieć najwyżej 32 ćwierćnuty i 128 nut. Wynik może zawierać najwyżej 512 nut; bardzo gęsty materiał może wymagać wcześniejszego uproszczenia.)",
            R"(ŁĄCZENIE CECH WARIANTÓW

Otwórz Combine. Wybierz trzy źródła z aktualnego banku:

Rhythm — początki i długości nut oraz długość wynikowej frazy.
Pitches — wysokości nut.
Accents — velocity, czyli siłę uderzeń.

Przykład: Rhythm 03, Pitches 07, Accents 02. Preview combination tworzy nową frazę z tych trzech źródeł. Różna liczba nut jest dopasowywana według względnej pozycji w źródłowych frazach; nie musi istnieć zgodność nuta w nutę.

Piano roll pokazuje wynik do odsłuchu. Keep preview zapisuje go w aktualnie wybranym slocie banku. Discard pozostawia poprzednią frazę. Możesz zmienić źródła i ponownie uruchomić podgląd przed decyzją.

Zablokowane nuty źródła Rhythm są zachowywane w całości: ich wysokość i velocity nie są zastępowane. Blokady w źródłach Pitches i Accents nie ograniczają wyniku.

Capture A przed rozpoczęciem ułatwia porównanie. Zapnij ważne warianty plusem i użyj Save bank, jeśli chcesz zachować zestaw źródeł na później.)",
            R"(STUDIO ONE I SKRÓTY

Najprostszy sposób pracy: odsłuchaj frazę przez Internal sound, a następnie przeciągnij Drag MIDI na ścieżkę wybranego instrumentu w Studio One. Export zapisuje standardowy plik MIDI, który można wczytać ręcznie.

Follow DAW wiąże start, stop, pozycję i tempo odtwarzania z transportem hosta. Play automatycznie wyłącza Follow DAW i włącza Internal sound do niezależnego odsłuchu. Wyłącz Follow DAW także do wyzwalania wariantów nutami MIDI. Gdy używasz innego instrumentu do odsłuchu wyjścia MIDI, wyłącz Internal sound, aby uniknąć podwójnego dźwięku. Dostępność routingu MIDI zależy od formatu wtyczki i konfiguracji hosta.

MIDI 48–59: przytrzymaj, aby wyzwolić warianty 1–12.
MIDI 60–127: transpozycja; 60 oznacza oryginalną wysokość. Nazwy oktaw mogą różnić się w DAW.

Spacja: Play / Stop (Play włącza brzmienie wewnętrzne i wyłącza Follow DAW).
Cmd/Ctrl+Z: Undo.
Cmd/Ctrl+Shift+Z: Redo.
Esc: zamknij instrukcję; poza instrukcją — Stop all.
Shift-klik lub L: zablokuj / odblokuj nutę.
Delete / prawy klik: usuń odblokowaną nutę, gdy Edit notes jest włączone.
Kółko myszy nad piano rollem: przewijaj wysokości.

Skróty odtwarzania nie działają podczas wpisywania tekstu. Save bank zachowuje bank i ustawienia; stan jest również zapisywany z projektem DAW. Tymczasowy podgląd należy wcześniej zachować przez Keep preview.)",
            R"(MIKROTONY (OPCJONALNIE)

Micro to osobny włącznik w zakładce Micro. Wyłączony daje zwykłą melodię 12-tonową. Włączony pozwala algorytmowi szukać krótkich dźwięków przejściowych między półtonami i rozwiązywać je na stabilnych nutach.

Wybór koloru określa dostępne odcienie: neutralne interwały, Rast, Bayati, Saba, blues albo 24-EDO. Główna Scale nadal prowadzi melodię. Nie ma ręcznego procentu mikrotonów ani przycisku przestrojenia wszystkich nut: ich miejsca, czas trwania i rozwiązania są częścią optymalizacji genetycznej.

Po zmianie ustawień użyj Generate melody bank lub Evolve selected. W piano rollu fioletowe nuty leżą między wierszami; E4-50 to E4 obniżone o 50 centów. Play odtwarza ich dokładną wysokość. Fit to scale dopasowuje bazową wysokość do Scale, zachowując mikrotonowe odchylenie; wyłącz go, aby usłyszeć niezmienione wysokości generatora.

Wyjście MIDI:
Pitch bend (mono): kanał 1, dla jednogłosowej melodii. Ustaw w syntezatorze odbierającym zakres pitch bend taki sam jak Bend. Gdy nuty zachodzą na siebie, nowy pitch bend może zmienić wybrzmiewanie poprzedniej nuty.
MPE: każda nuta na osobnym kanale 2–16, zakres ±48. Instrument odbierający musi obsługiwać MPE.

Export i Drag MIDI zapisują odpowiednie komunikaty stroju. Odsłuch wewnętrzny nie wymaga konfiguracji MIDI. Akordy pozostają w 12-TET i mają osobną warstwę dźwięku, więc mikrotonowy pitch bend melodii ich nie odstraja. Na karcie Melody włącz With chords, żeby odsłuchać obie warstwy.)",
            R"(ALGORYTM GENETYCZNY, METRUM I PROGRESJE

Search pokazuje rzeczywistą pracę algorytmu. Quick: 64 osobniki i 40 pokoleń. Balanced: 128 i 100. Deep: 256 i 300. Custom budget pozwala ustawić 16-512 osobników i 1-2000 pokoleń. Crossover to prawdopodobieństwo krzyżowania (domyślnie 90%).

Każde pokolenie jest oceniane równolegle funkcją fitness. Od 64 osobników wyszukiwanie dzieli się na cztery wyspy; każda wyspa zachowuje jedną elitę bez zmian. Selekcja turniejowa wybiera rodziców. Krzyżowanie wymienia fragmenty fraz, a mutacja rośnie, gdy wyspa przestaje się poprawiać. Blokady, Keep rhythm i Melody on rhythm chronią granice nut; wtedy krzyżowane są wysokości i velocity. W Rhythm only krzyżowane są także fragmenty rytmu, o ile rytm nie jest chroniony. Przypięte warianty mogą wejść do Evolve jako dodatkowi rodzice. Humanize domyślnie wynosi zero. Powyżej zera przesuwa początki i velocity melodii w eksporcie i odsłuchu; zapisana fraza, rolka akordów i eksport akordów pozostają dokładne. Przycisk EN/PL zmienia tylko język tego edytora i zapisuje się lokalnie, nie jako parametr hosta.

Generation, Evaluations, Crossovers oraz Best/Mean fitness pokazują faktyczne obliczenia. Populacja początkowa także jest oceniana, więc liczba ocen to populacja x (pokolenia + 1). Fitness to wewnętrzna miara zgodności z ustawieniami, nie obiektywna ocena muzyki. Więcej pokoleń nie gwarantuje lepszego pomysłu. Cancel search zachowuje poprzedni bank.

To nowy silnik, nie kopia dawnego genetic.cpp. Stary kod miał 128 osobników, 100 pokoleń i 90% krzyżowania. Nowy przechowuje wyniki fitness na czas pokolenia i ma inną funkcję oceny, dlatego nawet pełny przebieg może być szybki. Czas nie jest sztucznie wydłużany.

Meter: wybierz z 27 propozycji albo wpisz np. 11/8. Licznik 1-32; mianownik 1, 2, 4, 8, 16 lub 32. Metrum dotyczy nowego generowania; Evolve zachowuje metrum rodzica. Fraza może mieć do 128 ćwierćnut — przy długim metrum wybierz mniej taktów.

Harmony: 25 progresji oraz Scale only. Stopnie są względne do wybranej skali, jeden akord na takt. Custom chords otwiera wpisywanie akordów i długości, np. Cm:4 Ab:4 Eb:4 Bb:4. Wybór gotowej progresji usuwa aktywną własną harmonię.)",
            R"(KARTA CHORDS — AKORDY I PRZEWROTY

Melody i Chords to dwie główne karty tego samego interfejsu. Root i Scale są wspólne dla obu kart; Meter ustaw na Melody. Chords ma osobne Length, domyślnie 4 takty. Follow melody jawnie wiąże je z długością melodii. Ustawienia dotyczą następnego generowania, nie przepisują istniejących nut.

Originality określa charakter następnej progresji. Nisko: znane funkcjonalne układy. Wyżej: substytucje, rozszerzenia, zapożyczenia modalne i dominanty wtrącone. Generator harmonii używa reguł muzycznych i losowego ziarna; wartość nie jest oceną artystycznej oryginalności. Melodia nadal powstaje w pełnym algorytmie genetycznym z budżetem Search.

Harmonic rhythm wybiera zmianę co dwa takty, takt albo pół taktu. Resolve to tonic zamyka progresję na tonice. New chords only zmienia tylko harmonię. Melody from these chords zachowuje widoczne akordy i tworzy bank melodii na całą długość tej sekwencji. New chords + melody wykonuje obie operacje. Oba przyciski melodii wracają na Melody i włączają With chords; przypięte warianty pozostają zachowane. Generate melody bank na Melody korzysta z aktywnej harmonii, ale z długości wybranej na Melody; Evolve selected rozwija wybrany wariant.

Smooth inversions ogranicza ruch głosów przez całą sekwencję. Root position utrzymuje prymę w basie. Open voicings rozsuwa głosy. Voice centre określa rejestr; po zmianie kliknij Revoice, aby przeliczyć ułożenie głosów bez zmiany progresji. Slash pokazuje faktyczny bas: C/E to C-dur z E na dole.

Trójkąt Play pod akordowym piano rollem odtwarza same akordy, a kwadrat Stop zatrzymuje odsłuch. Na Melody włącz With chords i naciśnij Play, aby usłyszeć melodię wraz z harmonią. Przełączanie kart nie zatrzymuje odtwarzania.

Akordy przeciągaj z piano rollu Chords albo Drag chord MIDI. Melodię przeciągaj z Melody. Export zapisuje widoczne przewroty i rejestr akordów. MIDI obu warstw pozostaje oddzielne. Akordy mają strój 12-TET również przy włączonym Micro; wewnętrzny odsłuch używa osobnej warstwy, więc pitch bend melodii nie zmienia stroju akordów. Progresja i przewroty zapisują się z bankiem i projektem DAW.

Length ma też 3, 6, 12 i 16 taktów. Nazwy akordów obejmują m7b5, dim7, 6, m6, add9, 9 i maj9, nadal jako cztery głosy. Skale dodają frygijską, lokrycką, molową melodyczną, całotonową, bluesową i frygijską dominantową. Kształt rytmu może być równy, długi-krótki albo krótki-długi. Melody + chords zapisuje MIDI typu 1: melodię na ścieżce 1 i dokładne głosy akordów na ścieżce 2. Chord MIDI output jest domyślnie wyłączone; po włączeniu wysyła te głosy na kanale 16 i pozostaje wyłączone, gdy MPE używa kanałów 2–16.)"};
        static const char* english[]{
            R"(YOUR FIRST PHRASE

Choose Root, Scale, Length and Meter, then Generate melody bank. Select one of the 12 variations and press Play: it enables Internal sound and disables Follow DAW. Double-clicking a card or the piano roll also starts audition. Choose Soft keys, Sine or Warm organ beside the button; Stop ends preview.

Capture A keeps a reference for comparison. The plus on a card pins that variation so it survives another bank generation. Evolve selected develops the current phrase; Evolution controls the preferred amount of change.

Drag the piano roll onto an instrument track, or use Drag MIDI / Export. Clicking the roll does not create notes by default. Export writes the displayed phrase or draft, including Swing and Gate processing.

Diversity shapes pitch and interval variety. Dynamics shapes melodic movement, not loudness. Jazziness adds chromatic freedom. Valence favours brighter consonance and contour. Arousal shapes rhythmic energy and register. Weirdness adds unexpected accents and leaps. Pause Amount leaves space. Density sets note activity, also influenced by Arousal. These are scoring preferences with the original plugin's names; the new engine does not reproduce the old weights 1:1.

The header MIDI trace shows actual emitted note events; OUT meters the internal audio output. Piano keys respond to emitted notes. Turn Motion off in the bottom bar for reduced animation while transport and level indicators remain active.

Use the chapter selector above for detailed workflows. Escape closes this guide.)",
            R"(EDIT A REGION

Drag across the top bar-number ruler to select a time range. The green strip marks the selection. Region shows the endpoints in quarter-note beats; Whole phrase selects everything.

Loop range: enable this in Edit region, then press Play. Only notes starting inside the selection play, with note-offs at the range end. Moving the selection updates the loop. Disable it to hear the whole phrase again.

Optimise range runs the genetic search on the selected section of the current variation using the current evolution and harmony settings. The rest of the melody, locked notes and other variations remain unchanged. Undo restores the previous phrase. This works independently of Loop range.

Simplify thins the selection. More movement increases melodic movement. Answer creates a contrasting response. Resolve steers the ending towards a stable scale or chord tone.

Only unlocked notes whose onsets fall inside the selected range are transformed. Other notes are preserved. These are musical algorithms, not natural-language AI commands. Undo reverses the operation. Export range saves the selection as a separate MIDI file; notes crossing its boundaries are cropped.

Example: select the final bar of a four-bar phrase and press Resolve. Play the ending and compare it with a captured A reference.

The piano roll exports MIDI by default. Enable Edit notes for manual editing: click an empty area to add; drag a note to move; drag its right edge to resize. Shift-click or L toggles a lock. Delete or right-click removes an unlocked note. Up/down arrows change the selected note's pitch. Keep rhythm preserves note starts and durations during evolution.)",
            R"(YOUR OWN HARMONY

Open Harmony and enter, for example:

Cm:4 Fm:2 G7:2

Apply chords sets C minor for four quarter-note beats, F minor for two, then G dominant seventh for two. Durations are BEATS, not bars. One 4/4 bar is 4 beats; one 7/8 bar is 3.5. Fractional durations are supported.

Qualities: C, Cm, Cdim, Caug, C7, Cmaj7, Cm7, Csus2 and Csus4. Sharps and flats are supported. The strip above the notes shows the applied progression; a shorter progression repeats through the phrase.

Custom harmony overrides the preset progression. It guides generation and musical transforms; applying chords alone does not rewrite existing notes. Fit to scale remains available with custom harmony and fits pitches to the chosen scale during playback and export. It can change chromatic chord tones; turn it off to hear the generator's original pitches. Clear restores preset harmony.

Import harmony MIDI reads a separate reference without replacing the melody. Chord recognition is approximate: inspect the text, correct it, and apply. A clean chord track without percussion works best.)",
            R"(COMPARE A/B

Capture A stores the displayed phrase as an independent reference. Hear A auditions that reference; Hear B returns to the selected variation or draft. Capturing again replaces the reference. Clear A removes the saved reference and its outlines.

With Switch at loop enabled, variation and A/B changes wait until the current playback loop ends. Changes are immediate while stopped. Disable the option for immediate switching during playback.

The line above the piano roll identifies what is audible and what is queued. A teal card dot marks the audible variation; gold marks a queued variation. The editor keeps showing B even while A is audible.

Gold outlines show reference A notes with different positions, pitches or durations. Velocity-only differences are not outlined.

Example: Capture A, edit the ending in Region, press Play, then alternate Hear A and Hear B. Each change can start at the loop boundary.

A/B affects audition output. Export and Drag MIDI always write the displayed phrase, so check its title before exporting.)",
            R"(GROOVE AND VELOCITY

In Groove, Import groove MIDI loads a timing and accent reference without replacing the melody. Use selected captures the current variation as a reference; then choose the variation you want to change.

Timing controls how far note starts move towards the reference. Velocity controls accent transfer. 0% preserves the feature; 100% applies the full proposed change. Apply groove changes the selected phrase; Undo reverses it. Locked notes remain unchanged.

Try a drum reference with Timing at 30% and Velocity at 70%. Timing transfers deviations from the nearest reference grid positions, not the entire rhythm structure. A shorter reference loops; a longer reference contributes its opening portion. Pitches and durations are preserved.

To edit one note's velocity, enable Edit notes and click its bar in the VEL lane below the piano roll and drag vertically. The selected bar is brighter. Notes with identical onsets share the same horizontal position.

Swing and Gate apply additional playback and export processing. Check those values when evaluating the groove.)",
            R"(BUILD A — A′ — B — A″

Select a useful motif, open Arrange and press Preview arrangement. The result has four consecutive sections: original A, a development A′, an answer B, and an ending A″.

A four-bar source becomes a sixteen-bar sequence. The piano roll shows the entire draft. You can audition it or export it directly with Export or Drag MIDI.

Keep preview replaces the selected bank slot with the sequence. Discard returns to the committed phrase. Note editing and generation are disabled while a draft is pending; first keep or discard it. A draft is temporary: keep or export it before closing the project.

After keeping the sequence, select one section in the ruler and refine it using Region. Undo restores the phrase from before the commit. To export one section, select its time range in the ruler and use Region > Export range. The main Export writes the whole sequence.

This arranges one MIDI part, not multiple instruments. The source must be at most 32 quarter-note beats and 128 notes. The result can hold up to 512 notes; very dense source material may need simplifying first.)",
            R"(COMBINE VARIATIONS

In Combine, choose three sources from the current bank:

Rhythm supplies note starts, durations and the result's length.
Pitches supplies pitches.
Accents supplies velocity values.

For example, select Rhythm 03, Pitches 07 and Accents 02, then Preview combination. Sources with different note counts are matched by relative position within each phrase; a one-to-one note correspondence is not required.

Play the draft shown in the piano roll. Keep preview writes it to the currently selected bank slot. Discard preserves the previous phrase. You can change source choices and preview again before deciding.

Locked notes in the Rhythm source are preserved in full: their pitch and velocity are not replaced. Locks in the Pitches and Accents sources do not constrain the result.

Capture A before starting for easy comparison. Pin important sources and Save bank to retain the complete source set.)",
            R"(STUDIO ONE AND SHORTCUTS

Play through Internal sound, then drag Drag MIDI onto your instrument track in Studio One. Export saves a standard MIDI file for manual import.

Follow DAW synchronises playback position, tempo, start and stop with the host. Play automatically disables Follow DAW and enables Internal sound for independent preview. Turn Follow DAW off for MIDI-note triggering too. Disable Internal sound when another instrument receives the MIDI output. MIDI routing availability depends on the plugin format and host setup.

MIDI 48–59: hold to trigger variations 1–12.
MIDI 60–127: transpose; 60 means original pitch. DAW octave names may differ.

Space: Play / Stop (Play enables the internal sound and turns Follow DAW off).
Cmd/Ctrl+Z: Undo.
Cmd/Ctrl+Shift+Z: Redo.
Esc: close the guide; outside it, Stop all.
Shift-click or L: toggle a note lock.
Delete / right-click: delete an unlocked note when Edit notes is on.
Mouse wheel over the roll: scroll pitches.

Playback shortcuts are suspended while typing text. Save bank stores the bank and settings; the DAW project also stores plugin state. Keep preview commits a temporary draft before saving.)",
            R"(MICROTONES (OPTIONAL)

Micro is an explicit switch on the Micro tab. Off gives a normal 12-tone melody. On allows the genetic search to place short passing notes between semitones and resolve them into stable notes.

The colour selector supplies neutral intervals, Rast, Bayati, Saba, blues or 24-EDO. The main Scale still guides the melody. There is no manual percentage or blanket retuning action: the placement, duration and resolution of microtonal passing notes are part of the optimisation.

Use Generate melody bank or Evolve selected after changing the settings. Violet piano-roll notes sit between rows; E4-50 is E4 lowered by 50 cents. Play previews their exact pitches. Fit to scale moves base pitches into Scale while retaining their microtonal offsets; turn it off to hear the generator's original pitches.

MIDI output:
Pitch bend (mono): channel 1, for a single melody voice. Set the receiving synth's bend range to the Bend value. Overlapping notes may share the new bend during the preceding note's release.
MPE: one note per channel 2–16, at ±48. The receiving instrument must support MPE.

Export and Drag MIDI include the tuning messages. Internal preview needs no MIDI setup. Chords stay in 12-TET and use a separate sound layer, so melody pitch bends do not detune them. Enable With chords on Melody to hear both layers together.)",
            R"(GENETIC SEARCH, METER AND PROGRESSIONS

Quick uses 64 individuals and 40 generations; Balanced 128 and 100; Deep 256 and 300. Search > Custom budget allows 16-512 individuals and 1-2000 generations. Crossover is the mating probability (90% by default).

Every population is scored in parallel by fitness. From 64 individuals the search uses four islands, and each island keeps one elite unchanged. Tournament selection chooses parents. One-point crossover exchanges phrase sections, and mutation rises when an island stops improving. Locks, Keep rhythm and Melody on rhythm protect timing; crossover then exchanges pitches and velocities. Rhythm only also crosses rhythm sections when timing is unprotected. Pinned variations can join Evolve as extra parents. Humanize defaults to zero. Above zero it shifts melody starts and velocities in export and preview; the stored phrase, chord roll and chord export stay exact. The EN/PL button changes only this editor's language and is saved locally, not as a host parameter.

Generation, Evaluations, Crossovers and Best/Mean fitness report real work. The initial population is also evaluated: total evaluations = population x (generations + 1). Fitness is an internal measure of matching settings, not an objective musical rating. More generations do not guarantee a better idea. Cancel search keeps the old bank.

This is a new engine, not a copy of the old genetic.cpp. The old code used 128 individuals, 100 generations and 90% crossover. Cached fitness and a different scoring function make the new search faster; it adds no artificial delay.

Meter offers 27 presets or typed input such as 11/8: numerator 1-32, denominator 1, 2, 4, 8, 16 or 32. Meter affects new generation; Evolve retains the parent's meter. Phrases are limited to 128 quarter-note beats, so choose fewer bars for long meters.

Harmony offers 25 progressions plus Scale only. Degrees follow the chosen scale, one chord per bar. Custom chords opens chord/duration entry such as Cm:4 Ab:4 Eb:4 Bb:4. Selecting a preset clears active custom harmony.)",
            R"(CHORDS TAB — HARMONY AND VOICINGS

Melody and Chords are the two main pages of one interface. Root and Scale are shared; set Meter on Melody. Chords has its own Length, defaulting to 4 bars. Follow melody explicitly links the two lengths. Settings affect the next generation, not existing notes.

Originality shapes the next progression. Lower values use familiar functional movement. Higher values favour substitutions, extensions, modal borrowing and secondary dominants. Harmony uses musical rules and a random seed; the value is not an artistic originality score. Melody still uses the full genetic search with your Search budget.

Harmonic rhythm changes chords every two bars, one bar or half a bar. Resolve to tonic ends on the tonic. New chords only changes harmony. Melody from these chords keeps the displayed chords and creates a melody bank spanning the entire sequence. New chords + melody performs both steps. Both melody actions return to Melody and enable With chords; pinned variations remain intact. Generate melody bank on Melody uses active harmony with the length selected on Melody; Evolve selected develops the selected variation.

Smooth inversions limits voice movement across the sequence. Root position keeps the root in the bass. Open voicings spreads the voices. Voice centre sets the register; click Revoice after changing it to update the voices without changing the progression. Slash names show the actual bass: C/E is C major over E.

The triangular Play button below the chord piano roll plays the chords alone; the square Stop ends preview. On Melody, enable With chords and press Play to hear both layers together. Switching tabs does not stop playback.

Drag the Chords piano roll or Drag chord MIDI for the chord part. Drag the Melody piano roll for the melody. Export saves the displayed inversions and register. Each layer remains a separate MIDI export. Chords use 12-TET even with Micro on; internal preview gives them a separate sound layer so melody pitch bends do not detune them. The progression and voicings are saved with the bank and DAW project.

Length also offers 3, 6, 12 and 16 bars. Chord names add m7b5, dim7, 6, m6, add9, 9 and maj9, still as four voices. Scales add Phrygian, Locrian, melodic minor, whole tone, blues and Phrygian dominant. Rhythm shape can be even, long-short or short-long. Melody + chords writes MIDI type 1: the melody on track 1 and the exact chord voices on track 2. Chord MIDI output is off by default; when enabled it sends those voices on channel 16 and stays off while MPE uses channels 2-16.)"};
        const auto index = juce::jlimit(0, 10, chapter.getSelectedId() - 1);
        body.setText(juce::String::fromUTF8(language.getSelectedId() == 1 ? polish[index] : english[index]),
                     false);
        body.moveCaretToTop(false);
    }
