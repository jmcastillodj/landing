// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

#pragma once

#include <atomic>

//==============================================================================
// Raises an atomic to a value if the value is higher, without a lock. The audio
// thread uses it to publish peaks, which the GUI thread reads and resets with
// exchange(), so a peak is never lost between the two.
inline void storeMax(std::atomic<float>& target, float value)
{
    float current = target.load(std::memory_order_relaxed);
    while (value > current && ! target.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}
