// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "EventNavigator.h"

#include <algorithm>
#include <cstdint>
#include <iostream>

ClassImp(shipdisp::EventNavigator);

namespace shipdisp {

void EventNavigator::Configure(std::function<void(std::int64_t)> go, std::int64_t nEvents,
                               std::int64_t current) {
    fGo = std::move(go);
    fNum = nEvents;
    fCurrent = current;
    SetName("EventNavigator  [event " + std::to_string(fCurrent) + " / " +
            std::to_string(fNum ? fNum - 1 : 0) + "]");
}

void EventNavigator::Show(std::int64_t index) {
    if (fNum <= 0) {
        std::cout << "[EventNavigator] no events available\n";
        return;
    }
    // Clamp rather than wrap: less surprising when clicking repeatedly.
    const std::int64_t want = std::clamp<std::int64_t>(index, 0, fNum - 1);
    if (want != index) {
        std::cout << "[EventNavigator] event " << index << " out of range [0, " << fNum - 1
                  << "] -- showing " << want << "\n";
    }
    fCurrent = want;
    if (fGo) fGo(fCurrent);

    // Reflect the current event in the element's name, so the browser tree
    // doubles as the event counter.
    SetName("EventNavigator  [event " + std::to_string(fCurrent) + " / " +
            std::to_string(fNum - 1) + "]");
    StampObjProps();
}

void EventNavigator::NextEvent() { Show(fCurrent + 1); }
void EventNavigator::PreviousEvent() { Show(fCurrent - 1); }
void EventNavigator::GotoEvent(int index) { Show(static_cast<std::int64_t>(index)); }

void EventNavigator::PrintStatus() {
    std::cout << "[EventNavigator] event " << fCurrent << " of " << fNum << "\n";
}

}  // namespace shipdisp
