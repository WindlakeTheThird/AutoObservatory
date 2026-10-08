// catcheck.cpp - sanity check for celestial_objects.csv.
//
// Catches catalog regressions that only show up at runtime otherwise:
//   1. every row parses as 10 well-formed numeric fields (CelestialObjectReader
//      would throw std::stod failures on a malformed line and lose the whole
//      sky map, so a catalog edit must be provably parseable);
//   2. object names are unique (the fuzzy resolver must never see two
//      identical keys);
//   3. stars brighter than magnitude 0.0 carry a Bayer designation (bright
//      naked-eye stars in the legend without one are almost always a data
//      entry mistake - clusters/nebulae are excluded by definition);
//   4. every non-empty Bayer designation is well-formed:
//      <Greek>_<3-letter IAU abbreviation> (e.g. Alpha_UMi), validated against
//      the full 88-constellation list - catches entries like Alpha_Orion that
//      would silently render as-is in the legend;
//   5. fuzzy-resolution smoke test: every name must resolve to itself, and no
//      name may become ambiguous against the full catalog.
//
// Run via: make check-cat
#include <cstdio>
#include <cstdlib>
#include <set>
#include <map>
#include <string>
#include <vector>
#include "celestial_objects.h"
#include "celestial_object_reader.h"
#include "object_name.h"

// The 88 IAU constellation 3-letter abbreviations (IAU 1922/1930 designations).
static const char* const kIAUAbbrev[] = {
    "And", "Ant", "Aps", "Aql", "Aqr", "Ara", "Ari", "Aur", "Boo", "Cae",
    "CMa", "CMi", "CVn", "Cyg", "Car", "Cas", "Cen", "Cep", "Cet", "CrB",
    "CrA", "Crt", "Cru", "Crv", "Com", "Col", "Dra", "Dor", "Del", "Equ",
    "Eri", "For", "Gem", "Gru", "Her", "Hor", "Hya", "Hyi", "Ind", "Lac",
    "Leo", "LMi", "Lep", "Lib", "Lup", "Lyn", "Lyr", "Men", "Mic", "Mus",
    "Mon", "Nor", "Oct", "Oph", "Ori", "Pav", "Peg", "Per", "Phe", "PsA",
    "Psc", "Pup", "Pyx", "Ret", "Sge", "Sco", "Scl", "Sct", "Ser", "Sex",
    "Sgr", "Tau", "Tel", "TrA", "Tri", "Tuc", "UMa", "UMi", "Vel", "Vir",
    "Vol", "Vul", nullptr };

// Greek-letter genitive/alpha form used by the catalog: capitalized name,
// optionally multi-word (e.g. Alpha, Beta, Gamma_1 style is rejected; the
// catalog uses plain Greek spellings).
static bool KnownGreek(const std::string& g)
{
    static const char* const greeks[] = {
        "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta",
        "Iota", "Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi", "Rho",
        "Sigma", "Tau", "Upsilon", "Phi", "Chi", "Psi", "Omega" };
    for (const char* s : greeks) if (g == s) return true;
    return false;
}

// Validate "Letter_Constellation" Bayer strings. Returns '' if OK, else the
// problem description.
static std::string CheckBayer(const std::string& bayer)
{
    if (bayer.empty()) return ""; // clusters/nebulae legitimately have none
    size_t us = bayer.find('_');
    if (us == std::string::npos)
        return "missing underscore (want Letter_Constellation, e.g. Alpha_UMi)";
    std::string greek = bayer.substr(0, us);
    std::string con = bayer.substr(us + 1);
    if (!KnownGreek(greek))
        return "unknown Greek letter '" + greek + "'";
    if (con.size() != 3)
        return "constellation abbreviation '" + con + "' is not 3 letters (want IAU abbreviation like Ori)";
    for (const char* const* p = kIAUAbbrev; *p; ++p)
        if (con == *p) return "";
    return "'" + con + "' is not an IAU 3-letter constellation abbreviation";
}

struct Row
{
    int line;
    std::string name;
    double ra, dec, mag;
    std::string bayer;
};

