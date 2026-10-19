#include "PluginEditor.h"
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
} // namespace
PianoRoll::PianoRoll() {
    setWantsKeyboardFocus(true);
    setTooltip(
        "Click to add a note. Drag to move; drag its right edge to resize. Shift-click locks a note. "
        "Right-click or Delete removes it. Drag the bar ruler to select a region; drag in the velocity lane "
        "to edit velocity. Mouse wheel changes the visible register.");
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
    return {r.getX() + float(n.start / clip.length) * r.getWidth(), r.getY() + (high - n.pitch) * row + 1,
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
    g.drawText("BARS", 3, 1, 40, 20, juce::Justification::centredRight);
    if (harmony.count > 0) {
        static const char* qualities[]{"", "m", "dim", "+", "7", "maj7", "m7", "sus2", "sus4"};
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
                const auto name = juce::MidiMessage::getMidiNoteName(chord.root + 60, true, false, 4) +
                                  qualities[juce::jlimit(0, 8, chord.quality)];
                g.drawFittedText(name, int(x) + 4, 25, std::max(1, int(width) - 6), 17,
                                 juce::Justification::centredLeft, 1);
            }
        }
    } else {
        g.setColour(muted.withAlpha(0.6f));
        g.drawText("HARMONY  /  add chords in the Harmony tab", int(r.getX()) + 4, 25, int(r.getWidth()) - 8,
                   17, juce::Justification::centredLeft);
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
                if (current.pitch == n.pitch && std::abs(current.start - n.start) < 0.001 &&
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
        const auto colour = n.locked ? gold : teal.interpolatedWith(accent, float(n.pitch % 12) / 18.0f);
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
            g.drawText(noteName(n.pitch), bounds.toNearestInt().reduced(3, 0),
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
    g.drawText(chosen >= 0 ? "V " + juce::String(clip.notes[chosen].velocity) : "VEL", 4, getHeight() - 22,
               36, 20, juce::Justification::centredRight);
}
void PianoRoll::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    changed = false;
    changingVelocity = false;
    selectingTime = e.position.y < 24 && e.position.x >= gridArea().getX();
    if (selectingTime) {
        selectionAnchor = beatAt(e.position.x);
        selectionStart = selectionAnchor;
        selectionEnd = std::min(clip.length, selectionAnchor + clip.grid);
        if (onRegionChange)
            onRegionChange(selectionStart, selectionEnd);
        repaint();
        return;
    }
    if (!editable)
        return;
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
    if (selectingTime) {
        const double end = beatAt(e.position.x);
        selectionStart = std::min(selectionAnchor, end);
        selectionEnd = std::min(clip.length, std::max(selectionAnchor, end) + clip.grid);
        if (onRegionChange)
            onRegionChange(selectionStart, selectionEnd);
        repaint();
        return;
    }
    if (!editable)
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
void VariationButton::setClip(const composer::Clip& c, bool active) {
    clip = c;
    selected = active;
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
        g.setColour(n.locked ? gold : sounding ? palette::text : (selected ? accent : teal.withAlpha(0.65f)));
        g.fillRoundedRectangle(r.getX() + float(n.start / clip.length) * r.getWidth(),
                               r.getY() + float(hi - n.pitch) / (hi - lo) * std::max(1.0f, r.getHeight() - 3),
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
        language.onChange = [this] { updateChapters(); };
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
                 juce::String::fromUTF8("8. Studio One i skróty")},
                1);
        else
            chapter.addItemList({"1. Your first phrase", "2. Edit a region", "3. Your own harmony",
                                 "4. Compare A/B", "5. Groove and velocity", "6. Build an arrangement",
                                 "7. Combine variations", "8. Studio One and shortcuts"},
                                1);
        chapter.setSelectedId(previous, juce::dontSendNotification);
        updateText();
    }
    void updateText() {
        static const char* polish[]{
            R"(PIERWSZA FRAZA

1. Wybierz Root (tonikę), Scale (skalę), Length (długość) i Meter (metrum). Starting points ustawia punkt wyjścia dla charakteru melodii.

2. Kliknij Generate bank. Powstanie 12 propozycji. Kliknij kartę wariantu, a potem Play. Dwuklik karty też uruchamia odsłuch. Internal sound włącza prosty instrument do odsłuchu.

3. Znalazłeś ciekawy motyw? Capture A zachowa go do porównania. Plus na karcie przypina wariant, aby przetrwał następne generowanie. Evolve selected tworzy rozwinięcia wybranej frazy; Evolution określa preferowaną wielkość zmiany.

4. Przeciągnij Drag MIDI na ścieżkę instrumentu albo użyj Export. Plik zawiera aktualnie wyświetlaną frazę lub podgląd, wraz z przetwarzaniem Swing i Gate.

Density: gęstość nut. Variety: ruch wysokości i różnorodność. Syncopation: preferencja początków między uderzeniami. Tension: swoboda chromatyczna. Space: pauzy. Te parametry współdziałają; nie są dokładnymi procentami słyszalnej cechy.

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

Piano roll: kliknij pusty obszar, aby dodać nutę; przeciągnij nutę, aby zmienić pozycję i wysokość; przeciągnij prawą krawędź, aby zmienić długość. Shift-klik lub L blokuje nutę. Delete albo prawy klik usuwa odblokowaną nutę. Strzałki góra/dół zmieniają wysokość zaznaczonej nuty. Keep rhythm zachowuje rytm podczas ewolucji.)",
            R"(WŁASNA HARMONIA

Otwórz Harmony. Wpisz akordy wraz z długością, np.:

Cm:4 Fm:2 G7:2

Kliknij Apply chords. Cm trwa cztery ćwierćnuty, Fm dwie, G7 dwie. Liczby oznaczają ćwierćnuty, NIE takty. W 4/4 pełny takt to 4; w 7/8 pełny takt to 3.5. Długości mogą być ułamkowe.

Dostępne jakości: C, Cm, Cdim, Caug, C7, Cmaj7, Cm7, Csus2, Csus4. Możesz używać krzyżyków i bemoli. Pasek nad nutami pokazuje zastosowaną progresję. Krótsza progresja powtarza się przez frazę.

Własne akordy mają pierwszeństwo przed gotową progresją. Wpływają na generowanie i operacje muzyczne; samo Apply chords nie przepisuje istniejących nut. Przy aktywnej własnej harmonii Scale snap jest pomijany, aby globalna skala nie usuwała dźwięków wpisanych akordów. Clear przywraca działanie gotowej progresji i zwykłego Scale snap.

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

Velocity pojedynczej nuty: kliknij jej słupek w dolnym pasku VEL piano rollu i przeciągnij pionowo. Jaśniejszy słupek wskazuje wybór. Gdy kilka nut zaczyna się równocześnie, najłatwiej najpierw rozsunąć je albo edytować po eksporcie.

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

Follow DAW wiąże start, stop, pozycję i tempo odtwarzania z transportem hosta. Gdy jest włączone, przycisk Play jest nieaktywny. Wyłącz Follow DAW do niezależnego odsłuchu i wyzwalania wariantów nutami MIDI. Gdy używasz innego instrumentu do odsłuchu wyjścia MIDI, wyłącz Internal sound, aby uniknąć podwójnego dźwięku. Dostępność routingu MIDI zależy od formatu wtyczki i konfiguracji hosta.

MIDI 48–59: przytrzymaj, aby wyzwolić warianty 1–12.
MIDI 60–127: transpozycja; 60 oznacza oryginalną wysokość. Nazwy oktaw mogą różnić się w DAW.

Spacja: Play / Stop, gdy Follow DAW jest wyłączone.
Cmd/Ctrl+Z: Undo.
Cmd/Ctrl+Shift+Z: Redo.
Esc: zamknij instrukcję; poza instrukcją — Stop all.
Shift-klik lub L: zablokuj / odblokuj nutę.
Delete / prawy klik: usuń odblokowaną nutę.
Kółko myszy nad piano rollem: przewijaj wysokości.

Skróty odtwarzania nie działają podczas wpisywania tekstu. Save bank zachowuje bank i ustawienia; stan jest również zapisywany z projektem DAW. Tymczasowy podgląd należy wcześniej zachować przez Keep preview.)"};
        static const char* english[]{
            R"(YOUR FIRST PHRASE

Choose Root, Scale, Length and Meter, then Generate bank. Select one of the 12 variations and press Play; double-clicking a card also starts audition. Internal sound enables the built-in audition instrument.

Capture A keeps a reference for comparison. The plus on a card pins that variation so it survives another bank generation. Evolve selected develops the current phrase; Evolution controls the preferred amount of change.

Drag MIDI onto an instrument track, or use Export. Export writes the displayed phrase or draft, including Swing and Gate processing.

Density targets note density. Variety targets pitch movement and diversity. Syncopation favours off-beat starts. Tension permits chromatic movement. Space favours rests. These controls interact and are not exact percentages of perceptual qualities.

The header MIDI trace shows actual emitted note events; OUT meters the internal audio output. Piano keys respond to emitted notes. Turn Motion off in the bottom bar for reduced animation while transport and level indicators remain active.

Use the chapter selector above for detailed workflows. Escape closes this guide.)",
            R"(EDIT A REGION

Drag across the top bar-number ruler to select a time range. The green strip marks the selection. Region shows the endpoints in quarter-note beats; Whole phrase selects everything.

Simplify thins the selection. More movement increases melodic movement. Answer creates a contrasting response. Resolve steers the ending towards a stable scale or chord tone.

Only unlocked notes whose onsets fall inside the selected range are transformed. Other notes are preserved. These are musical algorithms, not natural-language AI commands. Undo reverses the operation. Export range saves the selection as a separate MIDI file; notes crossing its boundaries are cropped.

Example: select the final bar of a four-bar phrase and press Resolve. Audition the ending and compare it with a captured A reference.

Piano roll: click an empty area to add; drag a note to move; drag its right edge to resize. Shift-click or L toggles a lock. Delete or right-click removes an unlocked note. Up/down arrows change the selected note's pitch. Keep rhythm preserves note starts and durations during evolution.)",
            R"(YOUR OWN HARMONY

Open Harmony and enter, for example:

Cm:4 Fm:2 G7:2

Apply chords sets C minor for four quarter-note beats, F minor for two, then G dominant seventh for two. Durations are BEATS, not bars. One 4/4 bar is 4 beats; one 7/8 bar is 3.5. Fractional durations are supported.

Qualities: C, Cm, Cdim, Caug, C7, Cmaj7, Cm7, Csus2 and Csus4. Sharps and flats are supported. The strip above the notes shows the applied progression; a shorter progression repeats through the phrase.

Custom harmony overrides the preset progression. It guides generation and musical transforms; applying chords alone does not rewrite existing notes. Scale snap is bypassed while custom harmony is active, so a global scale cannot erase your chord tones. Clear restores preset harmony and normal Scale snap.

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

To edit one note's velocity, click its bar in the VEL lane below the piano roll and drag vertically. The selected bar is brighter. Notes with identical onsets share the same horizontal position.

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

Audition the draft shown in the piano roll. Keep preview writes it to the currently selected bank slot. Discard preserves the previous phrase. You can change source choices and preview again before deciding.

Locked notes in the Rhythm source are preserved in full: their pitch and velocity are not replaced. Locks in the Pitches and Accents sources do not constrain the result.

Capture A before starting for easy comparison. Pin important sources and Save bank to retain the complete source set.)",
            R"(STUDIO ONE AND SHORTCUTS

