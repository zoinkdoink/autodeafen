#pragma once

// Per-attempt threshold resolution. Strictly opt-in: no config, disabled
// config, or no entry for the active startpos all mean "do nothing".

#include <map>
#include <optional>

namespace autodeafen {

struct LevelConfig {
    bool enabled = false;
    // startpos index -> absolute deafen percent. Index 0 is the level's actual
    // start; 1..N are StartPos objects ordered by x, matching the in-game
    // "StartPos n/N" switcher.
    std::map<int, int> sp;
};

inline std::optional<int> resolveThreshold(
    LevelConfig const* cfg, int spIndex, int spawnPercent
) {
    if (!cfg || !cfg->enabled) return std::nullopt;
    auto it = cfg->sp.find(spIndex);
    if (it == cfg->sp.end()) return std::nullopt;
    int threshold = it->second;
    if (threshold < 1 || threshold > 100) return std::nullopt;
    // Arming guard: a threshold at or below the spawn point would fire the
    // instant the attempt starts (e.g. an 80% startpos with a 50% threshold).
    if (threshold <= spawnPercent) return std::nullopt;
    return threshold;
}

} // namespace autodeafen
