// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_EVENTNAVIGATOR_H
#define SHIPDISP_EVENTNAVIGATOR_H

// =============================================================================
//  EventNavigator.h -- in-GUI event navigation without restarting the display.
//
//  Built on the mechanism validated by the prototype's MenuTest: a method
//  tagged "// *MENU*" on an REveElement-derived class with a ROOT dictionary
//  shows up in that element's right-click context menu in the Eve7 client, and
//  clicking it issues a MIR (Method Invocation Request) that calls back into
//  this C++ object.
//
//  The element is added to the Eve world, so it appears in the browser's tree
//  on the left. Right-click "EventNavigator" -> Next / Previous / GotoEvent.
//
//  Requires a ROOT dictionary: see EventNavigatorLinkDef.h and CMakeLists.txt.
//  The *MENU* tag is metadata read from dictionary info, so without the
//  dictionary the menu entries simply do not appear.
// =============================================================================

#include <ROOT/REveElement.hxx>

#include <functional>
#include <string>

namespace shipdisp {

class EventNavigator : public ROOT::Experimental::REveElement {
   public:
    // A default constructor is REQUIRED for ROOT dictionary generation.
    EventNavigator() = default;
    explicit EventNavigator(const std::string& name) : REveElement(name) {}

    // --- context-menu entries (the *MENU* tag is what exposes them) ---------
    void NextEvent();           // *MENU*
    void PreviousEvent();       // *MENU*
    void GotoEvent(int index);  // *MENU*
    void PrintStatus();         // *MENU*

    /// Wire the navigator to the display. Not a menu entry.
    /// @param go        called with the event index to display
    /// @param nEvents   number of events in the file
    /// @param current   index currently shown
    void Configure(std::function<void(long long)> go, long long nEvents, long long current);

   private:
    void Show(long long index);

    // All transient (//!): never streamed, so the dictionary need not know how
    // to serialise a std::function.
    std::function<void(long long)> fGo;  //!
    long long fNum = 0;                  //!
    long long fCurrent = 0;              //!

    ClassDef(EventNavigator, 1);  // ROOT dictionary macro
};

}  // namespace shipdisp

#endif  // SHIPDISP_EVENTNAVIGATOR_H
