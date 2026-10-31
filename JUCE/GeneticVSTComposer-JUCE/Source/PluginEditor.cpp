#include "PluginEditor.h"
#include "InterfaceText.h"
#include "ChordComposerPanel.h"
#include "ChordGenerator.h"
#include <cmath>
using namespace palette;
namespace {
juce::Font font(float size, bool bold = false) {
#if JUCE_MAC
    return juce::Font(juce::FontOptions("Avenir Next", size, bold ? juce::Font::bold : juce::Font::plain));
#else
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
#endif
}
juce::String noteName(int pitch) {
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
}
// E4-50 is E4 a quarter tone flat.
juce::String noteName(const composer::Note& note, bool microtonal) {
    if (!microtonal || note.cents == 0)
        return noteName(note.pitch);
    return noteName(note.pitch) + (note.cents > 0 ? "+" : "") + juce::String(note.cents);
}
} // namespace
PianoRoll::PianoRoll() {
    setWantsKeyboardFocus(true);
    setNoteEditing(false);
}
void PianoRoll::setNoteEditing(bool enabled) {
    noteEditing = enabled;
    setMouseCursor(enabled ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
    setTooltip(
        enabled
            ? "Edit notes: click to add, drag to move or resize; right-click to delete. Shift-click locks. "
              "Microtonal passing notes are chosen by the generator."
            : "Drag the piano roll to an instrument track to export MIDI. Double-click to audition. "
              "Shift-click a note to lock it. Drag the bar ruler to select a range. Enable Edit notes for "
              "manual editing.");
    repaint();
}
void PianoRoll::setMicrotonal(bool enabled) {
    if (microtonal != enabled) {
        microtonal = enabled;
        repaint();
    }
}
void PianoRoll::setClip(const composer::Clip& c, bool resetView) {
    const auto previous = chosen >= 0 && chosen < clip.count ? clip.notes[chosen] : composer::Note{};
    const bool retainSelection = !resetView && chosen >= 0;
    resetView = resetView || c.seed != clip.seed;
    clip = c;
    sourceActivity.fill(0);
    if (resetView) {
        selectionStart = 0;
        selectionEnd = clip.length;
    } else {
        selectionStart = std::min(selectionStart, std::max(0.0, clip.length - clip.grid));
        selectionEnd = juce::jlimit(selectionStart + clip.grid, clip.length, selectionEnd);
    }
    chosen = -1;
    if (retainSelection)
        for (int i = 0; i < c.count; ++i)
            if (c.notes[i].pitch == previous.pitch && c.notes[i].start == previous.start) {
                chosen = i;
                break;
            }
    if (!resetView) {
        repaint();
        return;
    }
    low = 60;
    high = 72;
    for (int i = 0; i < c.count; ++i) {
        low = std::min(low, c.notes[i].pitch);
        high = std::max(high, c.notes[i].pitch);
    }
    low = std::max(0, low - 3);
    high = std::min(127, std::max(low + 17, high + 3));
    repaint();
}
void PianoRoll::setReference(const composer::Clip& c, bool visible) {
    reference = c;
    showReference = visible;
    repaint();
}
void PianoRoll::setHarmony(const composer::Harmony& h) {
    harmony = h;
    repaint();
}
void PianoRoll::selectAllTime() {
    selectionStart = 0;
    selectionEnd = clip.length;
    if (onRegionChange)
        onRegionChange(selectionStart, selectionEnd);
    repaint();
}
void PianoRoll::setActivity(const GeneticVSTComposerJUCEAudioProcessor::VisualState& state,
                            bool showingAudible, bool animate, float elapsed) {
    bool changedEnergy = false;
    const float attack = animate ? 1.0f - std::exp(-elapsed * 40.0f) : 1.0f;
    const float release = animate ? 1.0f - std::exp(-elapsed * 9.0f) : 1.0f;
    auto smooth = [&](float& current, float target) {
        const float next = current + (target - current) * (target > current ? attack : release);
        changedEnergy |= std::abs(next - current) > 0.001f;
        current = next < 0.001f ? 0.0f : next;
    };
    std::array<float, composer::maxNotes> sourceLevels{};
    for (size_t pitch = 0; pitch < noteActivity.size(); ++pitch) {
        smooth(noteActivity[pitch], state.notes[pitch]);
        const int source = state.sourceNotes[pitch];
        if (showingAudible && source >= 0 && source < clip.count)
            sourceLevels[size_t(source)] = std::max(sourceLevels[size_t(source)], state.notes[pitch]);
    }
    for (size_t i = 0; i < sourceActivity.size(); ++i)
        smooth(sourceActivity[i], sourceLevels[i]);
    if (changedEnergy)
        repaint();
}
void PianoRoll::setPlayhead(double beat) {
    if (playhead != beat) {
        playhead = beat;
        repaint();
    }
}
juce::Rectangle<float> PianoRoll::gridArea() const {
    return {48, 44, float(getWidth() - 49), float(getHeight() - 69)};
}
juce::Rectangle<float> PianoRoll::noteRect(const composer::Note& n) const {
    auto r = gridArea();
    const float row = r.getHeight() / (high - low + 1);
    // Microtonal notes sit between rows: E half-flat is half a row below E.
    const float pitch = n.pitch + (microtonal ? n.cents / 100.0f : 0.0f);
    return {r.getX() + float(n.start / clip.length) * r.getWidth(), r.getY() + (high - pitch) * row + 1,
            std::max(3.0f, float(n.duration / clip.length) * r.getWidth() - 1), std::max(2.0f, row - 2)};
}
double PianoRoll::beatAt(float x) const {
    auto r = gridArea();
    return juce::jlimit(0.0, clip.length - clip.grid,
                        std::floor((x - r.getX()) / r.getWidth() * clip.length / clip.grid) * clip.grid);
}
int PianoRoll::pitchAt(float y) const {
    auto r = gridArea();
    return juce::jlimit(0, 127, high - int((y - r.getY()) / (r.getHeight() / (high - low + 1))));
}
void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(background.darker(0.12f));
    const auto r = gridArea();
    const float row = r.getHeight() / (high - low + 1);
    for (int p = low; p <= high; ++p) {
        const float y = r.getY() + (high - p) * row;
        const int pc = p % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        g.setColour(black ? juce::Colour(0xff131a26) : juce::Colour(0xff19212e));
        g.fillRect(r.getX(), y, r.getWidth(), row);
        const float energy = noteActivity[size_t(p)];
        g.setColour((black ? surface.darker(0.22f) : juce::Colour(0xff384658))
                        .interpolatedWith(teal, energy * 0.65f));
        g.fillRoundedRectangle(2.0f, y + 0.5f, 42.0f, std::max(1.0f, row - 1.0f), 1.5f);
        if (energy > 0.01f) {
            g.setGradientFill(juce::ColourGradient(teal.withAlpha(energy * 0.16f), r.getX(), y,
                                                   teal.withAlpha(0.0f), r.getX() + 140, y, false));
            g.fillRect(r.getX(), y, r.getWidth(), row);
        }
        if (pc == 0 || (row > 13 && !black)) {
            g.setFont(font(10));
            g.setColour(pc == 0 ? text : muted);
            g.drawText(noteName(p), 4, int(y), 36, int(row), juce::Justification::centredRight);
        }
        g.setColour(line.withAlpha(0.3f));
        g.drawHorizontalLine(int(y), r.getX(), r.getRight());
    }
    const double bar = clip.numerator * 4.0 / clip.denominator;
    const double drawGrid = clip.length > 32 ? 1.0 : clip.grid;
    for (double t = 0; t < clip.length + 0.001; t += drawGrid) {
        const float x = r.getX() + float(t / clip.length) * r.getWidth();
        const bool barLine = std::abs(std::remainder(t, bar)) < 0.001;
        const bool beat = std::abs(t - std::round(t)) < 0.001;
        g.setColour(line.withAlpha(barLine ? 1.0f : (beat ? 0.6f : 0.25f)));
        g.drawVerticalLine(int(x), r.getY(), r.getBottom());
        if (barLine && t < clip.length) {
            g.setFont(font(11, true));
            g.setColour(muted);
            g.drawText(juce::String(int(t / bar) + 1), int(x) + 5, 1, 30, 20,
                       juce::Justification::centredLeft);
        }
    }
    const float selX = r.getX() + float(selectionStart / clip.length) * r.getWidth();
    const float selW = float((selectionEnd - selectionStart) / clip.length) * r.getWidth();
    g.setColour(teal.withAlpha(0.2f));
    g.fillRect(selX, 0.0f, selW, 22.0f);
    g.setColour(teal);
    g.fillRect(selX, 21.0f, selW, 2.0f);
    g.setFont(font(9));
    g.setColour(muted);
    g.drawText(interfaceText("BARS", interfaceLanguage), 3, 1, 40, 20, juce::Justification::centredRight);
    if (harmony.count > 0) {
        for (double offset = 0; offset < clip.length; offset += std::max(0.25, harmony.length)) {
            for (int i = 0; i < harmony.count; ++i) {
                const auto& chord = harmony.chords[i];
                const double start = offset + chord.start;
                if (start >= clip.length)
                    break;
                const float x = r.getX() + float(start / clip.length) * r.getWidth();
                const float width =
                    float(std::min(chord.duration, clip.length - start) / clip.length) * r.getWidth();
                g.setColour(i % 2 == 0 ? surface : panel);
                g.fillRect(x, 25.0f, width - 1, 17.0f);
                g.setColour(gold);
                const auto name = juce::String(composer::chordName(chord));
                g.drawFittedText(name, int(x) + 4, 25, std::max(1, int(width) - 6), 17,
                                 juce::Justification::centredLeft, 1);
            }
        }
    } else {
        g.setColour(muted.withAlpha(0.6f));
        g.drawText(interfaceText("HARMONY  /  create a sequence on Chords", interfaceLanguage),
                   int(r.getX()) + 4, 25, int(r.getWidth()) - 8, 17, juce::Justification::centredLeft);
    }
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    g.setColour(teal.withAlpha(0.035f));
    g.fillRect(selX, r.getY(), selW, r.getHeight());
    if (showReference) {
        for (int i = 0; i < reference.count; ++i) {
            const auto& n = reference.notes[i];
            bool unchanged = false;
            for (int j = 0; j < clip.count; ++j) {
                const auto& current = clip.notes[j];
                if (current.pitch == n.pitch && (!microtonal || current.cents == n.cents) &&
                    std::abs(current.start - n.start) < 0.001 &&
                    std::abs(current.duration - n.duration) < 0.001) {
                    unchanged = true;
                    break;
                }
            }
            if (!unchanged) {
                g.setColour(gold.withAlpha(0.55f));
                g.drawRoundedRectangle(noteRect(n).reduced(0.5f), 2, 1);
            }
        }
    }
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[i];
        auto bounds = noteRect(n);
        const float energy = playhead >= 0 ? sourceActivity[size_t(i)] : 0.0f;
        const auto colour = n.locked ? gold
                            : microtonal && n.cents != 0
                                ? violet
                                : teal.interpolatedWith(accent, float(n.pitch % 12) / 18.0f);
        if (energy > 0.01f) {
            g.setColour(colour.withAlpha(energy * 0.12f));
            g.fillRoundedRectangle(bounds.expanded(5, 3), 4);
            g.setColour(colour.withAlpha(energy * 0.18f));
            g.fillRoundedRectangle(bounds.expanded(2, 1), 3);
        }
        const auto fill = colour.withAlpha(0.65f + n.velocity / 127.0f * 0.3f)
                              .interpolatedWith(juce::Colours::white, energy * 0.45f);
        g.setGradientFill(juce::ColourGradient(fill.brighter(0.10f), bounds.getTopLeft(), fill.darker(0.17f),
                                               bounds.getBottomLeft(), false));
        g.fillRoundedRectangle(bounds, 2.5f);
        g.setColour(juce::Colours::white.withAlpha(0.16f + energy * 0.25f));
        g.fillRoundedRectangle(bounds.withHeight(1), 0.5f);
        if (i == chosen) {
            g.setColour(text);
            g.drawRoundedRectangle(bounds.reduced(0.5f), 2, 1);
        }
        if (bounds.getWidth() > 28 && bounds.getHeight() > 10) {
            g.setFont(font(9, true));
            g.setColour(background);
            g.drawText(noteName(n, microtonal), bounds.toNearestInt().reduced(3, 0),
                       juce::Justification::centredLeft);
        }
    }
    if (playhead >= 0) {
        const float x = r.getX() + float(playhead / clip.length) * r.getWidth();
        g.setGradientFill(
            juce::ColourGradient(teal.withAlpha(0.0f), x - 34, 0, teal.withAlpha(0.16f), x, 0, false));
        g.fillRect(x - 34, r.getY(), 34.0f, r.getHeight());
        g.setColour(teal.withAlpha(0.12f));
        g.fillRect(x - 3, r.getY(), 6.0f, r.getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.88f));
        g.drawLine(x, r.getY(), x, r.getBottom(), 1.2f);
        juce::Path marker;
        marker.addTriangle(x - 4, r.getY(), x + 4, r.getY(), x, r.getY() + 6);
        g.setColour(teal);
        g.fillPath(marker);
    }
    g.restoreState();
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[i];
        const float x = r.getX() + float(n.start / clip.length) * r.getWidth();
        g.setColour(i == chosen ? text : (n.locked ? gold.withAlpha(0.7f) : teal.withAlpha(0.6f)));
        const float h = n.velocity / 127.0f * 17;
        g.fillRect(x, r.getBottom() + 23 - h, 3.0f, h);
    }
    g.setColour(muted);
    g.setFont(font(9));
    g.drawText(chosen >= 0 ? "V " + juce::String(clip.notes[chosen].velocity)
                           : interfaceText("VEL", interfaceLanguage),
               4, getHeight() - 22,
               36, 20, juce::Justification::centredRight);
}
void PianoRoll::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    changed = false;
    changingVelocity = false;
    dragArmed = midiDragged = false;
    selectingTime = e.mods.isLeftButtonDown() && e.position.y < 24 && e.position.x >= gridArea().getX();
    if (selectingTime) {
        selectionAnchor = beatAt(e.position.x);
        selectionStart = selectionAnchor;
        selectionEnd = std::min(clip.length, selectionAnchor + clip.grid);
        if (onRegionChange)
            onRegionChange(selectionStart, selectionEnd);
        repaint();
        return;
    }
    if (!noteEditing || !editable) {
        chosen = -1;
        for (int i = clip.count - 1; i >= 0; --i)
            if (noteRect(clip.notes[i]).expanded(1).contains(e.position)) {
                chosen = i;
                break;
            }
        if (editable && chosen >= 0 && e.mods.isShiftDown() && e.mods.isLeftButtonDown()) {
            clip.notes[chosen].locked = !clip.notes[chosen].locked;
            if (onEdit)
                onEdit(clip);
        } else
            dragArmed = e.mods.isLeftButtonDown() && clip.count > 0;
        repaint();
        return;
    }
    if (e.position.y >= gridArea().getBottom() && e.position.x >= gridArea().getX()) {
        float distance = 9.0f;
        int nearest = -1;
        for (int i = 0; i < clip.count; ++i) {
            const float x = noteRect(clip.notes[i]).getX();
            if (std::abs(x - e.position.x) < distance) {
                distance = std::abs(x - e.position.x);
                nearest = i;
            }
        }
        if (nearest >= 0) {
            chosen = nearest;
            if (!clip.notes[chosen].locked) {
                changingVelocity = true;
                mouseDrag(e);
            }
        }
        return;
    }
    chosen = -1;
    for (int i = clip.count - 1; i >= 0; --i)
        if (noteRect(clip.notes[i]).expanded(1).contains(e.position)) {
            chosen = i;
            break;
        }
    if (chosen >= 0) {
        if (e.mods.isShiftDown()) {
            clip.notes[chosen].locked = !clip.notes[chosen].locked;
            if (onEdit)
                onEdit(clip);
        } else if (e.mods.isPopupMenu()) {
            if (!clip.notes[chosen].locked) {
                for (int i = chosen; i < clip.count - 1; ++i)
                    clip.notes[i] = clip.notes[i + 1];
                --clip.count;
                if (onEdit)
                    onEdit(clip);
                chosen = -1;
            }
        } else {
            original = clip.notes[chosen];
            resizingNote = e.position.x > noteRect(original).getRight() - 6;
        }
    } else if (gridArea().contains(e.position) && !e.mods.isPopupMenu() && clip.count < composer::maxNotes) {
        chosen = clip.count++;
        clip.notes[chosen] = {beatAt(e.position.x), clip.grid, pitchAt(e.position.y), 96, false};
        original = clip.notes[chosen];
        resizingNote = true;
        changed = true;
    }
    repaint();
}
void PianoRoll::mouseDrag(const juce::MouseEvent& e) {
    if (dragArmed && !midiDragged && e.getDistanceFromDragStart() > 5) {
        midiDragged = true;
        if (onMidiDrag)
            onMidiDrag();
        return;
    }
    if (selectingTime) {
        const double end = beatAt(e.position.x);
        selectionStart = std::min(selectionAnchor, end);
        selectionEnd = std::min(clip.length, std::max(selectionAnchor, end) + clip.grid);
        if (onRegionChange)
            onRegionChange(selectionStart, selectionEnd);
        repaint();
        return;
    }
    if (!editable || !noteEditing)
        return;
    if (chosen < 0 || clip.notes[chosen].locked || e.mods.isShiftDown() || e.mods.isPopupMenu())
        return;
    auto& n = clip.notes[chosen];
    if (changingVelocity)
        n.velocity =
            juce::jlimit(1, 127, int(std::round((gridArea().getBottom() + 23 - e.position.y) / 17 * 127)));
    else if (resizingNote)
        n.duration =
            juce::jlimit(clip.grid, clip.length - n.start, beatAt(e.position.x) + clip.grid - n.start);
    else {
        const double delta =
            std::round(e.getDistanceFromDragStartX() / gridArea().getWidth() * clip.length / clip.grid) *
            clip.grid;
        n.start = juce::jlimit(0.0, clip.length - n.duration, original.start + delta);
        n.pitch = juce::jlimit(0, 127,
                               original.pitch - int(std::round(e.getDistanceFromDragStartY() /
                                                               (gridArea().getHeight() / (high - low + 1)))));
    }
    changed = true;
    repaint();
}
void PianoRoll::mouseUp(const juce::MouseEvent&) {
    if (changed && onEdit)
        onEdit(clip);
    changed = false;
    selectingTime = changingVelocity = false;
    dragArmed = false;
}
void PianoRoll::mouseDoubleClick(const juce::MouseEvent&) {
    if (!noteEditing && onAudition)
        onAudition();
}
void PianoRoll::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    int shift = w.deltaY > 0 ? 2 : -2;
    if (low + shift >= 0 && high + shift <= 127) {
        low += shift;
        high += shift;
        repaint();
    }
}
bool PianoRoll::keyPressed(const juce::KeyPress& k) {
    if (!editable || chosen < 0)
        return false;
    auto& n = clip.notes[chosen];
    if (k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L')
        n.locked = !n.locked;
    else if (!noteEditing)
        return false;
    else if (!n.locked && (k.getKeyCode() == juce::KeyPress::deleteKey ||
                           k.getKeyCode() == juce::KeyPress::backspaceKey)) {
        for (int i = chosen; i < clip.count - 1; ++i)
            clip.notes[i] = clip.notes[i + 1];
        --clip.count;
        chosen = -1;
    } else if (!n.locked && k.getKeyCode() == juce::KeyPress::upKey)
        n.pitch = std::min(127, n.pitch + 1);
    else if (!n.locked && k.getKeyCode() == juce::KeyPress::downKey)
        n.pitch = std::max(0, n.pitch - 1);
    else
        return false;
    if (onEdit)
        onEdit(clip);
    repaint();
    return true;
}
VariationButton::VariationButton(int i) : juce::Button("Variation " + juce::String(i + 1)), slot(i) {
    addAndMakeVisible(pin);
    pin.setTooltip("Pin this variation to preserve it when generating a new bank.");
    pin.onClick = [this] {
        if (onPin)
            onPin();
    };
    setTooltip("Click to select. Double-click to audition. MIDI note " + juce::String(48 + i) +
               " triggers this variation.");
}
void VariationButton::setClip(const composer::Clip& c, bool active, bool showMicrotones) {
    clip = c;
    selected = active;
    microtonal = showMicrotones;
    pin.setButtonText(c.favourite ? "*" : "+");
    pin.setColour(juce::TextButton::textColourOffId, c.favourite ? gold : muted);
    repaint();
}
void VariationButton::setPlaybackState(bool isAudible, bool isQueued) {
    if (audible != isAudible || queued != isQueued) {
        audible = isAudible;
        queued = isQueued;
        repaint();
    }
}
void VariationButton::resized() {
    pin.setBounds(getWidth() - 29, 7, 22, 22);
}
void VariationButton::mouseDoubleClick(const juce::MouseEvent&) {
    if (onAudition)
        onAudition();
}
void VariationButton::advanceAnimation(double beat, float pulse, bool animate, float elapsed) {
    const float amount = animate ? 1.0f - std::exp(-elapsed * 16.0f) : 1.0f;
    const float nextHover = hoverMix + (float(isMouseOver(true)) - hoverMix) * amount;
    const float nextSelect = selectMix + (float(selected) - selectMix) * amount;
    const float nextEnergy = audible ? pulse : 0.0f;
    const bool changed = std::abs(nextHover - hoverMix) > 0.002f ||
                         std::abs(nextSelect - selectMix) > 0.002f ||
                         std::abs(nextEnergy - energy) > 0.005f || (audible && beat != beatPosition);
    hoverMix = nextHover;
    selectMix = nextSelect;
    energy = nextEnergy;
    beatPosition = beat;
    const float pinHover = float(pin.getProperties().getWithDefault("hoverAmount", 0.0f));
    const float nextPin = pinHover + (float(pin.isMouseOver()) - pinHover) * amount;
    pin.getProperties().set("hoverAmount", nextPin);
    if (std::abs(pinHover - nextPin) > 0.002f)
        pin.repaint();
    if (changed)
        repaint();
}
void VariationButton::paintButton(juce::Graphics& g, bool, bool down) {
    auto bounds = getLocalBounds().toFloat().reduced(0.7f);
    const auto tint = queued ? gold : selected ? accent : teal;
    const auto fill =
        panel.interpolatedWith(surface, hoverMix * 0.7f).interpolatedWith(tint, selectMix * 0.07f);
    g.setGradientFill(juce::ColourGradient(fill.brighter(down ? 0.01f : 0.05f), bounds.getTopLeft(),
                                           fill.darker(0.12f), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle(bounds, 8);
    g.setColour(line.interpolatedWith(tint, queued ? 0.75f : selectMix * 0.8f + hoverMix * 0.15f));
    g.drawRoundedRectangle(bounds, 8, 1);
    if (selected || queued || audible) {
        g.setColour(tint.withAlpha(0.7f + energy * 0.25f));
        g.fillRoundedRectangle(bounds.getX() + 9, bounds.getY(), bounds.getWidth() - 18, 2, 1);
    }
    g.setColour(selected ? accent : palette::text.withAlpha(0.8f));
    g.setFont(font(11, true));
    g.drawText(juce::String(slot + 1).paddedLeft('0', 2), 10, 6, 24, 18, juce::Justification::centredLeft);
    g.setFont(font(9));
    g.setColour(muted.withAlpha(0.8f));
    g.drawText(noteName(48 + slot), 36, 6, 40, 18, juce::Justification::centredLeft);
    if (audible || queued) {
        const float x = float(getWidth() - 37);
        g.setColour(tint.withAlpha(0.12f + energy * 0.15f));
        g.fillEllipse(x - 3, 9, 10, 10);
        g.setColour(tint);
        g.fillEllipse(x, 12, 4, 4);
    }
    auto r = bounds.reduced(10, 0).withTop(29).withBottom(bounds.getBottom() - 7);
    int lo = 127, hi = 0;
    for (int i = 0; i < clip.count; ++i) {
        lo = std::min(lo, clip.notes[i].pitch);
        hi = std::max(hi, clip.notes[i].pitch);
    }
    hi = std::max(hi, lo + 12);
    if (audible && beatPosition >= 0) {
        const float x = r.getX() + float(std::fmod(beatPosition, clip.length) / clip.length) * r.getWidth();
        g.setColour(teal.withAlpha(0.45f));
        g.drawLine(x, r.getY(), x, r.getBottom(), 1.0f);
    }
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[i];
        const bool sounding = audible && beatPosition >= n.start && beatPosition < n.start + n.duration;
        g.setColour(n.locked                     ? gold
                    : sounding                   ? palette::text
                    : microtonal && n.cents != 0 ? violet
                                                 : (selected ? accent : teal.withAlpha(0.65f)));
        const float pitch = n.pitch + (microtonal ? n.cents / 100.0f : 0.0f);
        g.fillRoundedRectangle(r.getX() + float(n.start / clip.length) * r.getWidth(),
                               r.getY() + (hi - pitch) / (hi - lo) * std::max(1.0f, r.getHeight() - 3),
                               std::max(2.0f, float(n.duration / clip.length) * r.getWidth() - 1), 2.5f, 1);
    }
}

class ComposerGuide final : public juce::Component {
  public:
    ComposerGuide() {
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
    std::function<void()> onClose;
    std::function<void(int)> onLanguage;
    void setLanguage(int languageIndex) {
        const int id = languageIndex == 1 ? 1 : 2;
        if (language.getSelectedId() == id)
            return;
        language.setSelectedId(id, juce::dontSendNotification);
        updateChapters();
    }
    void showChapter(int number) {
        chapter.setSelectedId(juce::jlimit(1, 11, number), juce::dontSendNotification);
        updateText();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getKeyCode() == juce::KeyPress::escapeKey && onClose) {
            onClose();
            return true;
        }
        return false;
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(palette::background.withAlpha(0.96f));
        g.setColour(palette::panel);
        g.fillRoundedRectangle(getLocalBounds().reduced(24).toFloat(), 12);
        g.setColour(palette::line);
        g.drawRoundedRectangle(getLocalBounds().reduced(24).toFloat(), 12, 1);
    }
    void resized() override {
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

  private:
    juce::Label title;
    juce::ComboBox chapter, language;
    juce::TextButton close{"Close / Esc"};
    juce::TextEditor body;
    void updateChapters() {
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
    void updateText() {
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
};

void TransportButton::setPlaying(bool active) {
    if (playing != active) {
        playing = active;
        setButtonText(active ? "Stop" : "Play");
        repaint();
    }
}
void TransportButton::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto background = findColour(juce::TextButton::buttonColourId);
    const bool light = background.getPerceivedBrightness() > 0.5f;
    getLookAndFeel().drawButtonBackground(g, *this, background, over, down);
    g.setColour(isEnabled() ? (light ? palette::background : palette::accent) : palette::muted);
    const float y = getHeight() * 0.5f;
    if (playing)
        g.fillRoundedRectangle(14, y - 5, 10, 10, 1);
    else {
        juce::Path triangle;
        triangle.startNewSubPath(14, y - 6);
        triangle.lineTo(14, y + 6);
        triangle.lineTo(24, y);
        triangle.closeSubPath();
        g.fillPath(triangle);
    }
    g.setFont(font(13, true));
    g.setColour(isEnabled() ? (light ? palette::background : palette::text) : palette::muted);
    g.drawText(getButtonText(), 34, 0, getWidth() - 39, getHeight(), juce::Justification::centredLeft);
}

using Editor = GeneticVSTComposerJUCEAudioProcessorEditor;
void Editor::attach(juce::ComboBox& box, const char* id) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(processor.parameters.getParameter(id));
    box.addItemList(p->choices, 1);
    addAndMakeVisible(box);
    comboAttachments.push_back(std::make_unique<ComboAttachment>(processor.parameters, id, box));
}
void Editor::attach(juce::Slider& s, const char* id, bool rotary) {
    s.setLookAndFeel(&look);
    s.setSliderStyle(rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(rotary ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight, false,
                      rotary ? 52 : 42, 18);
    if (rotary) {
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f,
                              true);
        s.textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
    }
    s.setDoubleClickReturnValue(true, processor.parameters.getParameter(id)->convertFrom0to1(
                                          processor.parameters.getParameter(id)->getDefaultValue()));
    addAndMakeVisible(s);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(processor.parameters, id, s));
    if (rotary) {
        s.textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
        s.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
        s.updateText();
    }
}
void Editor::attach(juce::ToggleButton& b, const char* id) {
    addAndMakeVisible(b);
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(processor.parameters, id, b));
}
GeneticVSTComposerJUCEAudioProcessorEditor::GeneticVSTComposerJUCEAudioProcessorEditor(
    GeneticVSTComposerJUCEAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    setLookAndFeel(&look);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    attach(root, "root");
    addAndMakeVisible(scale);
    scale.addItemList(GeneticVSTComposerJUCEAudioProcessor::extendedScaleNames(), 1);
    scale.onChange = [this] {
        if (scale.getSelectedId() > 0)
            processor.setScaleChoice(scale.getSelectedId() >= 10 ? scale.getSelectedId()
                                                                 : scale.getSelectedId() - 1);
    };
    scale.setTooltip("The main scale guides generation. Extra scales keep the original Scale automation "
                     "range and are stored separately.");
    addAndMakeVisible(bars);
    bars.addItemList({"1 bar", "2 bars", "3 bars", "4 bars", "6 bars", "8 bars", "12 bars", "16 bars"}, 1);
    bars.onChange = [this] {
        constexpr int values[]{1, 2, 3, 4, 6, 8, 12, 16};
        const int id = bars.getSelectedId();
        if (id >= 1 && id <= 8)
            processor.setPhraseBarCount(values[id - 1]);
    };
    bars.setTooltip("Phrase length for the next melody. 3, 6, 12 and 16 bars keep the original Length "
                    "automation on 1, 2, 4 and 8.");
    attach(grid, "grid");
    addAndMakeVisible(meter);
    meter.setEditableText(true);
    for (const auto& m : composer::timeSignatures())
        meter.addItem(juce::String(m.numerator) + "/" + juce::String(m.denominator), meter.getNumItems() + 1);
    meter.setTooltip("Choose a meter or type your own, e.g. 11/8. Numerator 1-32; denominator 1, 2, 4, 8, 16 "
                     "or 32. Applies to new generations.");
    meter.onChange = [this] { report(processor.setTimeSignature(meter.getText())); };
    attach(mode, "mode");
    attach(phraseType, "phraseType");
    phraseType.setTooltip("Riff: a short repeating hook. Motif: a recognisable idea with small changes. "
                          "Melody: a longer phrase with development and an ending.");
    addAndMakeVisible(melodyTab);
    addAndMakeVisible(chordsTab);
    melodyTab.getProperties().set("segment", true);
    chordsTab.getProperties().set("segment", true);
    melodyTab.onClick = [this] { showMelodyComposer(); };
    chordsTab.onClick = [this] { showChordComposer(); };
    melodyTab.setTooltip("Generate, play and export a melody, riff or motif.");
    chordsTab.setTooltip("Create the harmonic sequence, choose inversions and drag chord MIDI.");
    addAndMakeVisible(harmony);
    for (const auto& progression : composer::progressionPresets())
        harmony.addItem(progression.name, harmony.getNumItems() + 1);
    harmony.addItem("Custom chords...", 1000);
    harmony.addItem("Chord composer...", 1001);
    harmony.onChange = [this] {
        if (harmony.getSelectedId() == 1001) {
            showChordComposer();
            refresh();
        } else if (harmony.getSelectedId() == 1000) {
            showWorkflowPage(1);
            chordText.grabKeyboardFocus();
        } else
            processor.setProgressionPreset(harmony.getSelectedId() - 1);
    };
    attach(quality, "quality");
    attach(auditionSound, "auditionSound");
    auditionSound.setTooltip("Built-in audition tone. These sounds follow microtonal tuning too.");
    attach(customSearch, "customSearch");
    attach(searchPopulation, "searchPopulation");
    attach(searchGenerations, "searchGenerations");
    attach(crossover, "crossover");
    crossover.textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
    crossover.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100; };
    crossover.updateText();
    quality.setTooltip("Quick: 64 x 40. Balanced: 128 x 100. Deep: 256 x 300. Search tab: custom budget and "
                       "actual statistics.");
    searchPopulation.setTooltip("Individuals in each generation, 16-512. Applies when Custom budget is on.");
    searchGenerations.setTooltip(
        "Evolutionary generations, 1-2000. The initial population is evaluated separately.");
    crossover.setTooltip("Chance of crossing two tournament-selected parents before mutation.");
    addAndMakeVisible(searchDetails);
    searchDetails.onClick = [this] { showWorkflowPage(6); };
    addAndMakeVisible(searchInfo);
    searchInfo.setFont(font(10.5f));
    searchInfo.setColour(juce::Label::textColourId, muted);
    const char* ids[]{"diversity", "dynamics", "tension",  "valence", "arousal",
                      "weirdness", "space",    "mutation", "density"};
    const char* tips[]{
        "Diversity: variety of pitches and intervals while keeping the phrase recognisable.",
        "Dynamics: melodic motion and changes of register. This shapes the notes, not output volume.",
        "Jazziness: chromatic tension and less predictable harmonic colours. Turn Fit to scale off to hear "
        "the original pitches.",
        "Valence: preference for brighter consonance and upward melodic direction; a musical bias, not a "
        "mood measurement.",
        "Arousal: rhythmic energy, faster movement and a wider, higher register.",
        "Weirdness: unexpected rhythmic placement and larger melodic jumps.",
        "Pause Amount: room for silence between notes. Higher settings favour more space.",
        "Evolution: how far Evolve moves from the selected phrase. Locked notes always stay intact.",
        "Density: how many notes the search aims for. Turn up for busier riffs; Pause Amount independently "
        "controls silence."};
    for (int i = 0; i < int(knobs.size()); ++i) {
        attach(knobs[i], ids[i], true);
        knobs[i].setTooltip(tips[i]);
    }
    attach(low, "low");
    attach(high, "high");
    attach(swing, "swing");
    attach(gate, "gate");
    attach(humanize, "humanize");
    humanize.setTooltip("Small timing and velocity variation during playback and export. The piano roll "
                        "keeps the original notes. Zero leaves the performance unchanged.");
    attach(tempo, "tempo");
    attach(volume, "volume");
    low.textFromValueFunction = [](double v) { return noteName(int(v)); };
    high.textFromValueFunction = low.textFromValueFunction;
    low.valueFromTextFunction = [](const juce::String& input) {
        const auto value = input.trim();
        for (int pitch = 0; pitch < 128; ++pitch)
            if (noteName(pitch).equalsIgnoreCase(value))
                return double(pitch);
        return value.getDoubleValue();
    };
    high.valueFromTextFunction = low.valueFromTextFunction;
    low.updateText();
    high.updateText();
    for (auto* s : {&swing, &gate, &volume}) {
        s->textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
        s->valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
        s->updateText();
    }
    attach(snap, "snap");
    attach(withChords, "auditionChords");
    withChords.setTooltip("Play the melody and the active chord sequence together through the internal "
                          "sounds. Melody MIDI and chord MIDI remain separate exports.");
    attach(chordMidi, "chordMidiOut");
    chordMidi.setTooltip("While With chords is playing, also send the chord voices as MIDI notes on channel 16. "
                         "Turned off while MPE uses channels 2-16.");
    attach(lockRhythm, "lockRhythm");
    attach(follow, "follow");
    attach(monitor, "monitor");
    attach(quantiseSwitch, "quantiseSwitch");
    follow.setTooltip(
        "Start, stop and seek with the DAW transport. Audition turns this off for independent preview.");
    monitor.setTooltip("Built-in audition sound. Turn off when routing MIDI to another instrument.");
    lockRhythm.setTooltip("Keep all note starts and durations when evolving a phrase.");
    harmony.setTooltip("Choose a progression, enter Custom chords, or open Chord composer for creative "
                       "progressions, voiced chord audition and separate chord MIDI export.");
    mode.setTooltip("Melody on rhythm uses the selected variation as a rhythmic template.");
    preset.addItemList({"Starting point: Balanced", "Airy / spacious", "Rhythmic / driving",
                        "Chromatic / restless", "Bass / restrained"},
                       1);
    preset.setTextWhenNothingSelected("Starting points...");
    addAndMakeVisible(preset);
    preset.onChange = [this] { processor.applyPreset(preset.getSelectedId() - 1); };
    for (auto* b : {&generate, &evolve, &panic, &undo, &redo, &save, &load, &import, &exportBoth, &exportFile,
                    &help})
        addAndMakeVisible(*b);
    addAndMakeVisible(play);
    addAndMakeVisible(drag);
    addAndMakeVisible(roll);
    for (auto* l : {&status, &phraseTitle, &phraseInfo, &tempoInfo}) {
        addAndMakeVisible(*l);
        l->setFont(font(11));
        l->setColour(juce::Label::textColourId, muted);
    }
    phraseTitle.setFont(font(20, true));
    phraseTitle.setColour(juce::Label::textColourId, text);
    phraseInfo.setJustificationType(juce::Justification::centredRight);
    generate.setColour(juce::TextButton::buttonColourId, accent);
    generate.setColour(juce::TextButton::textColourOffId, background);
    play.setColour(juce::TextButton::buttonColourId, accent);
    play.setColour(juce::TextButton::textColourOffId, background);
    drag.setColour(juce::TextButton::buttonColourId, accent);
    drag.setColour(juce::TextButton::textColourOffId, background);
    generate.onClick = [this] {
        if (processor.isGenerating())
            processor.cancelGeneration();
        else
            processor.generate(false);
    };
    evolve.onClick = [this] { processor.generate(true); };
    play.onClick = [this] {
        if (processor.previewRequested() && !processor.isAuditioningChords())
            processor.setPreview(false);
        else
            processor.startAudition();
    };
    play.setTooltip("Audition the phrase with the built-in sound. Enables Internal sound and turns Follow "
                    "DAW off. Click again to stop.");
    panic.onClick = [this] { processor.panic(); };
    undo.onClick = [this] {
        processor.undo();
        refresh();
    };
    redo.onClick = [this] {
        processor.redo();
        refresh();
    };
    save.onClick = [this] { chooseFile(2); };
    load.onClick = [this] { chooseFile(3); };
    import.onClick = [this] { chooseFile(0); };
    exportBoth.onClick = [this] { chooseFile(7); };
    exportBoth.setTooltip("Export a type 1 MIDI file. Track 1 is the melody, including swing, gate, Fit to "
                          "scale, humanize and MPE. Track 2 is the exact chord voicing on channel 1.");
    exportFile.onClick = [this] { chooseFile(1); };
    drag.onClick = [this] { chooseFile(1); };
    drag.onDrag = [this] { startMidiDrag(); };
    drag.setTooltip("Drag this button onto an instrument track in your DAW. Click to save a MIDI file. "
                    "Export includes swing, gate and Fit to scale.");
    help.onClick = [this] { showGuide(); };
    help.setTooltip("Read the in-plugin guide: Polish / English, examples and shortcuts.");
    roll.onEdit = [this](composer::Clip c) {
        if (!processor.hasDraft())
            processor.setClip(processor.selectedIndex(), c);
    };
    roll.onMidiDrag = [this] { startMidiDrag(&roll); };
    roll.onAudition = [this] { processor.startAudition(); };
    addAndMakeVisible(editNotes);
    editNotes.onClick = [this] { roll.setNoteEditing(editNotes.getToggleState()); };
    editNotes.setTooltip("Optional note drawing and editing. Off: drag the piano roll to export MIDI; "
                         "double-click to audition.");
    roll.onRegionChange = [this](double start, double end) {
        regionInfo.setText("Beats " + juce::String(start, 2) + " - " + juce::String(end, 2),
                           juce::dontSendNotification);
    };
    for (auto* button : {&captureA,     &compare,       &clearA,        &allTime,      &exportRange,
                         &simplify,     &movement,      &answer,        &resolve,      &applyHarmony,
                         &clearHarmony, &importHarmony, &chordComposer, &importGroove, &captureGroove,
                         &applyGroove,  &arrange,       &combine,       &applyDraft,   &discardDraft})
        addAndMakeVisible(*button);
    for (auto* value : {&playbackInfo, &workflowInfo, &regionInfo}) {
        addAndMakeVisible(*value);
        value->setFont(font(11));
        value->setColour(juce::Label::textColourId, muted);
    }
    playbackInfo.setJustificationType(juce::Justification::centredRight);
    captureA.onClick = [this] {
        processor.captureComparison();
        refresh();
    };
    compare.onClick = [this] {
        processor.toggleComparison();
        refresh();
    };
    clearA.onClick = [this] {
        processor.clearComparison();
        refresh();
    };
    clearA.setTooltip("Remove the A reference and its ghost notes; return to B.");
    captureA.setTooltip("Save the displayed phrase as reference A, independently of the bank.");
    compare.setTooltip(
        "Compare saved A with the selected phrase B. Export always writes the displayed phrase.");
    quantiseSwitch.setTooltip(
        "Switch variations or A/B at the end of the audible loop. Immediate while stopped.");
    const char* tabs[]{"Edit region", "Chord text", "Groove", "Arrange", "Combine", "Micro", "Search"};
    const char* tabTips[]{
        "Change only the selected time range. Locked notes stay intact.",
        "Enter chord names or import a harmony timeline. The Chords page also generates voiced progressions.",
        "Transfer timing and velocity from a MIDI reference or a selected variation.",
        "Develop the selected idea into a four-part arrangement before keeping it.",
        "Combine rhythm, pitches and accents from different variations.",
        "Enable genetically chosen microtonal passing notes and configure their MIDI output.",
        "Set the genetic search budget and inspect actual generations, evaluations, crossovers and fitness."};
    for (int i = 0; i < 7; ++i) {
        auto& tab = workflowTabs[i];
        tab.setButtonText(tabs[i]);
        tab.setTooltip(tabTips[i]);
        addAndMakeVisible(tab);
        tab.onClick = [this, i] { showWorkflowPage(i); };
    }
    allTime.onClick = [this] { roll.selectAllTime(); };
    exportRange.onClick = [this] { chooseFile(6); };
    exportRange.setTooltip("Export just the selected ruler range, including a section of an arrangement.");
    auto regionAction = [this](composer::RegionAction action) {
        report(processor.applyRegion(roll.regionStart(), roll.regionEnd(), action));
    };
    simplify.onClick = [regionAction] { regionAction(composer::RegionAction::Simplify); };
    movement.onClick = [regionAction] { regionAction(composer::RegionAction::MoreMovement); };
    answer.onClick = [regionAction] { regionAction(composer::RegionAction::Answer); };
    resolve.onClick = [regionAction] { regionAction(composer::RegionAction::Resolve); };
    simplify.setTooltip("Thin unlocked notes whose onsets lie in the selected time range.");
    movement.setTooltip("Increase melodic movement inside the selected range.");
    answer.setTooltip("Create an answering variation inside the selected range.");
    resolve.setTooltip("Resolve the ending of this range towards the current chord or scale.");
    addAndMakeVisible(chordText);
    chordText.setFont(font(14));
    chordText.setTextToShowWhenEmpty("Cm:4 Fm:2 G7:2", muted);
    chordText.setTooltip("Chord:duration, separated by spaces. Durations are quarter-note beats, not bars.");
    chordText.onReturnKey = [this] { report(processor.setCustomHarmony(chordText.getText())); };
    applyHarmony.onClick = [this] { report(processor.setCustomHarmony(chordText.getText())); };
    clearHarmony.onClick = [this] {
        processor.clearCustomHarmony();
        chordText.clear();
        refresh();
    };
    importHarmony.onClick = [this] { chooseFile(4); };
    chordComposer.onClick = [this] { showChordComposer(); };
    chordComposer.setColour(juce::TextButton::textColourOffId, teal);
    chordComposer.setTooltip("Create creative progressions with Originality, audition inversions and "
                             "drag their MIDI from the Chords tab.");
    importGroove.onClick = [this] { chooseFile(5); };
    captureGroove.onClick = [this] {
        processor.useSelectedAsGroove();
        refresh();
    };
    for (auto* slider : {&grooveTiming, &grooveVelocity}) {
        addAndMakeVisible(*slider);
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
        slider->setRange(0, 1, 0.01);
        slider->setValue(0.5, juce::dontSendNotification);
        slider->textFromValueFunction = [](double value) {
            return juce::String(int(std::round(value * 100))) + "%";
        };
        slider->valueFromTextFunction = [](const juce::String& value) {
            return value.getDoubleValue() / 100;
        };
        slider->updateText();
    }
    grooveTiming.setTooltip(
        "Strength of note-start alignment to the groove reference. Locked notes stay intact.");
    grooveVelocity.setTooltip("Strength of accent transfer from the groove reference.");
    applyGroove.onClick = [this] {
        report(processor.applyGroove(float(grooveTiming.getValue()), float(grooveVelocity.getValue())));
    };
    arrange.onClick = [this] { report(processor.previewArrangement()); };
    for (auto* box : {&rhythmSource, &pitchSource, &accentSource}) {
        addAndMakeVisible(*box);
        for (int i = 1; i <= 12; ++i)
            box->addItem("Variation " + juce::String(i).paddedLeft('0', 2), i);
        box->setSelectedId(1, juce::dontSendNotification);
    }
    rhythmSource.setTooltip("Provides note starts, durations and phrase length.");
    pitchSource.setTooltip("Provides pitches, mapped by relative position through the source phrase.");
    accentSource.setTooltip(
        "Provides velocity accents, mapped by relative position through the source phrase.");
    combine.onClick = [this] {
        report(processor.previewCombination(rhythmSource.getSelectedId() - 1, pitchSource.getSelectedId() - 1,
                                            accentSource.getSelectedId() - 1));
    };
    applyDraft.onClick = [this] {
        processor.applyDraft();
        refresh();
    };
    discardDraft.onClick = [this] {
        processor.discardDraft();
        refresh();
    };
    applyDraft.setColour(juce::TextButton::buttonColourId, gold);
    applyDraft.setColour(juce::TextButton::textColourOffId, background);
    applyDraft.setTooltip("Replace the selected variation with this preview. Undo can restore it.");
    discardDraft.setTooltip("Discard the temporary preview and return to the committed phrase.");
    attach(microEnabled, "microEnabled");
    addAndMakeVisible(tuning);
    const auto tuningOptions = GeneticVSTComposerJUCEAudioProcessor::tuningNames();
    for (int i = 1; i < tuningOptions.size(); ++i)
        tuning.addItem(tuningOptions[i], i);
    tuning.onChange = [this] {
        if (tuning.getSelectedId() > 0) {
            auto* parameter = processor.parameters.getParameter("tuning");
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(tuning.getSelectedId())));
        }
    };
    microEnabled.onClick = [this] {
        if (microEnabled.getToggleState() &&
            processor.parameters.getRawParameterValue("tuning")->load() < 1) {
            auto* parameter = processor.parameters.getParameter("tuning");
            parameter->setValueNotifyingHost(parameter->convertTo0to1(1));
        }
    };
    attach(microOutput, "microOutput");
    attach(bendRange, "bendRange");
    bendRange.textFromValueFunction = [](double v) {
        return juce::String::fromUTF8("±") + juce::String(int(v));
    };
    bendRange.valueFromTextFunction = [](const juce::String& value) {
        return value.retainCharacters("0123456789").getDoubleValue();
    };
    bendRange.updateText();
    microEnabled.setTooltip("Enable microtonal generation. The genetic search chooses short passing notes "
                            "and resolves them into the phrase; the chord layer stays in 12-TET.");
    tuning.setTooltip("The microtonal colour available to the generator. Micro switches the feature on or "
                      "off; the algorithm chooses useful passing notes rather than retuning every note.");
    microOutput.setTooltip("Pitch bend: channel 1 for one melody voice. MPE: independent pitch per note "
                           "on channels 2-16. Internal preview needs no external setup.");
    bendRange.setTooltip("Set the receiving synth's pitch-bend range to this value. MPE uses ±48.");
    addAndMakeVisible(microInfo);
    microInfo.setFont(font(11));
    microInfo.setColour(juce::Label::textColourId, muted);
    microInfo.setText("The search places short microtonal passing notes and resolves them. "
                      "Play previews exact pitches; chord accompaniment stays in 12-TET.",
                      juce::dontSendNotification);
    for (int i = 0; i < 12; ++i) {
        variants[i] = std::make_unique<VariationButton>(i);
        addAndMakeVisible(*variants[i]);
        variants[i]->onClick = [this, i] {
            processor.selectClip(i);
            refresh();
        };
        variants[i]->onPin = [this, i] {
            processor.toggleFavourite(i);
            refresh();
        };
        variants[i]->onAudition = [this, i] {
            processor.selectClip(i);
            processor.startAudition();
            refresh();
        };
    }
    setResizable(true, true);
    setResizeLimits(870, 630, 1600, 1100);
    addAndMakeVisible(activity);
    addAndMakeVisible(motion);
    motion.setToggleState(true, juce::dontSendNotification);
    motion.setTooltip(
        "Animate playback highlights, hover transitions and MIDI activity. Turn off for reduced motion.");
    for (int i = 0; i < int(knobs.size()); ++i) {
        knobs[i].getProperties().set("accentColour", int((i == 7   ? violet
                                                          : i == 2 ? gold
                                                          : i == 5 ? teal
                                                                   : accent)
                                                             .getARGB()));
        knobs[i].getProperties().set("displayValue", knobs[i].valueToProportionOfLength(knobs[i].getValue()));
    }
    for (auto& tab : workflowTabs)
        tab.getProperties().set("segment", true);
    evolve.setColour(juce::TextButton::textColourOffId, violet);
    addAndMakeVisible(interfaceLanguageButton);
    interfaceLanguageButton.onClick = [this] {
        storeInterfaceLanguage(processor.interfaceLanguageIndex() == 1 ? 0 : 1);
    };
    interfaceLanguageButton.setTooltip("Interface language for this editor. Automation names stay in English.");
    if (juce::SystemStats::getEnvironmentVariable("COMPOSER_DISABLE_UI_PREFS", {}).isEmpty()) {
        juce::PropertiesFile::Options options;
        options.applicationName = "GeneticComposer";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Preferences";
        options.folderName = "Genetic Composer";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        preferences = std::make_unique<juce::PropertiesFile>(options);
        storeInterfaceLanguage(preferences->getIntValue("language", 0));
    } else
        applyInterfaceLanguage();
    setSize(1240, 900);
    updateMainPage();
    timerCallback();
    startTimerHz(60);
}
juce::String Editor::tr(const juce::String& english) const {
    return interfaceText(english, processor.interfaceLanguageIndex());
}
void Editor::storeInterfaceLanguage(int language) {
    processor.setInterfaceLanguage(language);
    if (preferences) {
        preferences->setValue("language", processor.interfaceLanguageIndex());
        preferences->saveIfNeeded();
    }
    if (guide)
        guide->setLanguage(processor.interfaceLanguageIndex());
    applyInterfaceLanguage();
    repaint();
}
void Editor::applyInterfaceLanguage() {
    const auto text = [this](juce::Button& button, const char* english) {
        button.setButtonText(tr(english));
    };
    text(generate, "Generate melody bank");
    text(evolve, "Evolve selected");
    text(panic, "Stop all");
    text(undo, "Undo");
    text(redo, "Redo");
    text(save, "Save bank");
    text(load, "Open bank");
    text(import, "Import MIDI");
    text(exportBoth, "Melody + chords");
    text(exportFile, "Export...");
    text(melodyTab, "Melody");
    text(chordsTab, "Chords");
    text(withChords, "With chords");
    text(chordMidi, "Chord MIDI");
    text(snap, "Fit to scale");
    text(lockRhythm, "Keep rhythm");
    text(follow, "Follow DAW");
    text(monitor, "Internal sound");
    text(microEnabled, "Micro");
    text(quantiseSwitch, "Switch at loop");
    text(editNotes, "Edit notes");
    text(motion, "Motion");
    text(allTime, "Whole phrase");
    text(exportRange, "Export range...");
    text(simplify, "Simplify");
    text(movement, "More movement");
    text(answer, "Answer");
    text(resolve, "Resolve");
    text(applyHarmony, "Apply chords");
    text(clearHarmony, "Clear");
    text(importHarmony, "Import harmony MIDI");
    text(chordComposer, "Chord composer...");
    text(importGroove, "Import groove MIDI");
    text(captureGroove, "Use selected");
    text(applyGroove, "Apply groove");
    text(arrange, "Preview arrangement");
    text(combine, "Preview combination");
    text(applyDraft, "Keep preview");
    text(discardDraft, "Discard");
    text(captureA, "Capture A");
    text(clearA, "Clear A");
    const char* tabs[]{"Edit region", "Chord text", "Groove", "Arrange", "Combine", "Micro", "Search"};
    for (int i = 0; i < 7; ++i)
        workflowTabs[i].setButtonText(tr(tabs[i]));
    interfaceLanguageButton.setButtonText(processor.interfaceLanguageIndex() == 1 ? "PL" : "EN");
    preset.setTextWhenNothingSelected(tr("Starting points..."));
    searchDetails.setButtonText(tr("Search settings..."));
    shownLanguage = processor.interfaceLanguageIndex();
    shownSearchSerial = ~uint64_t{0};
    shownStatusSerial = 0;
    roll.setInterfaceLanguage(shownLanguage);
}
GeneticVSTComposerJUCEAudioProcessorEditor::~GeneticVSTComposerJUCEAudioProcessorEditor() {
    stopTimer();
    chordPanel.reset();
    chooser.reset();
    setLookAndFeel(nullptr);
    for (auto& file : dragFiles)
        file.deleteFile();
}
void Editor::label(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, bool heading) {
    g.setColour(heading ? text : muted);
    g.setFont(font(heading ? 11 : 12, heading));
    g.drawText(tr(s), r, juce::Justification::centredLeft);
}
void Editor::paint(juce::Graphics& g) {
    g.fillAll(background);
    const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
    g.addTransform(juce::AffineTransform::scale(zoom));
    const int w = int(getWidth() / zoom), h = int(getHeight() / zoom);
    g.setGradientFill(juce::ColourGradient(background.brighter(0.03f), 0, 0, background.darker(0.2f),
                                           float(w), float(h), false));
    g.fillRect(0, 0, w, h);
    g.setGradientFill(juce::ColourGradient(violet.withAlpha(0.045f), float(w) * 0.45f, 0,
                                           violet.withAlpha(0.0f), float(w) * 0.45f, 450, true));
    g.fillRect(0, 0, w, h);
    auto panelSurface = [&](juce::Rectangle<float> bounds) {
        g.setColour(juce::Colours::black.withAlpha(0.17f));
        g.fillRoundedRectangle(bounds.translated(0, 3), 12);
        g.setGradientFill(juce::ColourGradient(panel.brighter(0.02f), bounds.getTopLeft(),
                                               panel.darker(0.08f), bounds.getBottomLeft(), false));
        g.fillRoundedRectangle(bounds, 12);
        g.setColour(line.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 12, 1);
        g.setColour(juce::Colours::white.withAlpha(0.025f));
        g.drawLine(bounds.getX() + 13, bounds.getY() + 1, bounds.getRight() - 13, bounds.getY() + 1);
    };
    if (!chordPage) {
        panelSurface({20, 92, 262, 266});
        panelSurface({20, 370, 262, float(h - 442)});
        panelSurface({296, 92, float(w - 308), 438});
    }
    g.setColour(line.withAlpha(0.65f));
    g.drawHorizontalLine(71, 20, float(w - 20));
    g.drawHorizontalLine(h - 64, 0, float(w));
    // A small, code-drawn double helix: the visual identity belongs to the instrument.
    for (int i = 0; i < 7; ++i) {
        const float y = 22 + i * 4.7f, x = 38 + std::sin(float(i * 0.8 + motionPhase)) * 11;
        g.setColour(teal.withAlpha(0.28f + onsetPulse * 0.22f));
        g.drawLine(x, y, 76 - x, y, 1);
        g.setColour(accent);
        g.fillEllipse(x - 2, y - 2, 4, 4);
        g.setColour(teal);
        g.fillEllipse(76 - x - 2, y - 2, 4, 4);
    }
    g.setColour(text);
    g.setFont(font(26, true));
    g.drawText("GENETIC", 70, 14, 200, 34, juce::Justification::centredLeft);
    g.setFont(font(10));
    g.setColour(muted);
    g.drawText("M I D I   C O M P O S E R", 72, 45, 200, 16, juce::Justification::centredLeft);
    if (chordPage) {
        label(g, "BPM", {650, h - 53, 34, 22});
        label(g, "Level", {784, h - 53, 34, 22});
        return;
    }
    label(g, "COMPOSITION", {36, 105, 220, 18}, true);
    label(g, "Root", {36, 131, 62, 16});
    label(g, "Scale", {108, 131, 168, 16});
    label(g, "Length", {36, 188, 74, 16});
    label(g, "Grid", {118, 188, 66, 16});
    label(g, "Meter", {192, 188, 74, 16});
    label(g, "Phrase type", {36, 244, 100, 16});
    label(g, "Source mode", {146, 244, 120, 16});
    label(g, shownHarmony.isNotEmpty() ? "Using sequence from Chords" : "Harmony preset / or open Chords",
          {36, 300, 230, 16});
    label(g, "CHARACTER", {36, 378, 230, 16}, true);
    const juce::String names[]{"Diversity", "Dynamics",     "Jazziness", "Valence", "Arousal",
                               "Weirdness", "Pause Amount", "Evolution", "Density"};
    for (int i = 0; i < int(knobs.size()); ++i) {
        g.setFont(font(10));
        g.setColour(muted);
        g.drawText(tr(names[i]), 36 + (i % 3) * 79, 456 + (i / 3) * 76, 72, 16, juce::Justification::centred);
    }
    label(g, "Low note", {36, 628, 104, 16});
    label(g, "High note", {158, 628, 108, 16});
    label(g, "Search", {36, 687, 56, 18});
    const int mainX = 304, mainW = w - mainX - 20;
    g.setColour(line);
    g.drawRoundedRectangle(float(mainX) - 0.5f, 173.5f, float(mainW) + 1, 271, 5, 1);
    label(g, "Swing", {mainX + 236, 498, 42, 24});
    label(g, "Gate", {mainX + 390, 498, 32, 24});
    label(g, "Humanize", {mainX + 530, 498, 64, 24});
    panelSurface({float(mainX), 570, float(mainW), 102});
    if (workflowPage == 2) {
        label(g, "Timing", {mainX + 12, 638, 46, 26});
        label(g, "Velocity", {mainX + 244, 638, 54, 26});
    }
    if (workflowPage == 3) {
        const char* parts[]{"A / original", "A' / develop", "B / answer", "A'' / ending"};
        const int partWidth = (mainW - 40) / 4;
        for (int i = 0; i < 4; ++i) {
            auto cell = juce::Rectangle<int>(mainX + 12 + i * (partWidth + 4), 603, partWidth, 28);
            g.setColour(i == 0 ? accent.withAlpha(0.15f) : surface);
            g.fillRoundedRectangle(cell.toFloat(), 4);
            g.setColour(i == 0 ? accent : text);
            g.setFont(font(12, true));
            g.drawText(tr(parts[i]), cell, juce::Justification::centred);
        }
    }
    if (workflowPage == 4) {
        const int width = (mainW - 48) / 3;
        label(g, "Rhythm", {mainX + 12, 595, width, 16});
        label(g, "Pitches", {mainX + 24 + width, 595, width, 16});
        label(g, "Accents", {mainX + 36 + width * 2, 595, width, 16});
    }
    if (workflowPage == 5) {
        label(g, "Bend", {mainX + 622, 602, 40, 28});
    }
    if (workflowPage == 6) {
        label(g, "Population", {mainX + 156, 602, 78, 28});
        label(g, "Generations", {mainX + 378, 602, 84, 28});
        label(g, "Crossover", {mainX + 610, 602, 76, 28});
    }
    label(g, "VARIATIONS", {mainX, 682, 150, 20}, true);
    g.setColour(muted);
    g.setFont(font(11));
    g.drawText(tr("Select  /  Double-click to play  /  + to pin"), mainX + 145, 682, mainW - 145, 20,
               juce::Justification::centredRight);
    label(g, "BPM", {650, h - 53, 34, 22});
    label(g, "Level", {784, h - 53, 34, 22});
    if (processor.isGenerating()) {
        g.setColour(line);
        g.fillRoundedRectangle(36, 713, 230, 3, 1.5f);
        g.setColour(accent);
        g.fillRoundedRectangle(36, 713, 230 * processor.generationProgress(), 3, 1.5f);
    }
}
void Editor::resized() {
    const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
    const int w = int(getWidth() / zoom), h = int(getHeight() / zoom), x = 304, mw = w - x - 20;
    melodyTab.setBounds(304, 23, 110, 32);
    chordsTab.setBounds(420, 23, 110, 32);
    activity.setBounds(20, h - 58, 224, 34);
    preset.setBounds(w - 550, 25, 188, 30);
    undo.setBounds(w - 350, 25, 54, 30);
    redo.setBounds(w - 290, 25, 54, 30);
    load.setBounds(w - 224, 25, 82, 30);
    save.setBounds(w - 136, 25, 82, 30);
    help.setBounds(w - 44, 25, 24, 30);
    interfaceLanguageButton.setBounds(210, 28, 42, 28);
    root.setBounds(36, 151, 62, 30);
    scale.setBounds(108, 151, 168, 30);
    bars.setBounds(36, 208, 74, 30);
    grid.setBounds(118, 208, 66, 30);
    meter.setBounds(192, 208, 74, 30);
    phraseType.setBounds(36, 264, 100, 30);
    mode.setBounds(146, 264, 120, 30);
    harmony.setBounds(36, 320, 230, 30);
    for (int i = 0; i < int(knobs.size()); ++i)
        knobs[i].setBounds(36 + (i % 3) * 79, 396 + (i / 3) * 76, 72, 60);
    low.setBounds(32, 649, 116, 24);
    high.setBounds(154, 649, 116, 24);
    quality.setBounds(94, 681, 172, 28);
    generate.setBounds(36, 720, 230, 34);
    evolve.setBounds(36, 762, 230, 34);
    searchDetails.setBounds(36, 804, 230, 24);
    lockRhythm.setBounds(x + 106, 498, 118, 26);
    editNotes.setBounds(x + 418, 456, 100, 30);
    phraseTitle.setBounds(x, 96, 320, 32);
    phraseInfo.setBounds(x + 330, 100, mw - 330, 26);
    applyDraft.setBounds(w - 240, 98, 132, 28);
    discardDraft.setBounds(w - 100, 98, 80, 28);
    captureA.setBounds(x, 135, 94, 28);
    compare.setBounds(x + 102, 135, 76, 28);
    clearA.setBounds(x + 186, 135, 72, 28);
    quantiseSwitch.setBounds(x + 268, 134, 136, 30);
    playbackInfo.setBounds(x + 414, 135, mw - 414, 28);
    roll.setBounds(x, 174, mw, 270);
    play.setBounds(x, 456, 72, 30);
    withChords.setBounds(x + 76, 456, 108, 30);
    chordMidi.setBounds(x + 188, 456, 108, 30);
    snap.setBounds(x + 300, 456, 112, 30);
    import.setBounds(x, 498, 96, 26);
    exportBoth.setBounds(w - 350, 456, 132, 30);
    exportFile.setBounds(w - 210, 456, 72, 30);
    drag.setBounds(w - 130, 456, 110, 30);
    swing.setBounds(x + 278, 498, 108, 24);
    gate.setBounds(x + 424, 498, 100, 24);
    humanize.setBounds(x + 596, 498, 110, 24);
    tempoInfo.setBounds(x + 716, 498, mw - 716, 24);
    const int tabWidth = (mw - 6 * 6) / 7;
    for (int i = 0; i < 7; ++i)
        workflowTabs[i].setBounds(x + i * (tabWidth + 6), 538, tabWidth, 28);
    workflowInfo.setBounds(x + 12, 573, mw - 24, 24);
    regionInfo.setBounds(x + 12, 602, mw - 320, 28);
    allTime.setBounds(w - 304, 602, 132, 28);
    exportRange.setBounds(w - 164, 602, 132, 28);
    const int regionWidth = (mw - 48) / 4;
    std::array<juce::TextButton*, 4> actions{&simplify, &movement, &answer, &resolve};
    for (int i = 0; i < 4; ++i)
        actions[i]->setBounds(x + 12 + i * (regionWidth + 8), 638, regionWidth, 28);
    chordText.setBounds(x + 12, 602, mw - 24, 28);
    applyHarmony.setBounds(x + 12, 638, 118, 28);
    clearHarmony.setBounds(x + 138, 638, 66, 28);
    importHarmony.setBounds(x + 212, 638, 188, 28);
    chordComposer.setBounds(x + 412, 638, mw - 424, 28);
    importGroove.setBounds(x + 12, 602, 174, 28);
    captureGroove.setBounds(x + 194, 602, 124, 28);
    grooveTiming.setBounds(x + 60, 638, 174, 26);
    grooveVelocity.setBounds(x + 300, 638, 174, 26);
    applyGroove.setBounds(w - 160, 638, 128, 28);
    arrange.setBounds(x + 12, 638, 186, 28);
    const int sourceWidth = (mw - 48) / 3;
    rhythmSource.setBounds(x + 12, 612, sourceWidth, 26);
    pitchSource.setBounds(x + 24 + sourceWidth, 612, sourceWidth, 26);
    accentSource.setBounds(x + 36 + 2 * sourceWidth, 612, sourceWidth, 26);
    combine.setBounds(x + 12, 642, 186, 26);
    microEnabled.setBounds(x + 12, 602, 92, 28);
    tuning.setBounds(x + 114, 602, 286, 28);
    microOutput.setBounds(x + 410, 602, 202, 28);
    bendRange.setBounds(x + 666, 603, mw - 678, 26);
    microInfo.setBounds(x + 12, 636, mw - 24, 31);
    customSearch.setBounds(x + 12, 602, 136, 28);
    searchPopulation.setBounds(x + 234, 603, 136, 26);
    searchGenerations.setBounds(x + 462, 603, 140, 26);
    crossover.setBounds(x + 686, 603, mw - 698, 26);
    searchInfo.setBounds(x + 12, 635, mw - 24, 34);
    const int gap = 8, cardW = (mw - 5 * gap) / 6, cardH = (h - 64 - 712 - 20) / 2;
    for (int i = 0; i < 12; ++i)
        variants[i]->setBounds(x + (i % 6) * (cardW + gap), 712 + (i / 6) * (cardH + 8), cardW, cardH);
    auditionSound.setBounds(256, h - 54, 122, 30);
    monitor.setBounds(386, h - 54, 134, 30);
    follow.setBounds(526, h - 54, 116, 30);
    tempo.setBounds(688, h - 53, 80, 26);
    volume.setBounds(820, h - 53, 94, 26);
    panic.setBounds(w - 250, h - 54, 80, 30);
    motion.setBounds(w - 116, h - 54, 96, 30);
    status.setBounds(20, h - 22, w - 40, 19);
    if (chordPanel)
        chordPanel->setBounds(20, 84, w - 40, h - 154);
    if (guide)
        guide->setBounds(0, 0, w, h);
    for (auto* child : getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*>(child) == nullptr &&
            dynamic_cast<juce::ResizableBorderComponent*>(child) == nullptr)
            child->setTransform(juce::AffineTransform::scale(zoom));
}
void Editor::report(const juce::Result& result) {
    if (result.failed())
        processor.setStatus(result.getErrorMessage());
    refresh();
}
void Editor::showWorkflowPage(int page) {
    if (chordPage)
        showMelodyComposer();
    workflowPage = juce::jlimit(0, 6, page);
    refresh();
    updateWorkflow();
    repaint();
}
void Editor::showGuide() {
    if (!guide) {
        guide = std::make_unique<ComposerGuide>();
        guide->onClose = [this] {
            guide->setVisible(false);
            grabKeyboardFocus();
        };
        guide->onLanguage = [this](int language) { storeInterfaceLanguage(language); };
        addChildComponent(*guide);
    }
    guide->setLanguage(processor.interfaceLanguageIndex());
    guide->setVisible(true);
    guide->toFront(true);
    resized();
}
void Editor::showChordComposer() {
    chordPage = true;
    if (!chordPanel) {
        chordPanel = std::make_unique<ChordComposerPanel>(processor);
        chordPanel->onShowHelp = [this] {
            showGuide();
            guide->showChapter(11);
        };
        chordPanel->onShowMelody = [this] { showMelodyComposer(); };
        addChildComponent(*chordPanel);
    }
    chordPanel->refreshDisplay();
    updateMainPage();
    resized();
    repaint();
}
void Editor::showMelodyComposer() {
    chordPage = false;
    updateMainPage();
    resized();
    repaint();
}
void Editor::updateMainPage() {
    for (auto* child : getChildren()) {
        const bool global = child == &melodyTab || child == &chordsTab || child == &activity ||
                            child == &preset || child == &undo || child == &redo || child == &load ||
                            child == &save || child == &help || child == &auditionSound ||
                            child == &monitor || child == &follow || child == &tempo || child == &volume ||
                            child == &panic || child == &motion || child == &interfaceLanguageButton ||
                            child == &status;
        if (child == chordPanel.get() || child == guide.get() ||
            dynamic_cast<juce::ResizableCornerComponent*>(child) != nullptr ||
            dynamic_cast<juce::ResizableBorderComponent*>(child) != nullptr)
            continue;
        child->setVisible(global || !chordPage);
    }
    if (chordPanel)
        chordPanel->setVisible(chordPage);
    melodyTab.setColour(juce::TextButton::buttonColourId, chordPage ? palette::surface : palette::teal);
    melodyTab.setColour(juce::TextButton::textColourOffId, chordPage ? palette::muted : palette::background);
    chordsTab.setColour(juce::TextButton::buttonColourId, chordPage ? palette::teal : palette::surface);
    chordsTab.setColour(juce::TextButton::textColourOffId, chordPage ? palette::background : palette::muted);
    if (!chordPage) {
        updateWorkflow();
        refresh();
    }
}
void Editor::updateWorkflow() {
    for (int i = 0; i < 7; ++i) {
        workflowTabs[i].setColour(juce::TextButton::buttonColourId, i == workflowPage ? teal : surface);
        workflowTabs[i].setColour(juce::TextButton::textColourOffId, i == workflowPage ? background : muted);
    }
    for (auto* component : std::initializer_list<juce::Component*>{&regionInfo, &allTime, &exportRange,
                                                                   &simplify, &movement, &answer, &resolve})
        component->setVisible(!chordPage && workflowPage == 0);
    for (auto* component : std::initializer_list<juce::Component*>{&chordText, &applyHarmony, &clearHarmony,
                                                                   &importHarmony, &chordComposer})
        component->setVisible(!chordPage && workflowPage == 1);
    for (auto* component : std::initializer_list<juce::Component*>{
             &importGroove, &captureGroove, &grooveTiming, &grooveVelocity, &applyGroove})
        component->setVisible(!chordPage && workflowPage == 2);
    arrange.setVisible(!chordPage && workflowPage == 3);
    for (auto* component :
         std::initializer_list<juce::Component*>{&rhythmSource, &pitchSource, &accentSource, &combine})
        component->setVisible(!chordPage && workflowPage == 4);
    for (auto* component : std::initializer_list<juce::Component*>{&microEnabled, &tuning, &microOutput,
                                                                   &bendRange, &microInfo})
        component->setVisible(!chordPage && workflowPage == 5);
    for (auto* component : std::initializer_list<juce::Component*>{
             &customSearch, &searchPopulation, &searchGenerations, &crossover, &searchInfo})
        component->setVisible(!chordPage && workflowPage == 6);
    const char* descriptions[]{
        "Drag the bar ruler to select a region. Locked notes and other regions stay intact.",
        "Enter chord:beats, e.g. Cm:4 Fm:2 G7:2, or open Chord composer to generate and export voiced "
        "chords.",
        "Import a reference or capture a variation, then apply its timing and accents.",
        "Turn the selected motif into four sections. Audition the preview before keeping it.",
        "Choose the sources below. Notes are matched by relative phrase position.",
        "",
        "Real genetic search: islands, one elite per island, crossover and adaptive mutation. No artificial "
        "delay."};
    workflowInfo.setText(workflowPage == 5 ? microDescription() : tr(descriptions[workflowPage]),
                         juce::dontSendNotification);
}
juce::String Editor::microDescription() const {
    const bool enabled = processor.parameters.getRawParameterValue("microEnabled")->load() > 0.5f;
    return enabled ? "Micro is on: the search chooses and resolves short passing notes. Select their colour "
                     "and MIDI output."
                   : "Micro is off: plain 12-tone melody. Enable it to let the search explore short "
                     "microtonal passing notes.";
}
void Editor::refresh() {
    const auto meterNow = processor.timeSignature();
    if (!meter.hasKeyboardFocus(true) && (meterNow.numerator != shownMeterNumerator ||
                                           meterNow.denominator != shownMeterDenominator)) {
        meter.setText(juce::String(meterNow.numerator) + "/" + juce::String(meterNow.denominator),
                      juce::dontSendNotification);
        shownMeterNumerator = meterNow.numerator;
        shownMeterDenominator = meterNow.denominator;
    }
    const bool custom = customSearch.getToggleState();
    quality.setEnabled(!custom);
    searchPopulation.setEnabled(custom);
    searchGenerations.setEnabled(custom);
    const bool busy = processor.isGenerating();
    const auto budget = processor.configuredSearchBudget();
    const auto searchSerial = processor.searchSerial();
    if (searchSerial != shownSearchSerial || busy != shownSearchBusy ||
        budget.population != shownBudgetPopulation || budget.generations != shownBudgetGenerations ||
        workflowPage != shownSearchPage) {
        if (searchSerial != shownSearchSerial || busy != shownSearchBusy ||
            budget.population != shownBudgetPopulation || budget.generations != shownBudgetGenerations) {
            const auto completedSearch = processor.searchSummary();
            shownSearchDescription = processor.searchDescription();
            searchDetails.setButtonText(completedSearch.isNotEmpty()
                                            ? tr(completedSearch)
                                            : juce::String(budget.population) + tr(" individuals / ") +
                                                  juce::String(budget.generations) + tr(" generations..."));
            searchDetails.setTooltip(tr(shownSearchDescription) + "\n" +
                                     tr("Click for budget and full search statistics."));
            shownSearchSerial = searchSerial;
            shownSearchBusy = busy;
            shownBudgetPopulation = budget.population;
            shownBudgetGenerations = budget.generations;
        }
        if (workflowPage == 6)
            searchInfo.setText(tr(shownSearchDescription), juce::dontSendNotification);
        shownSearchPage = workflowPage;
    }
    const auto version = processor.documentRevision();
    const int progression = processor.progressionIndex();
    if (version != shownRevision || progression != shownProgression) {
        harmony.setSelectedId(processor.customHarmonyText().isNotEmpty() ? 1000 : progression + 1,
                              juce::dontSendNotification);
        shownProgression = progression;
    }
    const int scaleNow = processor.scaleIndex();
    const int scaleId = scaleNow >= 10 ? scaleNow : scaleNow + 1;
    if (!scale.isPopupActive() && scale.getSelectedId() != scaleId)
        scale.setSelectedId(scaleId, juce::dontSendNotification);
    if (!bars.isPopupActive() && bars.getSelectedId() != processor.phraseLengthMenuId())
        bars.setSelectedId(processor.phraseLengthMenuId(), juce::dontSendNotification);
    const auto slot = processor.selectedIndex();
    const bool draft = processor.hasDraft();
    const int tuningNow = processor.tuningIndex(), output = processor.microtonalOutput();
    const int bend = int(processor.parameters.getRawParameterValue("bendRange")->load());
    const bool microtonal = processor.parameters.getRawParameterValue("microEnabled")->load() > 0.5f;
    const int rawTuning = std::max(1, int(processor.parameters.getRawParameterValue("tuning")->load()));
    tuning.setSelectedId(rawTuning, juce::dontSendNotification);
    const bool tuningChanged = tuningNow != shownTuning || output != shownOutput || bend != shownBend;
    if (tuningChanged) {
        if (workflowPage == 5)
            workflowInfo.setText(microDescription(), juce::dontSendNotification);
        scale.setEnabled(true);
        scale.setTooltip("The main scale guides generation. Micro adds passing-note colours without "
                         "replacing this scale.");
        roll.setMicrotonal(microtonal);
        shownTuning = tuningNow;
        shownOutput = output;
        shownBend = bend;
    }
    if (version != shownRevision || slot != shownSlot || tuningChanged) {
        auto bank = processor.bankSnapshot();
        for (int i = 0; i < 12; ++i)
            variants[i]->setClip(bank[i], i == slot, microtonal);
        const auto c = processor.displayedClip();
        roll.setClip(c, slot != shownSlot);
        roll.setReference(processor.comparisonClip(), processor.hasComparison());
        roll.setHarmony(processor.harmonySnapshot());
        phraseTitle.setText(draft ? (processor.draftDescription().startsWith("Arrangement")
                                         ? tr("PREVIEW / Arrangement")
                                         : tr("PREVIEW / Combined phrase"))
                                  : tr("Variation ") + juce::String(slot + 1).paddedLeft('0', 2),
                            juce::dontSendNotification);
        phraseTitle.setTooltip(draft ? tr(processor.draftDescription()) : juce::String());
        phraseTitle.setColour(juce::Label::textColourId, draft ? gold : text);
        phraseTitle.setFont(font(draft ? 17 : 20, true));
        int locked = 0, microtonalNotes = 0;
        for (int i = 0; i < c.count; ++i) {
            locked += c.notes[i].locked;
            microtonalNotes += c.notes[i].cents != 0;
        }
        phraseInfo.setText(tr(juce::String(c.count) + " notes   /   " +
                               juce::String(c.length / (c.numerator * 4.0 / c.denominator), 1) +
                               " bars   /   " + juce::String(locked) + " locked" +
                               (microtonal ? "   /   " + juce::String(microtonalNotes) + " microtonal" : "")),
                           juce::dontSendNotification);
        regionInfo.setText("Beats " + juce::String(roll.regionStart(), 2) + " - " +
                               juce::String(roll.regionEnd(), 2),
                           juce::dontSendNotification);
        const auto currentHarmony = processor.customHarmonyText();
        if (currentHarmony != shownHarmony) {
            chordText.setText(currentHarmony, false);
            shownHarmony = currentHarmony;
        }
        snap.setEnabled(true);
        snap.setTooltip("Fit melody pitches to the selected scale during playback and export, keeping "
                        "microtonal offsets. This can change borrowed chord tones. Turn off to hear "
                        "the generator's original pitches.");
        shownRevision = version;
        shownSlot = slot;
        repaint();
    }
    const bool hasChords = processor.chordClipSnapshot().count > 0;
    withChords.setEnabled(hasChords);
    chordMidi.setEnabled(hasChords && output != 2);
    if (output == 2 && processor.parameters.getRawParameterValue("chordMidiOut")->load() > 0.5f) {
        if (auto* chordMidiParameter = processor.parameters.getParameter("chordMidiOut"))
            chordMidiParameter->setValueNotifyingHost(0.0f);
        processor.setStatus("Chord MIDI stays off while MPE uses channels 2-16.");
    }
    withChords.setTooltip(
        hasChords ? "Play melody and chords together through separate internal sound layers. Enable Chord MIDI "
                    "to also send the chords on channel 16."
                  : "Create chords in the Chords tab first, then enable this to hear both parts together.");
    chordMidi.setTooltip(output == 2 ? "MPE uses channels 2-16 for the melody, so chord MIDI on channel 16 stays off."
                                     : "Send the sounding chord voices on MIDI channel 16 while With chords plays. "
                                       "Internal sound does not have to be on.");
    roll.setEditable(!draft);
    applyDraft.setVisible(draft && !chordPage);
    discardDraft.setVisible(draft && !chordPage);
    phraseInfo.setVisible(!draft && !chordPage);
    for (auto* action : {&simplify, &movement, &answer, &resolve})
        action->setEnabled(!draft && !processor.isGenerating());
    applyGroove.setEnabled(!draft && processor.hasGrooveReference() && !processor.isGenerating());
    microOutput.setEnabled(microtonal);
    tuning.setEnabled(microtonal);
    bendRange.setEnabled(output == 1);

    compare.setEnabled(processor.hasComparison());
    clearA.setEnabled(processor.hasComparison());
    compare.setButtonText(processor.isComparing() ? tr("Hear B") : tr("Hear A"));
    compare.setColour(juce::TextButton::textColourOffId, processor.isComparing() ? gold : text);
    captureGroove.setTooltip(processor.hasGrooveReference()
                                 ? "Replace the saved groove reference with the selected variation."
                                 : "Capture the selected variation as a groove reference.");
    undo.setEnabled(processor.canUndo() && !draft);
    redo.setEnabled(processor.canRedo() && !draft);
}
void Editor::advanceVisuals() {
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float elapsed = lastFrame > 0 ? float(juce::jlimit(0.001, 0.1, now - lastFrame)) : 1.0f / 60;
    lastFrame = now;
    const bool animate = motion.getToggleState() && !(guide && guide->isVisible());
    const auto state = processor.visualState();
    float midiEnergy = 0;
    for (float level : state.notes)
        midiEnergy = std::max(midiEnergy, level);
    if (state.noteOnSerial > seenNoteSerial)
        onsetPulse = 1.0f;
    else
        onsetPulse *= std::exp(-elapsed * 7.0f);
    seenNoteSerial = state.noteOnSerial;
    const bool running = processor.isPlaying(), searching = processor.isGenerating();
    const double reported = processor.playPosition();
    if (reported != lastReportedBeat) {
        lastReportedBeat = reported;
        beatReceivedAt = now;
    }
    // Extrapolate only between fresh audio block reports, and never across a
    // stopped transport. The audio thread remains the source of musical time.
    double beat = reported;
    if (running && animate && reported >= 0)
        beat += std::min(0.04, now - beatReceivedAt) * processor.tempo() / 60.0;
    const bool showingAudible = running && !processor.audibleChords() && !processor.audibleComparison() &&
                                processor.audibleIndex() == processor.selectedIndex();
    if (showingAudible)
        beat = std::fmod(std::max(0.0, beat), processor.displayedClip().length);
    roll.setPlayhead(showingAudible ? beat : -1);
    roll.setActivity(processor.audibleChords() ? GeneticVSTComposerJUCEAudioProcessor::VisualState{} : state,
                     showingAudible, animate, elapsed);
    activity.update(state.outputPeak, midiEnergy, state.noteOnSerial, running, searching,
                    processor.generationProgress(), reported, animate);
    for (auto& variant : variants)
        variant->advanceAnimation(reported, animate ? onsetPulse : midiEnergy, animate, elapsed);
    const float transition = animate ? 1.0f - std::exp(-elapsed * 18.0f) : 1.0f;
    for (auto* child : getChildren()) {
        if (!child->isVisible())
            continue;
        auto* button = dynamic_cast<juce::Button*>(child);
        auto* slider = dynamic_cast<juce::Slider*>(child);
        if (!button && !slider && !dynamic_cast<juce::ComboBox*>(child))
            continue;
        auto& properties = child->getProperties();
        const float hover = float(properties.getWithDefault("hoverAmount", 0.0f));
        const float next =
            hover + (float(child->isMouseOver(true) && child->isEnabled()) - hover) * transition;
        const float press = float(properties.getWithDefault("pressAmount", 0.0f));
        const float nextPress = press + (float(button && button->isDown()) - press) * transition;
        properties.set("hoverAmount", next);
        properties.set("pressAmount", nextPress);
        bool changed = std::abs(next - hover) > 0.002f || std::abs(nextPress - press) > 0.002f;
        if (slider && slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag) {
            const float target = float(slider->valueToProportionOfLength(slider->getValue()));
            const float shown = float(properties.getWithDefault("displayValue", target));
            const float value = shown + (target - shown) * transition;
            properties.set("displayValue", value);
            changed |= std::abs(value - shown) > 0.0001f;
        }
        if (changed)
            child->repaint();
    }
    if (animate && (running || searching || onsetPulse > 0.01f)) {
        motionPhase += elapsed * (searching ? 2.8 : running ? processor.tempo() / 80.0 : 0.4);
        const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
        repaint(juce::Rectangle<float>(18, 15, 45, 46)
                    .transformedBy(juce::AffineTransform::scale(zoom))
                    .toNearestInt());
    }
}
void Editor::timerCallback() {
    if (!isShowing() && shownRevision != 0) {
        if (timerRate != 10) {
            timerRate = 10;
            startTimerHz(timerRate);
        }
        return;
    }
    refreshDisplay();
}
void Editor::refreshDisplay() {
    const int desiredRate = motion.getToggleState() && !(guide && guide->isVisible()) ? 60 : 30;
    if (timerRate != desiredRate) {
        timerRate = desiredRate;
        startTimerHz(timerRate);
    }
    refresh();
    const auto statusSerial = processor.statusSerial();
    if (statusSerial != shownStatusSerial) {
        status.setText(processor.statusText(), juce::dontSendNotification);
        shownStatusSerial = statusSerial;
    }
    const bool busy = processor.isGenerating();
    generate.setButtonText(busy ? tr("Cancel search  ") +
                                      juce::String(int(processor.generationProgress() * 100)) + "%"
                                : tr("Generate melody bank"));
    evolve.setEnabled(!busy && !processor.hasDraft());
    generate.setEnabled(busy || !processor.hasDraft());
    play.setPlaying(processor.previewRequested() && !processor.isAuditioningChords());
    play.setButtonText(tr(play.getButtonText()));
    roll.setInterfaceLanguage(processor.interfaceLanguageIndex());
    tempoInfo.setText(juce::String(processor.tempo(), 1) + " BPM  /  " +
                          tr(processor.isPlaying() ? "PLAYING" : "READY"),
                      juce::dontSendNotification);
    const bool hearingChords = processor.audibleChords();
    const int audible = processor.audibleIndex(), queued = hearingChords ? -1 : processor.queuedIndex();
    const bool hearingA = processor.audibleComparison();
    const bool pendingCompare = !hearingChords && processor.isComparing() != hearingA;
    juce::String audibleText =
        hearingA ? "A reference"
                 : (processor.hasDraft() ? "Preview" : "B / " + juce::String(audible + 1).paddedLeft('0', 2));
    juce::String playback =
        processor.isPlaying() ? (hearingChords ? "Hearing chords" : "Hearing " + audibleText) : "Ready";
    if (processor.isPlaying() && (queued >= 0 || pendingCompare))
        playback +=
            "  >  " + juce::String(processor.isComparing() ? "A" : "B") +
            (queued >= 0 && !processor.isComparing() ? " / " + juce::String(queued + 1).paddedLeft('0', 2)
                                                     : "") +
            " next loop";
    playbackInfo.setText(tr(playback), juce::dontSendNotification);
    playbackInfo.setColour(juce::Label::textColourId, queued >= 0 || pendingCompare ? gold : muted);
    for (int i = 0; i < 12; ++i)
        variants[i]->setPlaybackState(processor.isPlaying() && !hearingChords && !hearingA &&
                                          !processor.hasDraft() && i == audible,
                                      i == queued);
    advanceVisuals();
    if (busy || busy != shownBusy) {
        const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
        repaint(juce::Rectangle<float>(32, 709, 238, 12)
                    .transformedBy(juce::AffineTransform::scale(zoom))
                    .toNearestInt());
    }
    shownBusy = busy;
}
bool Editor::keyPressed(const juce::KeyPress& k) {
    if (guide && guide->isVisible()) {
        if (k.getKeyCode() == juce::KeyPress::escapeKey)
            guide->onClose();
        return true;
    }
    for (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr;
         focused = focused->getParentComponent()) {
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr)
            return false;
        if (focused == this)
            break;
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey) {
        processor.panic();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) {
        if (processor.previewRequested())
            processor.setPreview(false);
        else if (chordPage)
            processor.startChordAudition();
        else
            processor.startAudition();
        return true;
    }
    if (k.getModifiers().isCommandDown() &&
        (k.getTextCharacter() == 'z' || k.getTextCharacter() == 'Z' || k.getKeyCode() == 'Z')) {
        if (k.getModifiers().isShiftDown())
            processor.redo();
        else
            processor.undo();
        refresh();
        return true;
    }
    return false;
}
bool Editor::isInterestedInFileDrag(const juce::StringArray& files) {
    return files.size() == 1 &&
           (files[0].endsWithIgnoreCase(".mid") || files[0].endsWithIgnoreCase(".midi") ||
            files[0].endsWithIgnoreCase(".genetic"));
}
void Editor::filesDropped(const juce::StringArray& files, int, int) {
    if (!isInterestedInFileDrag(files))
        return;
    juce::File file(files[0]);
    auto result = file.hasFileExtension("genetic") ? processor.loadBank(file) : processor.importMidi(file);
    if (result.failed())
        processor.setStatus(result.getErrorMessage());
    refresh();
}
void Editor::chooseFile(int action) {
    const bool saving = action == 1 || action == 2 || action == 6 || action == 7;
    const bool bankFile = action == 2 || action == 3;
    const juce::String title = action == 4   ? "Import harmony reference MIDI"
                               : action == 5 ? "Import groove reference MIDI"
                               : action == 6 ? "Export selected time range"
                               : action == 7 ? "Export melody and chords"
                               : saving      ? (bankFile ? "Save phrase bank" : "Export MIDI")
                                             : (bankFile ? "Open phrase bank" : "Import MIDI");
    chooser =
        std::make_unique<juce::FileChooser>(title,
                                            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                .getChildFile(saving ? (bankFile      ? "My phrases.genetic"
                                                                        : action == 6 ? "Selected range.mid"
                                                                        : action == 7 ? "Melody and chords.mid"
                                                                                      : "Variation.mid")
                                                                     : ""),
                                            bankFile ? "*.genetic" : "*.mid;*.midi");
    const int flags =
        saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
               : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    const double rangeStart = roll.regionStart(), rangeEnd = roll.regionEnd();
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(flags, [safe, action, rangeStart, rangeEnd](const juce::FileChooser& dialog) {
        if (!safe)
            return;
        auto file = dialog.getResult();
        if (file == juce::File())
            return;
        if (action == 1 || action == 6 || action == 7)
            file = file.withFileExtension("mid");
        if (action == 2)
            file = file.withFileExtension("genetic");
        auto& p = safe->processor;
        auto result = action == 0   ? p.importMidi(file)
                      : action == 1 ? p.exportMidi(file)
                      : action == 2 ? p.saveBank(file)
                      : action == 3 ? p.loadBank(file)
                      : action == 4 ? p.importHarmonyMidi(file)
                      : action == 5 ? p.importGrooveMidi(file)
                      : action == 7 ? p.exportMelodyAndChords(file)
                                    : p.exportMidiRange(file, rangeStart, rangeEnd);
        if (result.failed())
            p.setStatus(result.getErrorMessage());
        else if (action == 1 || action == 6 || action == 7)
            p.setStatus(action == 6   ? "Selected time range exported."
                        : action == 7 ? "Melody and chords exported."
                                      : "MIDI exported.");
        else if (action == 2 || action == 3)
            p.setStatus(action == 2 ? "Phrase bank saved." : "Phrase bank restored.");
        safe->refresh();
    });
}
void Editor::startMidiDrag(juce::Component* source) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "Genetic-Variation-" + juce::String(processor.selectedIndex() + 1), ".mid", false);
    auto result = processor.exportMidi(file);
    if (result.failed()) {
        processor.setStatus(result.getErrorMessage());
        return;
    }
    dragFiles.push_back(file);
    if (!juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false,
                                                                    source ? source : &drag))
        processor.setStatus("Drag could not start. Use Export to save this MIDI file.");
}
