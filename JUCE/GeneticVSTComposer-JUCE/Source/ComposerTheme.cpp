#include "ComposerTheme.h"
#include <cmath>

namespace {
using namespace palette;

juce::Font controlFont(float size, bool bold = false) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

float interaction(const juce::Component& c, const char* property, bool fallback) {
    return juce::jlimit(
        0.0f, 1.0f, static_cast<float>(c.getProperties().getWithDefault(property, fallback ? 1.0f : 0.0f)));
}

juce::Colour controlAccent(const juce::Component& c, juce::Colour fallback) {
    if (c.getProperties().contains("accentColour"))
        return juce::Colour(static_cast<juce::uint32>(static_cast<int>(c.getProperties()["accentColour"])));
    return fallback;
}

void arc(juce::Graphics& g, float cx, float cy, float radius, float from, float to, juce::Colour colour,
         float width) {
    if (to <= from || radius <= 0)
        return;
    juce::Path p;
    p.addCentredArc(cx, cy, radius, radius, 0, from, to, true);
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

ComposerLookAndFeel::ComposerLookAndFeel() {
    using namespace palette;
    setColour(juce::TextButton::buttonColourId, surface);
    setColour(juce::TextButton::buttonOnColourId, teal);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, background);
    setColour(juce::ComboBox::backgroundColourId, surface);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::ComboBox::focusedOutlineColourId, teal);
    setColour(juce::ComboBox::arrowColourId, muted);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, muted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, surface);
    setColour(juce::PopupMenu::highlightedTextColourId, accent);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, teal.withAlpha(0.25f));
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, line);
    setColour(juce::Slider::thumbColourId, teal);
    setColour(juce::ToggleButton::textColourId, muted);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ToggleButton::tickDisabledColourId, line);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TooltipWindow::backgroundColourId, surface);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, line);
    setColour(juce::TextEditor::backgroundColourId, background.brighter(0.025f));
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::highlightColourId, teal.withAlpha(0.25f));
    setColour(juce::TextEditor::highlightedTextColourId, text);
    setColour(juce::TextEditor::outlineColourId, line);
    setColour(juce::TextEditor::focusedOutlineColourId, teal);
    setColour(juce::CaretComponent::caretColourId, accent);
    setColour(juce::ScrollBar::thumbColourId, muted.withAlpha(0.45f));
    setColour(juce::ScrollBar::trackColourId, background);
}

void ComposerLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                                               bool over, bool down) {
    using namespace palette;
    const float hover = interaction(b, "hoverAmount", over);
    const float press = interaction(b, "pressAmount", down);
    const float enabled = b.isEnabled() ? 1.0f : 0.44f;
    const bool segment = static_cast<bool>(b.getProperties().getWithDefault("segment", false));
    const bool filled = colour.getPerceivedBrightness() > 0.52f;
    const auto tint = controlAccent(b, teal);
    const auto r = b.getLocalBounds().toFloat().reduced(0.7f);
    const float radius = std::min(segment ? 6.0f : 7.5f, r.getHeight() * 0.28f);
    auto top = colour.interpolatedWith(juce::Colours::white,
                                       filled ? 0.04f + hover * 0.1f : 0.025f + hover * 0.055f);
    auto bottom = colour.interpolatedWith(background, filled ? 0.04f : 0.2f);
    top = top.interpolatedWith(background, press * 0.1f);
    bottom = bottom.interpolatedWith(background, press * 0.12f);
    if (!b.isEnabled()) {
        top = top.interpolatedWith(panel, 0.5f);
        bottom = bottom.interpolatedWith(panel, 0.5f);
    }

    g.setGradientFill(juce::ColourGradient(top.withMultipliedAlpha(enabled), r.getCentreX(), r.getY(),
                                           bottom.withMultipliedAlpha(enabled), r.getCentreX(), r.getBottom(),
                                           false));
    g.fillRoundedRectangle(r, radius);
    const auto border = filled ? colour.brighter(0.12f) : line.interpolatedWith(tint, hover * 0.5f);
    g.setColour(border.withMultipliedAlpha(enabled));
    g.drawRoundedRectangle(r, radius, 1.0f);

    // A single inset highlight gives the control an edge without a glossy bevel.
    g.setColour(juce::Colours::white.withAlpha((filled ? 0.14f : 0.055f) * enabled * (1.0f - press)));
    g.drawHorizontalLine(static_cast<int>(r.getY() + 1.0f), r.getX() + radius, r.getRight() - radius);
    if (b.hasKeyboardFocus(true)) {
        g.setColour((filled ? background : tint).withAlpha(0.9f));
        g.drawRoundedRectangle(r.reduced(2.0f), std::max(2.0f, radius - 2.0f), 1.0f);
    }
}

void ComposerLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) {
    const bool small = b.getHeight() < 25;
    g.setFont(controlFont(small ? 11.5f : 12.5f, true));
    // Honour editor-provided colours for primary actions, pins and A/B states.
    auto colour = b.findColour(juce::TextButton::textColourOffId);
    if (b.getToggleState() && !b.isColourSpecified(juce::TextButton::textColourOffId))
        colour = b.findColour(juce::TextButton::textColourOnId);
    g.setColour(colour.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.46f));
    auto bounds = b.getLocalBounds().reduced(b.getWidth() > 30 ? 6 : 2, 1);
    if (down)
        bounds.translate(0, 1);
    g.drawFittedText(b.getButtonText(), bounds, juce::Justification::centred, 1, 0.8f);
}

void ComposerLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) {
    using namespace palette;
    const bool on = b.getToggleState();
    const float hover = interaction(b, "hoverAmount", over);
    const float enabled = b.isEnabled() ? 1.0f : 0.44f;
    const auto tint = controlAccent(b, accent);
    const float trackHeight = std::min(14.0f, b.getHeight() - 6.0f);
    const juce::Rectangle<float> track{1.0f, (b.getHeight() - trackHeight) * 0.5f, 25.0f, trackHeight};
    const auto trackColour = on ? tint.withAlpha(0.2f + hover * 0.1f) : surface.brighter(hover * 0.08f);
    g.setColour(trackColour.withMultipliedAlpha(enabled));
    g.fillRoundedRectangle(track, trackHeight * 0.5f);
    g.setColour((on ? tint.withAlpha(0.65f) : line.interpolatedWith(muted, hover * 0.45f))
                    .withMultipliedAlpha(enabled));
    g.drawRoundedRectangle(track, trackHeight * 0.5f, 1.0f);
    const float diameter = std::max(3.0f, trackHeight - 5.0f);
    const float thumbX = on ? track.getRight() - diameter - 2.5f : track.getX() + 2.5f;
    g.setColour((on ? tint : muted).withMultipliedAlpha(enabled * (down ? 0.8f : 1.0f)));
    g.fillEllipse(thumbX, track.getCentreY() - diameter * 0.5f, diameter, diameter);
    auto textColour = b.findColour(juce::ToggleButton::textColourId);
    if (on || over)
        textColour = textColour.interpolatedWith(text, on ? 0.8f : 0.4f);
    g.setColour(textColour.withMultipliedAlpha(enabled));
    g.setFont(controlFont(11.5f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft(33).reduced(0, 1),
                     juce::Justification::centredLeft, 1, 0.8f);
    if (b.hasKeyboardFocus(true)) {
        g.setColour(tint.withAlpha(0.8f));
        g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    }
}

void ComposerLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float value,
                                           float start, float end, juce::Slider& slider) {
    using namespace palette;
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h));
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float radius = std::max(3.0f, std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f - 6.0f);
    const float hover = interaction(slider, "hoverAmount", slider.isMouseOverOrDragging());
    const float enabled = slider.isEnabled() ? 1.0f : 0.4f;
    const auto tint = controlAccent(slider, slider.findColour(juce::Slider::rotarySliderFillColourId));
    const float displayValue = juce::jlimit(
        0.0f, 1.0f, static_cast<float>(slider.getProperties().getWithDefault("displayValue", value)));
    const float angle = start + displayValue * (end - start);

    for (int i = 0; i <= 10; ++i) {
        const float a = start + (end - start) * i / 10.0f;
        const bool active = i / 10.0f <= displayValue;
        g.setColour((active ? tint.withAlpha(0.55f) : muted.withAlpha(0.23f)).withMultipliedAlpha(enabled));
        g.drawLine(cx + std::sin(a) * (radius + 4.0f), cy - std::cos(a) * (radius + 4.0f),
                   cx + std::sin(a) * (radius + 5.5f), cy - std::cos(a) * (radius + 5.5f), 1.0f);
    }
    arc(g, cx, cy, radius, start, end, line.withMultipliedAlpha(enabled), 2.8f);
    if (hover > 0.01f)
        arc(g, cx, cy, radius, start, angle, tint.withAlpha(0.10f * hover * enabled), 7.0f);
    arc(g, cx, cy, radius, start, angle, tint.withMultipliedAlpha(enabled), 2.8f);

    const float capRadius = std::max(2.0f, radius - 5.0f);
    const juce::Rectangle<float> cap{cx - capRadius, cy - capRadius, capRadius * 2, capRadius * 2};
    g.setGradientFill(juce::ColourGradient(
        surface.brighter(0.09f + hover * 0.06f).withMultipliedAlpha(enabled), cx, cap.getY(),
        background.brighter(0.04f).withMultipliedAlpha(enabled), cx, cap.getBottom(), false));
    g.fillEllipse(cap);
    g.setColour(line.brighter(hover * 0.1f).withMultipliedAlpha(enabled));
    g.drawEllipse(cap.reduced(0.5f), 1.0f);
    arc(g, cx, cy, capRadius - 1, -0.7f, 0.7f, text.withAlpha(0.06f * enabled), 1.0f);

    const float inner = capRadius * 0.35f, outer = capRadius * 0.78f;
    g.setColour(text.withMultipliedAlpha(enabled));
    g.drawLine(cx + std::sin(angle) * inner, cy - std::cos(angle) * inner, cx + std::sin(angle) * outer,
               cy - std::cos(angle) * outer, 2.0f);
    g.setColour(tint.withMultipliedAlpha(enabled));
    g.fillEllipse(cx + std::sin(angle) * radius - 1.9f, cy - std::cos(angle) * radius - 1.9f, 3.8f, 3.8f);
    if (slider.hasKeyboardFocus(true)) {
        g.setColour(tint.withAlpha(0.75f));
        g.drawEllipse(cap.expanded(1.5f), 1.0f);
    }
}

void ComposerLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float minPos, float maxPos, juce::Slider::SliderStyle style,
                                           juce::Slider& slider) {
    // Range sliders are not used in this editor; retain JUCE's complete behaviour
    // if one is introduced rather than drawing a misleading single handle.
    if (style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical ||
        style == juce::Slider::ThreeValueHorizontal || style == juce::Slider::ThreeValueVertical) {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, slider);
        return;
    }
    using namespace palette;
    const bool vertical = slider.isVertical();
    const float hover = interaction(slider, "hoverAmount", slider.isMouseOverOrDragging());
    const float enabled = slider.isEnabled() ? 1.0f : 0.4f;
    const auto tint = controlAccent(slider, slider.findColour(juce::Slider::thumbColourId));
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const auto track = vertical ? juce::Rectangle<float>(cx - 2, float(y), 4, float(h))
                                : juce::Rectangle<float>(float(x), cy - 2, float(w), 4);
    g.setColour(background.withMultipliedAlpha(enabled));
    g.fillRoundedRectangle(track.expanded(0.5f), 2.5f);
    g.setColour(line.withMultipliedAlpha(enabled));
    g.fillRoundedRectangle(track, 2);
    const auto fill = vertical ? track.withTop(juce::jlimit(float(y), float(y + h), pos))
                               : track.withRight(juce::jlimit(float(x), float(x + w), pos));
    g.setColour(tint.withMultipliedAlpha(enabled * 0.85f));
    g.fillRoundedRectangle(fill, 2);
    const float thumbX = vertical ? cx : pos, thumbY = vertical ? pos : cy;
    if (hover > 0.01f) {
        g.setColour(tint.withAlpha(0.11f * hover * enabled));
        g.fillEllipse(thumbX - 9, thumbY - 9, 18, 18);
    }
    const juce::Rectangle<float> thumb = vertical
                                             ? juce::Rectangle<float>(thumbX - 5.5f, thumbY - 3.0f, 11, 6)
                                             : juce::Rectangle<float>(thumbX - 3.0f, thumbY - 5.5f, 6, 11);
    g.setColour(background.withMultipliedAlpha(enabled));
    g.fillRoundedRectangle(thumb.expanded(1), 2.5f);
    g.setColour(text.interpolatedWith(tint, hover * 0.4f).withMultipliedAlpha(enabled));
    g.fillRoundedRectangle(thumb, 2.0f);
    if (slider.hasKeyboardFocus(true)) {
        g.setColour(tint.withAlpha(0.8f));
        g.drawRoundedRectangle(thumb.expanded(2), 3, 1);
    }
}

void ComposerLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int,
                                       juce::ComboBox& box) {
    using namespace palette;
    const float hover = interaction(box, "hoverAmount", box.isMouseOverOrDragging());
    const bool focus = box.hasKeyboardFocus(true) || box.isPopupActive();
    const float enabled = box.isEnabled() ? 1.0f : 0.44f;
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, w - 1.0f, h - 1.0f);
    const auto base = box.findColour(juce::ComboBox::backgroundColourId);
    g.setGradientFill(juce::ColourGradient(
        base.interpolatedWith(text, 0.01f + hover * 0.025f).withMultipliedAlpha(enabled), 0, 0,
        base.interpolatedWith(background, 0.2f + (down ? 0.1f : 0.0f)).withMultipliedAlpha(enabled), 0,
        float(h), false));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour((focus ? teal.withAlpha(0.85f) : line.interpolatedWith(muted, hover * 0.35f))
                    .withMultipliedAlpha(enabled));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    g.setColour(line.withAlpha(0.6f * enabled));
    g.drawVerticalLine(w - 27, h * 0.29f, h * 0.71f);
    g.setColour((focus ? teal : muted).withMultipliedAlpha(enabled));
    juce::Path arrow;
    const float arrowY = h * 0.5f + (box.isPopupActive() ? 1.5f : -1.5f);
    const float direction = box.isPopupActive() ? -1.0f : 1.0f;
    arrow.startNewSubPath(w - 19.0f, arrowY);
    arrow.lineTo(w - 15.0f, arrowY + 3.0f * direction);
    arrow.lineTo(w - 11.0f, arrowY);
    g.strokePath(arrow,
                 juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font ComposerLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return controlFont(12.5f);
}
