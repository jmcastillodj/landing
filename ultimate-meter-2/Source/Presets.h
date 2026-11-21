// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

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
            .getChildFile("Jm Castillo").getChildFile("ULTIMATE METER 2").getChildFile("Presets");
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
        static const juce::StringArray leftOut { "editorWidth", "editorHeight", "tonalCustomTargets", "referenceFiles", "stateVersion" };
        return leftOut.contains(property.toString());
    }

    // The monitor section changes what is heard, and what a bounce would hold. A preset is how the meter looks and
    // measures, so the monitor is never part of one: it would be put on without anyone asking for it.
    inline bool isMonitorParameter(const juce::ValueTree& child)
    {
        return child.getProperty("id").toString().startsWith("mon");
    }

    // The tests turn this off, so that they do not depend on the files of whoever runs them
    inline bool loadDefaultInNewInstances = true;

    inline bool save(const juce::ValueTree& state, const juce::File& file)
    {
        auto copy = state.createCopy();
        for (int i = copy.getNumProperties() - 1; i >= 0; --i)
            if (isLeftOut(copy.getPropertyName(i)))
                copy.removeProperty(copy.getPropertyName(i), nullptr);

        for (int i = copy.getNumChildren() - 1; i >= 0; --i)
            if (isMonitorParameter(copy.getChild(i)))
                copy.removeChild(i, nullptr);

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

        // The layout of multi mode is one thing in several properties. A preset that has it replaces all of it, so that
        // what an older preset does not hold is not mixed in from the session.
        bool hasLayout = false;
        for (int i = 0; i < preset.getNumProperties(); ++i)
            hasLayout = hasLayout || preset.getPropertyName(i).toString().startsWith("multi");

        if (hasLayout)
            for (int i = merged.getNumProperties() - 1; i >= 0; --i)
                if (merged.getPropertyName(i).toString().startsWith("multi") && !preset.hasProperty(merged.getPropertyName(i)))
                    merged.removeProperty(merged.getPropertyName(i), nullptr);

        for (auto child : preset)
        {
            if (isMonitorParameter(child))
                continue;

            auto existing = merged.getChildWithProperty("id", child.getProperty("id"));
            if (existing.isValid())
                merged.removeChild(existing, nullptr);
            merged.appendChild(child.createCopy(), nullptr);
        }

        apvts.replaceState(merged);
        return true;
    }
}
