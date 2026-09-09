// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  Tells rootcling which class to build a dictionary for. The dictionary is
//  what makes the "// *MENU*" tags on EventNavigator take effect: the tag is
//  metadata read from dictionary info by the Eve7 client.
// =============================================================================
#ifdef __CLING__

#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class shipdisp::EventNavigator+;

#endif