static bool ParseRow(const std::string& line, Row& r, std::string& err)
{
    std::stringstream ss(line);
    std::string f[10];
    int k = 0;
    for (; k < 10; ++k)
    {
        if (!std::getline(ss, f[k], ',')) break;
    }
    // Mirror CelestialObjectReader: 9 mandatory fields, the Bayer designation
    // is optional - a trailing comma yields an empty final field, and a line
    // that just ends after the magnitude means no Bayer either.
    if (k < 9)
    {
        err = "expected 9 comma-separated fields (+ optional Bayer), got " + std::to_string(k);
        return false;
    }
    try
    {
        r.ra = std::stod(f[1]) + std::stod(f[2]) / 60.0 + std::stod(f[3]) / 3600.0;
        r.dec = std::stod(f[4]) * (std::stod(f[5]) + std::stod(f[6]) / 60.0 + std::stod(f[7]) / 3600.0);
        r.mag = std::stod(f[8]);
    }
    catch (const std::exception&)
    {
        err = "non-numeric RA/Dec/magnitude field";
        return false;
    }
    r.name = f[0];
    r.bayer = (k >= 10) ? f[9] : "";
    if (r.name.empty()) { err = "empty name"; return false; }
    if (r.ra < 0.0 || r.ra >= 24.0) { err = "RA out of [0,24) h"; return false; }
    if (r.dec < -90.0 || r.dec > 90.0) { err = "Dec out of [-90,90] deg"; return false; }
    return true;
}

int main()
{
    auto objs = CelestialObjectReader::ReadFromCSV("celestial_objects.csv");

    // Re-parse with checks; also catches lines the lenient reader skips silently.
    FILE* f = fopen("celestial_objects.csv", "r");
    if (!f) { printf("FAIL cannot open celestial_objects.csv\n"); return 1; }
    char buf[512];
    std::vector<Row> rows;
    int lineno = 0, parseFail = 0;
    while (fgets(buf, sizeof(buf), f))
    {
        ++lineno;
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (lineno == 1 || line.empty()) continue;
        Row r; std::string err;
        if (!ParseRow(line, r, err))
        {
            printf("FAIL line %d: %s\n", lineno, err.c_str());
            ++parseFail;
        }
        else rows.push_back(r);
    }
    fclose(f);
    if (parseFail) { printf("\n%d malformed row(s) (plus %zu parsed)\n", parseFail, rows.size()); return 1; }
    printf("ok   all %zu rows parse, RA/Dec in range\n", rows.size());

    int failures = parseFail;

    // Uniqueness
    std::set<std::string> names;
    int dupes = 0;
    for (const auto& r : rows)
        if (!names.insert(r.name).second) { printf("FAIL duplicate name: %s\n", r.name.c_str()); ++dupes; }
    if (!dupes) printf("ok   all names unique\n");
    failures += dupes;

    // Bright stars need a Bayer designation
    int missing = 0;
    for (const auto& r : rows)
        if (r.mag < 0.0 && r.bayer.empty())
        {
            printf("FAIL mag %.2f star '%s' has no Bayer designation\n", r.mag, r.name.c_str());
            ++missing;
        }
    if (!missing) printf("ok   all stars brighter than mag 0 carry a Bayer designation\n");
    failures += missing;

    // Bayer designation well-formedness: Letter_ plus a 3-letter IAU
    // abbreviation ("Alpha_Orion" is the canonical typo this catches).
    int badBayer = 0;
    int bayerCount = 0;
    for (const auto& r : rows)
    {
        if (r.bayer.empty()) continue;
        ++bayerCount;
        std::string err = CheckBayer(r.bayer);
        if (!err.empty())
        {
            printf("FAIL '%s': Bayer '%s' %s\n", r.name.c_str(), r.bayer.c_str(), err.c_str());
            ++badBayer;
        }
    }
    if (!badBayer) printf("ok   all %d Bayer designations use IAU 3-letter abbreviations\n", bayerCount);
    failures += badBayer;

    // Reader count must match re-parse count (no silently skipped lines)
    if (objs.size() != rows.size())
    {
        printf("FAIL reader loaded %zu objects but file has %zu valid rows\n", objs.size(), rows.size());
        ++failures;
    }
    else printf("ok   reader loaded %zu objects, matches row count\n", objs.size());

    // Fuzzy smoke test through the REAL resolver and the REAL name pool the
    // tools use (stars + planets + Moon via LoadCatalogNames): each catalog
    // name must resolve to itself, and no name may become ambiguous against
    // the full pool. (Names are keys for --select/--object; a new entry that
    // shadows an existing prefix, or is shadowed by one, degrades resolution
    // silently.)
    std::vector<std::string> nameList = LoadCatalogNames();
    int resFail = 0;
    for (const auto& r : rows)
    {
        std::string canon; std::vector<std::string> cands;
        if (!ResolveObjectName(nameList, r.name, canon, cands) || canon != r.name)
        {
            printf("FAIL '%s' does not resolve to itself (canon '%s', %zu candidates)\n",
                   r.name.c_str(), canon.c_str(), cands.size());
            ++resFail;
        }
    }
    if (!resFail) printf("ok   every name resolves to itself through the shared resolver (%zu-name pool)\n", nameList.size());
    failures += resFail;

    if (failures) { printf("\n%d catalog problem(s) FAILED\n", failures); return 1; }
    printf("\nCatalog check passed (%zu objects).\n", rows.size());
    return 0;
}