Audition through Internal sound, then drag Drag MIDI onto your instrument track in Studio One. Export saves a standard MIDI file for manual import.

Follow DAW synchronises playback position, tempo, start and stop with the host. Play is disabled while it is on. Turn Follow DAW off for independent audition and MIDI-note triggering. Disable Internal sound when another instrument receives the MIDI output. MIDI routing availability depends on the plugin format and host setup.

MIDI 48–59: hold to trigger variations 1–12.
MIDI 60–127: transpose; 60 means original pitch. DAW octave names may differ.

Space: Play / Stop when Follow DAW is off.
Cmd/Ctrl+Z: Undo.
Cmd/Ctrl+Shift+Z: Redo.
Esc: close the guide; outside it, Stop all.
Shift-click or L: toggle a note lock.
Delete / right-click: delete an unlocked note.
Mouse wheel over the roll: scroll pitches.

Playback shortcuts are suspended while typing text. Save bank stores the bank and settings; the DAW project also stores plugin state. Keep preview commits a temporary draft before saving.)"};
        const auto index = juce::jlimit(0, 7, chapter.getSelectedId() - 1);
        body.setText(juce::String::fromUTF8(language.getSelectedId() == 1 ? polish[index] : english[index]),
                     false);
        body.moveCaretToTop(false);
    }
};

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
    attach(scale, "scale");
    attach(bars, "bars");
    attach(grid, "grid");
    attach(meter, "meter");
    attach(mode, "mode");
    attach(harmony, "progression");
    attach(quality, "quality");
    const char* ids[]{"density", "diversity", "syncopation", "tension", "space", "mutation"};
    const char* tips[]{
        "How many notes fill a phrase. Higher values favour shorter, busier phrases.",
        "Pitch movement and variety. Lower values favour a more recognisable motif.",
        "Preference for off-beat note starts.",
        "Allows more chromatic movement and harmonic tension. Turn Scale snap off to hear chromatic notes.",
        "Room between notes. Higher values leave more silence.",
        "How far Evolve moves from the selected phrase. Locked notes always stay intact."};
    for (int i = 0; i < 6; ++i) {
        attach(knobs[i], ids[i], true);
        knobs[i].setTooltip(tips[i]);
    }
    attach(low, "low");
    attach(high, "high");
    attach(swing, "swing");
    attach(gate, "gate");
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
    attach(lockRhythm, "lockRhythm");
    attach(follow, "follow");
    attach(monitor, "monitor");
    attach(quantiseSwitch, "quantiseSwitch");
    follow.setTooltip(
        "Start, stop and seek with the DAW transport. Turn off for the Play button and MIDI-key triggering.");
    monitor.setTooltip("Built-in audition sound. Turn off when routing MIDI to another instrument.");
    lockRhythm.setTooltip("Keep all note starts and durations when evolving a phrase.");
    harmony.setTooltip(
        "One diatonic chord per bar, repeating every four bars. Chords follow your selected scale.");
    mode.setTooltip("Melody on rhythm uses the selected variation as a rhythmic template.");
    preset.addItemList({"Starting point: Balanced", "Airy / spacious", "Rhythmic / driving",
                        "Chromatic / restless", "Bass / restrained"},
                       1);
    preset.setTextWhenNothingSelected("Starting points...");
    addAndMakeVisible(preset);
    preset.onChange = [this] { processor.applyPreset(preset.getSelectedId() - 1); };
    for (auto* b :
         {&generate, &evolve, &play, &panic, &undo, &redo, &save, &load, &import, &exportFile, &help})
        addAndMakeVisible(*b);
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
    play.onClick = [this] { processor.setPreview(!processor.previewRequested()); };
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
    exportFile.onClick = [this] { chooseFile(1); };
    drag.onClick = [this] { chooseFile(1); };
    drag.onDrag = [this] { startMidiDrag(); };
    drag.setTooltip("Drag this button onto an instrument track in your DAW. Click to save a MIDI file. "
                    "Export includes swing, gate and scale snap.");
    help.onClick = [this] { showGuide(); };
    help.setTooltip("Read the in-plugin guide: Polish / English, examples and shortcuts.");
    roll.onEdit = [this](composer::Clip c) {
        if (!processor.hasDraft())
            processor.setClip(processor.selectedIndex(), c);
    };
    roll.onRegionChange = [this](double start, double end) {
        regionInfo.setText("Beats " + juce::String(start, 2) + " - " + juce::String(end, 2),
                           juce::dontSendNotification);
    };
    for (auto* button : {&captureA, &compare, &clearA, &allTime, &exportRange, &simplify, &movement, &answer,
                         &resolve, &applyHarmony, &clearHarmony, &importHarmony, &importGroove,
                         &captureGroove, &applyGroove, &arrange, &combine, &applyDraft, &discardDraft})
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
    const char* tabs[]{"Region", "Harmony", "Groove", "Arrange", "Combine"};
    for (int i = 0; i < 5; ++i) {
        auto& tab = workflowTabs[i];
        tab.setButtonText(tabs[i]);
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
            processor.setPreview(true);
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
    for (int i = 0; i < 6; ++i) {
        knobs[i].getProperties().set("accentColour", int((i == 5   ? violet
                                                          : i == 3 ? gold
                                                          : i == 2 ? teal
                                                                   : accent)
                                                             .getARGB()));
        knobs[i].getProperties().set("displayValue", knobs[i].valueToProportionOfLength(knobs[i].getValue()));
    }
    for (auto& tab : workflowTabs)
        tab.getProperties().set("segment", true);
    evolve.setColour(juce::TextButton::textColourOffId, violet);
    setSize(1240, 900);
    updateWorkflow();
    timerCallback();
    startTimerHz(60);
}
GeneticVSTComposerJUCEAudioProcessorEditor::~GeneticVSTComposerJUCEAudioProcessorEditor() {
    stopTimer();
    chooser.reset();
    setLookAndFeel(nullptr);
    for (auto& file : dragFiles)
        file.deleteFile();
}
void Editor::label(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, bool heading) {
    g.setColour(heading ? text : muted);
    g.setFont(font(heading ? 11 : 12, heading));
    g.drawText(s, r, juce::Justification::centredLeft);
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
    panelSurface({20, 92, 262, 266});
    panelSurface({20, 370, 262, float(h - 442)});
    panelSurface({296, 92, float(w - 308), 438});
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
    label(g, "COMPOSITION", {36, 105, 220, 18}, true);
    label(g, "Root", {36, 131, 62, 16});
    label(g, "Scale", {108, 131, 158, 16});
    label(g, "Length", {36, 188, 74, 16});
    label(g, "Grid", {118, 188, 66, 16});
    label(g, "Meter", {192, 188, 74, 16});
    label(g, "Generate", {36, 244, 230, 16});
    label(g, shownHarmony.isNotEmpty() ? "Harmony / custom timeline active" : "Harmony / one chord per bar",
          {36, 300, 230, 16});
    label(g, "CHARACTER", {36, 378, 230, 16}, true);
    const juce::String names[]{"Density", "Variety", "Syncopation", "Tension", "Space", "Evolution"};
    for (int i = 0; i < 6; ++i) {
        g.setFont(font(11));
        g.setColour(muted);
        g.drawText(names[i], 36 + (i % 3) * 79, 472 + (i / 3) * 96, 72, 16, juce::Justification::centred);
    }
    label(g, "Low note", {36, 585, 104, 16});
    label(g, "High note", {158, 585, 108, 16});
    label(g, "Search", {36, 650, 56, 18});
    const int mainX = 304, mainW = w - mainX - 20;
    g.setColour(line);
    g.drawRoundedRectangle(float(mainX) - 0.5f, 173.5f, float(mainW) + 1, 271, 5, 1);
    label(g, "Swing", {mainX, 498, 45, 24});
    label(g, "Gate", {mainX + 230, 498, 38, 24});
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
            g.drawText(parts[i], cell, juce::Justification::centred);
        }
    }
    if (workflowPage == 4) {
        const int width = (mainW - 48) / 3;
        label(g, "Rhythm", {mainX + 12, 595, width, 16});
        label(g, "Pitches", {mainX + 24 + width, 595, width, 16});
        label(g, "Accents", {mainX + 36 + width * 2, 595, width, 16});
    }
    label(g, "VARIATIONS", {mainX, 682, 150, 20}, true);
    g.setColour(muted);
    g.setFont(font(11));
    g.drawText("Select  /  Double-click to play  /  + to pin", mainX + 145, 682, mainW - 145, 20,
               juce::Justification::centredRight);
    label(g, "BPM", {405, h - 53, 34, 22});
    label(g, "Level", {552, h - 53, 34, 22});
    if (processor.isGenerating()) {
        g.setColour(line);
        g.fillRoundedRectangle(36, 678, 230, 3, 1.5f);
        g.setColour(accent);
        g.fillRoundedRectangle(36, 678, 230 * processor.generationProgress(), 3, 1.5f);
    }
    g.setColour(muted.withAlpha(0.6f));
    g.setFont(font(10));
    g.drawText("GENETIC  /  2.2", w - 256, h - 51, 122, 22, juce::Justification::centredRight);
}
void Editor::resized() {
    const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
    const int w = int(getWidth() / zoom), h = int(getHeight() / zoom), x = 304, mw = w - x - 20;
    activity.setBounds(304, 19, 224, 42);
    preset.setBounds(w - 550, 25, 188, 30);
    undo.setBounds(w - 350, 25, 54, 30);
    redo.setBounds(w - 290, 25, 54, 30);
    load.setBounds(w - 224, 25, 82, 30);
    save.setBounds(w - 136, 25, 82, 30);
    help.setBounds(w - 44, 25, 24, 30);
    root.setBounds(36, 151, 62, 30);
    scale.setBounds(108, 151, 158, 30);
    bars.setBounds(36, 208, 74, 30);
    grid.setBounds(118, 208, 66, 30);
    meter.setBounds(192, 208, 74, 30);
    mode.setBounds(36, 264, 230, 30);
    harmony.setBounds(36, 320, 230, 30);
    for (int i = 0; i < 6; ++i)
        knobs[i].setBounds(36 + (i % 3) * 79, 396 + (i / 3) * 96, 72, 76);
    low.setBounds(32, 606, 116, 26);
    high.setBounds(154, 606, 116, 26);
    quality.setBounds(94, 644, 172, 28);
    generate.setBounds(36, 685, 230, 36);
    evolve.setBounds(36, 685 + 42, 230, 36);
    lockRhythm.setBounds(x + 240, 456, 120, 30);
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
    import.setBounds(x, 456, 100, 30);
    snap.setBounds(x + 110, 456, 120, 30);
    exportFile.setBounds(w - 250, 456, 92, 30);
    drag.setBounds(w - 150, 456, 130, 30);
    swing.setBounds(x + 45, 498, 164, 24);
    gate.setBounds(x + 268, 498, 164, 24);
    tempoInfo.setBounds(x + 460, 498, mw - 460, 24);
    const int tabWidth = (mw - 4 * 6) / 5;
    for (int i = 0; i < 5; ++i)
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
    const int gap = 8, cardW = (mw - 5 * gap) / 6, cardH = (h - 64 - 712 - 20) / 2;
    for (int i = 0; i < 12; ++i)
        variants[i]->setBounds(x + (i % 6) * (cardW + gap), 712 + (i / 6) * (cardH + 8), cardW, cardH);
    play.setBounds(20, h - 54, 70, 30);
    follow.setBounds(102, h - 54, 120, 30);
    monitor.setBounds(235, h - 54, 145, 30);
    tempo.setBounds(446, h - 53, 92, 26);
    volume.setBounds(592, h - 53, 108, 26);
    panic.setBounds(720, h - 54, 80, 30);
    motion.setBounds(w - 116, h - 54, 96, 30);
    status.setBounds(20, h - 22, w - 40, 19);
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
    workflowPage = juce::jlimit(0, 4, page);
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
        addChildComponent(*guide);
    }
    guide->setVisible(true);
    guide->toFront(true);
    resized();
}
void Editor::updateWorkflow() {
    for (int i = 0; i < 5; ++i) {
        workflowTabs[i].setColour(juce::TextButton::buttonColourId, i == workflowPage ? teal : surface);
        workflowTabs[i].setColour(juce::TextButton::textColourOffId, i == workflowPage ? background : muted);
    }
    for (auto* component : std::initializer_list<juce::Component*>{&regionInfo, &allTime, &exportRange,
                                                                   &simplify, &movement, &answer, &resolve})
        component->setVisible(workflowPage == 0);
    for (auto* component :
         std::initializer_list<juce::Component*>{&chordText, &applyHarmony, &clearHarmony, &importHarmony})
        component->setVisible(workflowPage == 1);
    for (auto* component : std::initializer_list<juce::Component*>{
             &importGroove, &captureGroove, &grooveTiming, &grooveVelocity, &applyGroove})
        component->setVisible(workflowPage == 2);
    arrange.setVisible(workflowPage == 3);
    for (auto* component :
         std::initializer_list<juce::Component*>{&rhythmSource, &pitchSource, &accentSource, &combine})
        component->setVisible(workflowPage == 4);
    const char* descriptions[]{
        "Drag the bar ruler to select a region. Locked notes and other regions stay intact.",
        "Chord:beats, e.g. Cm:4 Fm:2 G7:2. Quarter-note beats; shorter progressions repeat.",
        "Import a reference or capture a variation, then apply its timing and accents.",
        "Turn the selected motif into four sections. Audition the preview before keeping it.",
        "Choose the sources below. Notes are matched by relative phrase position."};
    workflowInfo.setText(descriptions[workflowPage], juce::dontSendNotification);
}
void Editor::refresh() {
    const auto version = processor.documentRevision();
    const auto slot = processor.selectedIndex();
    const bool draft = processor.hasDraft();
    if (version != shownRevision || slot != shownSlot) {
        auto bank = processor.bankSnapshot();
        for (int i = 0; i < 12; ++i)
            variants[i]->setClip(bank[i], i == slot);
        const auto c = processor.displayedClip();
        roll.setClip(c, slot != shownSlot);
        roll.setReference(processor.comparisonClip(), processor.hasComparison());
        roll.setHarmony(processor.harmonySnapshot());
        phraseTitle.setText(draft ? (processor.draftDescription().startsWith("Arrangement")
                                         ? "PREVIEW / Arrangement"
                                         : "PREVIEW / Combined phrase")
                                  : "Variation " + juce::String(slot + 1).paddedLeft('0', 2),
                            juce::dontSendNotification);
        phraseTitle.setTooltip(draft ? processor.draftDescription() : juce::String());
        phraseTitle.setColour(juce::Label::textColourId, draft ? gold : text);
        phraseTitle.setFont(font(draft ? 17 : 20, true));
        int locked = 0;
        for (int i = 0; i < c.count; ++i)
            locked += c.notes[i].locked;
        phraseInfo.setText(juce::String(c.count) + " notes   /   " +
                               juce::String(c.length / (c.numerator * 4.0 / c.denominator), 1) +
                               " bars   /   " + juce::String(locked) + " locked",
                           juce::dontSendNotification);
        regionInfo.setText("Beats " + juce::String(roll.regionStart(), 2) + " - " +
                               juce::String(roll.regionEnd(), 2),
                           juce::dontSendNotification);
        const auto currentHarmony = processor.customHarmonyText();
        if (currentHarmony != shownHarmony) {
            chordText.setText(currentHarmony, false);
            shownHarmony = currentHarmony;
        }
        const bool customHarmony = processor.harmonySnapshot().count > 0;
        harmony.setEnabled(!customHarmony);
        snap.setEnabled(!customHarmony);
        snap.setTooltip(customHarmony ? "Bypassed while custom harmony is active. Clear the custom timeline "
                                        "to restore scale snapping."
                                      : "Snap outgoing notes to the selected scale.");
        shownRevision = version;
        shownSlot = slot;
        repaint();
    }
    roll.setEditable(!draft);
    applyDraft.setVisible(draft);
    discardDraft.setVisible(draft);
    phraseInfo.setVisible(!draft);
    for (auto* action : {&simplify, &movement, &answer, &resolve})
        action->setEnabled(!draft && !processor.isGenerating());
    applyGroove.setEnabled(!draft && processor.hasGrooveReference() && !processor.isGenerating());
    compare.setEnabled(processor.hasComparison());
    clearA.setEnabled(processor.hasComparison());
    compare.setButtonText(processor.isComparing() ? "Hear B" : "Hear A");
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
    const bool showingAudible =
        running && !processor.audibleComparison() && processor.audibleIndex() == processor.selectedIndex();
    if (showingAudible)
        beat = std::fmod(std::max(0.0, beat), processor.displayedClip().length);
    roll.setPlayhead(showingAudible ? beat : -1);
    roll.setActivity(state, showingAudible, animate, elapsed);
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
    status.setText(processor.statusText(), juce::dontSendNotification);
    const bool busy = processor.isGenerating();
    generate.setButtonText(busy ? "Cancel search" : "Generate bank");
    evolve.setEnabled(!busy && !processor.hasDraft());
    generate.setEnabled(busy || !processor.hasDraft());
    play.setButtonText(processor.previewRequested() ? "Stop" : "Play");
    play.setEnabled(!follow.getToggleState());
    tempoInfo.setText(juce::String(processor.tempo(), 1) + " BPM  /  " +
                          (processor.isPlaying() ? "PLAYING" : "READY"),
                      juce::dontSendNotification);
    const int audible = processor.audibleIndex(), queued = processor.queuedIndex();
    const bool hearingA = processor.audibleComparison();
    const bool pendingCompare = processor.isComparing() != hearingA;
    juce::String audibleText =
        hearingA ? "A reference"
                 : (processor.hasDraft() ? "Preview" : "B / " + juce::String(audible + 1).paddedLeft('0', 2));
    juce::String playback = processor.isPlaying() ? "Hearing " + audibleText : "Ready";
    if (processor.isPlaying() && (queued >= 0 || pendingCompare))
        playback +=
            "  >  " + juce::String(processor.isComparing() ? "A" : "B") +
            (queued >= 0 && !processor.isComparing() ? " / " + juce::String(queued + 1).paddedLeft('0', 2)
                                                     : "") +
            " next loop";
    playbackInfo.setText(playback, juce::dontSendNotification);
    playbackInfo.setColour(juce::Label::textColourId, queued >= 0 || pendingCompare ? gold : muted);
    for (int i = 0; i < 12; ++i)
        variants[i]->setPlaybackState(
            processor.isPlaying() && !hearingA && !processor.hasDraft() && i == audible, i == queued);
    advanceVisuals();
    if (busy || busy != shownBusy) {
        const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
        repaint(juce::Rectangle<float>(32, 674, 238, 12)
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
        if (!follow.getToggleState())
            processor.setPreview(!processor.previewRequested());
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
    const bool saving = action == 1 || action == 2 || action == 6;
    const bool bankFile = action == 2 || action == 3;
    const juce::String title = action == 4   ? "Import harmony reference MIDI"
                               : action == 5 ? "Import groove reference MIDI"
                               : action == 6 ? "Export selected time range"
                               : saving      ? (bankFile ? "Save phrase bank" : "Export MIDI")
                                             : (bankFile ? "Open phrase bank" : "Import MIDI");
    chooser =
        std::make_unique<juce::FileChooser>(title,
                                            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                .getChildFile(saving ? (bankFile      ? "My phrases.genetic"
                                                                        : action == 6 ? "Selected range.mid"
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
        if (action == 1 || action == 6)
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
                                    : p.exportMidiRange(file, rangeStart, rangeEnd);
        if (result.failed())
            p.setStatus(result.getErrorMessage());
        else if (action == 1 || action == 6)
            p.setStatus(action == 6 ? "Selected time range exported." : "MIDI exported.");
        else if (action == 2 || action == 3)
            p.setStatus(action == 2 ? "Phrase bank saved." : "Phrase bank restored.");
        safe->refresh();
    });
}
void Editor::startMidiDrag() {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "Genetic-Variation-" + juce::String(processor.selectedIndex() + 1), ".mid", false);
    auto result = processor.exportMidi(file);
    if (result.failed()) {
        processor.setStatus(result.getErrorMessage());
        return;
    }
    dragFiles.push_back(file);
    if (!juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, &drag))
        processor.setStatus("Drag could not start. Use Export to save this MIDI file.");
}
