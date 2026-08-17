#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <vector>

namespace autodeafen {

// The percent GD would show at a position: 2.2's native time-based percent
// by default, or 2.1-style (x / m_levelLength, the formula GD itself falls
// back to on levels without a duration timestamp) when use21 is set.
inline int percentForPos(PlayLayer* layer, cocos2d::CCPoint pos, bool use21) {
    float length = std::max(1.f, layer->m_levelLength);
    float fraction;
    if (use21) {
        fraction = pos.x / length;
    } else {
        float total = layer->timeForPos({length, 0.f}, 0, 0, false, 0);
        if (total <= 0.f) return 0;
        fraction = layer->timeForPos(pos, 0, 0, false, 0) / total;
    }
    // GD truncates: you are "at N%" until you fully pass N+1
    return std::clamp(static_cast<int>(fraction * 100.f), 0, 100);
}

// StartPos objects (object ID 31) sorted by x. Index i in this vector is
// startpos index i+1 in the config model (0 = level start), matching the
// in-game "StartPos n/N" switcher numbering.
inline std::vector<StartPosObject*> collectStartPositions(GJBaseGameLayer* layer) {
    std::vector<StartPosObject*> out;
    if (!layer || !layer->m_objects) return out;
    for (auto obj : geode::cocos::CCArrayExt<GameObject*>(layer->m_objects)) {
        if (obj->m_objectID == 31) {
            out.push_back(static_cast<StartPosObject*>(obj));
        }
    }
    std::sort(out.begin(), out.end(), [](GameObject* a, GameObject* b) {
        return a->getPositionX() < b->getPositionX();
    });
    return out;
}

} // namespace autodeafen
