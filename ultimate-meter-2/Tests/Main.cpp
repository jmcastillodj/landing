// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

#include <JuceHeader.h>
#include "../Source/Presets.h"

//==============================================================================
// Runs every juce::UnitTest that the other files of this target register, and
// returns a failure to CTest if any of them fails.
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // The tests do not read the default preset of whoever runs them
    Presets::loadDefaultInNewInstances = false;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;

    return failures == 0 ? 0 : 1;
}
