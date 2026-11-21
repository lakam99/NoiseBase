#pragma once
#include <juce_core/juce_core.h>
// Background cleanup owns everything it touches; no processor/editor pointer
// may be captured. The containing module stays mapped until process exit.
void deferNoiseBaseCleanup(std::function<void()>);
int pendingNoiseBaseCleanups();
