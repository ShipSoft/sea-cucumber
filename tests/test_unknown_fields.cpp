// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  Regression test for issue #35: a file carrying a field whose type has no
//  dictionary in the reading process (aegir's `event_header` read by a build
//  against an older data model) must still display.
//
//  To get such a file, a forked child declares a class to the interpreter,
//  writes it alongside `sim_hits`, and exits; the parent never learns about the
//  class -- the same technique ROOT's own ntuple_emulated tests use.
// =============================================================================

#include <ROOT/RError.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleWriter.hxx>

#include <TInterpreter.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "RNTupleEventSource.h"
#include "SHiP/SimHit.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

// Child process: write one event with `sim_hits` plus a `future_header` field
// of a class only this process knows about.
[[noreturn]] void writeInChild(const std::string& path) {
    try {
        if (!gInterpreter->Declare(R"(
                namespace sctest {
                struct FutureHeader {
                    double weight = 1.0;
                    long long original_event_id = -1;
                    ClassDefNV(FutureHeader, 2);
                };
                }  // namespace sctest
            )"))
            std::exit(EXIT_FAILURE);

        auto model = ROOT::RNTupleModel::Create();
        model->AddField(ROOT::RFieldBase::Create("future_header", "sctest::FutureHeader").Unwrap());
        auto hits = model->MakeField<std::vector<SHiP::SimHit>>("sim_hits");
        {
            auto writer = ROOT::RNTupleWriter::Recreate(std::move(model), "events", path);
            SHiP::SimHit h;
            h.detectorId = 42;
            h.position = {1.0, 2.0, 3.0};
            hits->push_back(h);
            writer->Fill();
        }  // flush before exiting
    } catch (const std::exception& e) {
        std::cerr << "child: " << e.what() << "\n";
        std::exit(EXIT_FAILURE);
    }
    std::exit(EXIT_SUCCESS);
}
}  // namespace

int main() {
    const std::string path = "test_unknown_fields.root";

    const pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "FAIL: fork() failed\n";
        return 1;
    }
    if (pid == 0) writeInChild(path);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "FAIL: writer child did not exit successfully\n";
        return 1;
    }

    // Guard: the file really does reproduce the issue -- rebuilding the full
    // model (what GetModel()/LoadEntry() do) fails on the unknown type.
    {
        auto reader = ROOT::RNTupleReader::Open("events", path);
        bool threw = false;
        try {
            (void)reader->GetDescriptor().CreateModel();
        } catch (const ROOT::RException&) {
            threw = true;
        }
        check(threw, "full model reconstruction fails on the unknown type");
    }

    // The event source must still read the fields it knows about.
    shipdisp::RNTupleEventSource src(path, "events");
    check(src.numEvents() == 1, "numEvents == 1");
    check(src.loadEvent(0), "loadEvent(0)");
    check(src.hits().size() == 1, "event 0 has 1 hit");
    if (!src.hits().empty()) check(src.hits()[0].detectorId == 42, "hit detectorId == 42");
    check(src.mcParticles().empty(), "absent mc_particles reads as empty");

    std::remove(path.c_str());

    if (failures == 0) std::cout << "test_unknown_fields: OK\n";
    return failures == 0 ? 0 : 1;
}
