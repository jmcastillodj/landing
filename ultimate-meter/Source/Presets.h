#pragma once

#include <JuceHeader.h>

//==============================================================================
// Presets are the settings of the plugin, with the layout of multi mode, kept as files in a folder of the user,
// so that they are there in every session and every host. One more file is the default, which a new instance of
// the plugin opens with.
namespace Presets
{
    inline juce::File folder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Jm Castillo").getChildFile("ULTIMATE METER").getChildFile("Presets");
    }

    inline juce::File defaultFile()   { return folder().getSiblingFile("default.xml"); }
    inline juce::File fileOf(const juce::String& name) { return folder().getChildFile(juce::File::createLegalFileName(name) + ".xml"); }

    inline juce::StringArray names()
    {
        juce::StringArray result;
        for (const auto& file : folder().findChildFiles(juce::File::findFiles, false, "*.xml"))
            result.add(file.getFileNameWithoutExtension());
        result.sort(true);
        return result;
    }

    // What belongs to the window or to the user's own files rather than to the look of the meter
    inline bool isLeftOut(const juce::Identifier& property)
    {
        static const juce::StringArray leftOut { "editorWidth", "editorHeight", "tonalCustomTargets", "stateVersion" };
        return leftOut.contains(property.toString());
    }

    inline bool save(const juce::ValueTree& state, const juce::File& file)
    {
        auto copy = state.createCopy();
        for (int i = copy.getNumProperties() - 1; i >= 0; --i)
            if (isLeftOut(copy.getPropertyName(i)))
                copy.removeProperty(copy.getPropertyName(i), nullptr);

        file.getParentDirectory().createDirectory();
        if (auto xml = copy.createXml())
            return xml->writeTo(file);
        return false;
    }

    // Puts the settings of a file over the current ones. What the file does not have, such as the window's size and
    // the targets made from files, stays as it is.
    inline bool load(juce::AudioProcessorValueTreeState& apvts, const juce::File& file)
    {
        auto xml = juce::parseXML(file);
        if (xml == nullptr)
            return false;

        const auto preset = juce::ValueTree::fromXml(*xml);
        if (!preset.isValid() || preset.getType() != apvts.state.getType())
            return false;

        auto merged = apvts.copyState();

        for (int i = 0; i < preset.getNumProperties(); ++i)
            if (!isLeftOut(preset.getPropertyName(i)))
                merged.setProperty(preset.getPropertyName(i), preset.getProperty(preset.getPropertyName(i)), nullptr);

        for (auto child : preset)
        {
            auto existing = merged.getChildWithProperty("id", child.getProperty("id"));
            if (existing.isValid())
                merged.removeChild(existing, nullptr);
            merged.appendChild(child.createCopy(), nullptr);
        }

        apvts.replaceState(merged);
        return true;
    }
}
