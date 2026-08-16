#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <vector>

namespace autodeafen {

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
