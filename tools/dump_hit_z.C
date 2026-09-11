// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  dump_hit_z.C  --  independent cross-check of hit positions.
//
//  Reads the "events" RNTuple directly and prints, for one event, the x/y/z of
//  every SimHit plus x/y/z ranges. Shares NO code with the display, so it is a
//  clean ground-truth reference: if these disagree with the screen, the display
//  is at fault; if they already look wrong here, the data/units/frame are.
//
//  Confirmed schema (from PrintInfo):
//    sim_hits (std::vector<SHiP::SimHit>)
//      _0.position (std::array<double,3>)  ->  sub-fields _0,_1,_2
//
//  Strategy: read the per-event count and the flattened component columns of
//  the collection. In RNTuple a collection member "sim_hits._0.position._0" is
//  stored as one flat column across all events; the collection field
//  "sim_hits" carries the per-event offsets. We use the offsets from the
//  cardinality view to slice the flat component columns for the chosen event.
//
//  Usage (no build, no dictionary):
//    root -l -b -q 'tools/dump_hit_z.C("files/llp_display.root", 16)'
// =============================================================================

#include <TSystem.h>
#include <ROOT/RNTupleReader.hxx>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Read one flat component column ("sim_hits._0.position._N") fully, plus the
// collection offsets, and return the values belonging to `event`.
std::vector<double> componentForEvent(ROOT::RNTupleReader& reader, const std::string& col,
                                      const std::vector<std::size_t>& counts,
                                      unsigned long long event) {
    std::vector<double> out;
    // Flat columns are indexed by a global element number; sum counts before
    // `event` to get the start offset.
    std::size_t start = 0;
    for (unsigned long long e = 0; e < event; ++e) start += counts[e];
    const std::size_t n = counts[event];

    auto view = reader.GetView<double>(col);
    for (std::size_t i = 0; i < n; ++i) out.push_back(view(start + i));
    return out;
}

}  // namespace

void dump_hit_z(const char* file, unsigned long long event = 0, const char* ntuple = "events") {
    if (gSystem->AccessPathName(file)) {
        std::cerr << "dump_hit_z: cannot find file '" << file << "'\n";
        return;
    }
    std::unique_ptr<ROOT::RNTupleReader> reader;
    try {
        reader = ROOT::RNTupleReader::Open(ntuple, file);
    } catch (const std::exception& e) {
        std::cerr << "dump_hit_z: cannot open '" << ntuple << "' in '" << file << "': " << e.what()
                  << "\n";
        return;
    }

    const auto nEntries = reader->GetNEntries();
    std::cout << "dump_hit_z: '" << file << "' ntuple '" << ntuple << "' has " << nEntries
              << " events\n";
    if (event >= nEntries) {
        std::cerr << "dump_hit_z: event " << event << " out of range [0, " << nEntries - 1 << "]\n";
        return;
    }

    // Per-event hit multiplicity via the collection cardinality view. This is
    // the number of SimHits in each event.
    std::vector<std::size_t> counts(nEntries, 0);
    try {
        auto card = reader->GetView<ROOT::RNTupleCardinality<std::uint64_t>>("sim_hits");
        for (unsigned long long e = 0; e < nEntries; ++e) counts[e] = card(e);
    } catch (const std::exception& e) {
        std::cerr << "dump_hit_z: cannot read 'sim_hits' cardinality: " << e.what() << "\n";
        return;
    }

    if (counts[event] == 0) {
        std::cout << "dump_hit_z: event " << event << " has 0 hits.\n";
        return;
    }

    // The three position components, sliced to this event.
    std::vector<double> X, Y, Z;
    try {
        X = componentForEvent(*reader, "sim_hits._0.position._0", counts, event);
        Y = componentForEvent(*reader, "sim_hits._0.position._1", counts, event);
        Z = componentForEvent(*reader, "sim_hits._0.position._2", counts, event);
    } catch (const std::exception& e) {
        std::cerr << "dump_hit_z: cannot read position components: " << e.what() << "\n"
                  << "           (the flat-column path may differ; check PrintInfo)\n";
        return;
    }

    const std::size_t n = std::min({X.size(), Y.size(), Z.size()});
    std::cout << "dump_hit_z: event " << event << " has " << n << " hits, positions in mm:\n\n";
    std::printf("%6s %14s %14s %14s\n", "hit", "x", "y", "z");

    double zmin = 1e30, zmax = -1e30, zsum = 0;
    double xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30;
    for (std::size_t i = 0; i < n; ++i) {
        std::printf("%6zu %14.2f %14.2f %14.2f\n", i, X[i], Y[i], Z[i]);
        zmin = std::min(zmin, Z[i]);
        zmax = std::max(zmax, Z[i]);
        zsum += Z[i];
        xmin = std::min(xmin, X[i]);
        xmax = std::max(xmax, X[i]);
        ymin = std::min(ymin, Y[i]);
        ymax = std::max(ymax, Y[i]);
    }

    std::cout << "\nsummary (mm):\n";
    std::printf("  x range [%.1f, %.1f]\n", xmin, xmax);
    std::printf("  y range [%.1f, %.1f]\n", ymin, ymax);
    std::printf("  z range [%.1f, %.1f]  mean z %.1f\n", zmin, zmax, zsum / static_cast<double>(n));
    std::cout << "\ncompare z against the --inspect detector span and against the markers "
                 "on screen.\n";
}
