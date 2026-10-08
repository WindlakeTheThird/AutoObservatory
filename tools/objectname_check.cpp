// objectname_check.cpp - unit self-check for object_name.h, the shared
// name-resolution module behind `planetarium --select` and
// `autoobservatory --object`.
//
// Pins the matching contract against a small synthetic catalog instead of the
// real CSVs, so the algorithm is tested in isolation and stays independent of
// catalog edits: exact (case-insensitive) match, then a unique prefix, then a
// unique substring, with an ambiguous query resolving to *nothing* and
// reporting its candidates. The real catalog is covered end-to-end by
// tools/namecheck.sh (`make check-select`).
//
// Build & run via:  make check-name
#include <cstdio>
#include <string>
#include <vector>
#include "object_name.h"

static const std::vector<std::string> kCatalog = {
    "Vega", "Arcturus", "Mars", "Markab", "Makemake", "Moon", "Venus"
};

struct Case {
    const char* query;
    const char* expect;     // expected canonical name, or nullptr when unresolved
    int expectCandidates;   // expected candidate count when expect == nullptr
};

static const Case kCases[] = {
    // query      expected      candidates (when unresolved)
    {"vega",     "Vega",       0},  // exact, different case
    {"VEGA",     "Vega",       0},
    {"Arcturus", "Arcturus",   0},
    {"arctur",   "Arcturus",   0},  // unique prefix
    {"ARCT",     "Arcturus",   0},  // prefix matching is case-insensitive
    {"mak",      "Makemake",   0},
    {"moon",     "Moon",       0},
    {"ctur",     "Arcturus",   0},  // unique substring
    {"ma",       nullptr,      3},  // ambiguous prefix: Markab, Mars, Makemake
    {"u",        nullptr,      2},  // ambiguous substring: Arcturus, Venus
    {"zzzzz",    nullptr,      0},  // nothing matches
};

int main()
{
    int failures = 0;

    for (const Case& c : kCases)
    {
        std::string canonical;
        std::vector<std::string> candidates;
        bool resolved = ResolveObjectName(kCatalog, c.query, canonical, candidates);

        bool ok = c.expect
            ? (resolved && canonical == c.expect)
            // On failure the caller must not be handed a stale canonical name.
            : (!resolved && canonical.empty() &&
               candidates.size() == (size_t)c.expectCandidates);

        if (!ok) ++failures;
        if (resolved)
            printf("%s %-8s -> %s\n", ok ? "ok  " : "FAIL", c.query, canonical.c_str());
        else
            printf("%s %-8s -> no match (%zu candidate%s)\n", ok ? "ok  " : "FAIL", c.query,
                   candidates.size(), candidates.size() == 1 ? "" : "s");
    }

    // An exact match must win even when the query is also a prefix that would
    // otherwise be ambiguous - a stale prefix tie must not override a real name.
    {
        std::vector<std::string> names = {"Mars", "Marsilea"};
        std::string canonical;
        std::vector<std::string> candidates;
        bool ok = ResolveObjectName(names, "mars", canonical, candidates) && canonical == "Mars";
        if (!ok) ++failures;
        printf("%s %-8s -> %s  (exact beats ambiguous prefix)\n",
               ok ? "ok  " : "FAIL", "mars", canonical.c_str());
    }

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nAll name-resolution checks passed.\n");
    return 0;
}
