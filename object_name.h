#ifndef OBJECT_NAME_H
#define OBJECT_NAME_H

// object_name.h - one place that turns a user-typed object name into the
// canonical catalog spelling. Shared by planetarium (--select) and
// autoobservatory (--object / --from-planetarium), and mirrored by
// pas/astrop.pas's ResolveName so all three resolve names identically.
//
// Matching is case-insensitive: exact name first, then a unique prefix
// ("arctur" -> "Arcturus"), then a unique substring. A fuzzy query matching
// several objects resolves to nothing and reports the candidates, because
// silently aiming a mount at the wrong object is worse than failing.

#include <string>
#include <vector>
#include <cctype>
#include <iostream>
#include "astro_math.h"
#include "celestial_objects.h"
#include "celestial_object_reader.h"
#include "planetary_objects.h"

inline std::string ObjectNameLower(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

inline bool CaseInsensitiveEq(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// Every selectable name: the star catalog plus the planet names. Planet names
// don't depend on the epoch, so any valid JD works (the Moon is always appended
// by the model).
inline std::vector<std::string> LoadCatalogNames()
{
    std::vector<std::string> names;
    std::vector<CelestialObject> stars;
    try { stars = CelestialObjectReader::ReadFromCSV("celestial_objects.csv"); } catch (...) {}
    for (auto& s : stars) names.push_back(s.name);
    // The Sun is not a catalog row: its position comes from the solar model, so
    // it is added to the selectable-name list by hand.
    names.push_back("Sun");
    double jd = JulianDate(2026, 1, 1, 0, 0, 0, 0.0);
    for (auto& p : GetPlanetaryObjects(jd)) names.push_back(p.name);
    return names;
}

// Does `name` denote exactly Messier object `token` (already lowercased)?
//
// The catalog carries two spellings, both of which must resolve:
//   "Lagoon Nebula (M8)"   tag at the end   - 9 existing rows
//   "M11 (Wild Duck Cluster)"  number first - 50 existing rows
//
// "M8" must match neither "M80" nor "M81", hence the token-boundary test.
inline bool IsMessierObject(const std::string& name, const std::string& tokenLower)
{
    std::string ln = ObjectNameLower(name);

    // Form 1: "(M8)" as a complete tag. Plain find() -- rfind(...,0) would only
    // look at position 0. The closing paren in the pattern is what keeps
    // "(m80)" from matching "(m8)".
    size_t i = ln.find("(" + tokenLower + ")");
    if (i != std::string::npos)
    {
        size_t after = i + tokenLower.size() + 2;
        if (after == ln.size() || ln[after] == ' ') return true;
    }

    // Form 2: the name begins with "M8" followed by a non-alphanumeric, so
    // "M8" and "M8 (Crab Nebula)" match while "M80" and "M81" do not.
    if (ln.rfind(tokenLower, 0) != 0) return false;
    size_t after = tokenLower.size();
    return after == ln.size() || !std::isalnum((unsigned char)ln[after]);
}

inline bool LooksLikeMessier(const std::string& q)
{
    if (q.size() < 2 || q[0] != 'm') return false;
    for (size_t i = 1; i < q.size(); ++i)
        if (!std::isdigit((unsigned char)q[i])) return false;
    return true;
}

// Returns true with `canonical` set on a unique match (exact, unique prefix, or
// unique substring). On failure returns false: `candidates` is filled with the
// ambiguous matches, or empty when nothing matched at all.
inline bool ResolveObjectName(const std::vector<std::string>& names,
                              const std::string& query,
                              std::string& canonical,
                              std::vector<std::string>& candidates)
{
    std::string q = ObjectNameLower(query);
    for (auto& n : names)
        if (ObjectNameLower(n) == q) { canonical = n; return true; }

    // Messier numbers need their own pass BEFORE prefix matching. "M8" is a
    // prefix of "M80", "M81", ... so without this, `--select M8` matched the
    // single prefix candidate and silently resolved to the WRONG OBJECT -- it
    // aimed the mount at M80. That is the worst possible failure for a mount,
    // so an exact Messier designation outranks any prefix or substring hit.
    if (LooksLikeMessier(q))
    {
        candidates.clear();
        for (auto& n : names)
            if (IsMessierObject(n, q)) candidates.push_back(n);
        if (candidates.size() == 1) { canonical = candidates.front(); return true; }
        if (!candidates.empty()) return false; // ambiguous
    }

    for (int pass = 0; pass < 2; ++pass) // 0 = prefix, 1 = substring
    {
        candidates.clear();
        for (auto& n : names)
        {
            std::string ln = ObjectNameLower(n);
            bool hit = pass == 0 ? ln.rfind(q, 0) == 0 : ln.find(q) != std::string::npos;
            if (hit) candidates.push_back(n);
        }
        if (candidates.size() == 1) { canonical = candidates.front(); return true; }
        if (!candidates.empty()) return false; // ambiguous
    }
    return false;
}

// One shared "no unique match" report so every program prints the same text.
inline void ReportNameFailure(const std::string& query,
                              const std::vector<std::string>& candidates)
{
    if (!candidates.empty())
    {
        std::cerr << "Ambiguous object: \"" << query << "\" matches "
                  << candidates.size() << ":\n";
        for (auto& c : candidates) std::cerr << "  " << c << "\n";
        std::cerr << "Use the full name.\n";
    }
    else
    {
        std::cerr << "Unknown object: \"" << query << "\"\n"
                  << "Names come from celestial_objects.csv and planetary_objects.csv\n";
    }
}

#endif
