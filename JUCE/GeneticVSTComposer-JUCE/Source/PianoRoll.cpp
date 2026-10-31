#include "PianoRoll.h"
#include "ComposerTheme.h"
#include "EditorFormat.h"
#include "InterfaceText.h"
#include <cmath>
using namespace palette;

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
