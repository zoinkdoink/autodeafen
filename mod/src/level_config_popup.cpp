#include "level_config_popup.hpp"

#include "config_store.hpp"
#include "startpos.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>

#include <charconv>

using namespace geode::prelude;

namespace autodeafen {

namespace {

constexpr float POPUP_WIDTH = 320.f;
constexpr float POPUP_HEIGHT = 240.f;
constexpr float LIST_WIDTH = 176.f;
constexpr float LIST_HEIGHT = 128.f;
constexpr float ROW_HEIGHT = 26.f;

std::optional<int> parsePercent(std::string const& text) {
    int value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) return std::nullopt;
    if (value < 1 || value > 100) return std::nullopt;
    return value;
}

} // namespace

LevelConfigPopup* LevelConfigPopup::create(PlayLayer* playLayer) {
    auto ret = new LevelConfigPopup();
    if (ret && ret->initWith(playLayer)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LevelConfigPopup::initWith(PlayLayer* playLayer) {
    if (!playLayer || !playLayer->m_level) return false;
    if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

    m_playLayer = playLayer;
    m_levelKey = levelKeyFor(playLayer->m_level);

    auto existing = ConfigStore::get().levelConfig(m_levelKey);
    // opening the popup on an unconfigured level implies intent to configure
    m_enabled = existing ? existing->enabled : true;
    m_use21 = existing && existing->use21;
    LevelConfig config = existing.value_or(LevelConfig{});

    this->setTitle(std::string(playLayer->m_level->m_levelName), "goldFont.fnt", .7f);

    // toggle row, centered under the title:
    // [x] Enable for this level    [ ] 2.1 %
    auto toggleMenu = CCMenu::create();
    toggleMenu->setContentSize({LIST_WIDTH, 24.f});
    toggleMenu->ignoreAnchorPointForPosition(false);
    auto toggler = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(LevelConfigPopup::onToggleEnabled), .6f);
    toggler->toggle(m_enabled);
    auto toggleLabel = CCLabelBMFont::create("Enable for this level", "bigFont.fnt");
    toggleLabel->setScale(.4f);
    toggleLabel->setAnchorPoint({0.f, .5f});
    auto toggler21 = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(LevelConfigPopup::onToggle21), .6f);
    toggler21->toggle(m_use21);
    auto label21 = CCLabelBMFont::create("2.1 %", "bigFont.fnt");
    label21->setScale(.4f);
    label21->setAnchorPoint({0.f, .5f});
    // manually centered
    float const toggleWidth = 16.f, gap = 8.f, groupGap = 24.f;
    float labelWidth = toggleLabel->getScaledContentSize().width;
    float label21Width = label21->getScaledContentSize().width;
    float total = toggleWidth + gap + labelWidth + groupGap
                + toggleWidth + gap + label21Width;
    float x = -total / 2;
    toggleMenu->addChildAtPosition(toggler, Anchor::Center, ccp(x + toggleWidth / 2, 0));
    x += toggleWidth + gap;
    toggleMenu->addChildAtPosition(toggleLabel, Anchor::Center, ccp(x, 0));
    x += labelWidth + groupGap;
    toggleMenu->addChildAtPosition(toggler21, Anchor::Center, ccp(x + toggleWidth / 2, 0));
    x += toggleWidth + gap;
    toggleMenu->addChildAtPosition(label21, Anchor::Center, ccp(x, 0));
    m_mainLayer->addChildAtPosition(toggleMenu, Anchor::Top, ccp(0, -42));

    // one row per spawn point
    auto startPositions = collectStartPositions(playLayer);
    int activeIndex = 0;
    if (playLayer->m_startPosObject) {
        auto it = std::find(startPositions.begin(), startPositions.end(),
                            playLayer->m_startPosObject);
        activeIndex = it == startPositions.end()
            ? -1 : int(it - startPositions.begin()) + 1;
    }
    // dark inset behind the list, GD-style
    auto listBg = CCScale9Sprite::create("square02b_001.png");
    listBg->setContentSize({LIST_WIDTH + 8.f, LIST_HEIGHT + 8.f});
    listBg->setColor(ccc3(0, 0, 0));
    listBg->setOpacity(90);
    m_mainLayer->addChildAtPosition(listBg, Anchor::Center, ccp(0, -16));

    auto scroll = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
    scroll->ignoreAnchorPointForPosition(false);
    scroll->setAnchorPoint({.5f, .5f});
    scroll->m_contentLayer->setLayout(
        ColumnLayout::create()
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::End)
            ->setAutoGrowAxis(LIST_HEIGHT)
            ->setGap(4.f));

    m_inputs.clear();
    m_rowLabels.clear();
    m_rowPositions.clear();
    for (int index = 0; index <= int(startPositions.size()); ++index) {
        auto row = CCNode::create();
        row->setContentSize({LIST_WIDTH - 10.f, ROW_HEIGHT});

        // the label is where this spawn point sits in the level; filled in by
        // refreshRowLabels() so the 2.1 toggle can recompute it live
        auto pos = index == 0 ? ccp(0, 0)
                              : startPositions[size_t(index - 1)]->getPosition();
        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setScale(.5f);
        label->setAnchorPoint({1.f, .5f});
        if (index == activeIndex) {
            label->setColor(ccc3(255, 220, 100));  // the one you're paused on
        }
        row->addChildAtPosition(label, Anchor::Center, ccp(-8, 0));
        m_rowLabels.push_back(label);
        m_rowPositions.push_back(pos);

        auto input = TextInput::create(52.f, "-");
        input->setCommonFilter(CommonFilter::Uint);
        input->setMaxCharCount(3);
        input->setScale(.75f);
        if (auto it = config.sp.find(index); it != config.sp.end()) {
            input->setString(std::to_string(it->second));
        }
        row->addChildAtPosition(input, Anchor::Center, ccp(30, 0));
        m_inputs.push_back(input);

        scroll->m_contentLayer->addChild(row);
    }
    scroll->m_contentLayer->updateLayout();
    scroll->scrollToTop();
    this->refreshRowLabels();

    m_mainLayer->addChildAtPosition(scroll, Anchor::Center, ccp(0, -16));

    // always-visible scrollbar whenever there's actually something to scroll
    if (scroll->m_contentLayer->getContentHeight() > LIST_HEIGHT) {
        auto scrollbar = Scrollbar::create(scroll);
        m_mainLayer->addChildAtPosition(
            scrollbar, Anchor::Center, ccp(LIST_WIDTH / 2 + 12.f, -16));
    }

    auto hint = CCLabelBMFont::create(
        "deafen percent per startpos; empty = don't deafen", "chatFont.fnt");
    hint->setScale(.45f);
    hint->setColor(ccc3(180, 180, 180));
    m_mainLayer->addChildAtPosition(hint, Anchor::Bottom, ccp(0, 12));

    // shortcut to the global mod settings
    auto gearSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
    gearSprite->setScale(.6f);
    auto gearButton = CCMenuItemSpriteExtra::create(
        gearSprite, this, menu_selector(LevelConfigPopup::onSettings));
    m_buttonMenu->addChildAtPosition(gearButton, Anchor::BottomRight, ccp(-22, 20));

    return true;
}

void LevelConfigPopup::onSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get());
}

void LevelConfigPopup::onToggleEnabled(CCObject* sender) {
    // CCMenuItemToggler reports its pre-toggle state inside the callback
    m_enabled = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
}

void LevelConfigPopup::onToggle21(CCObject* sender) {
    m_use21 = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    this->refreshRowLabels();
}

void LevelConfigPopup::refreshRowLabels() {
    for (size_t i = 0; i < m_rowLabels.size(); ++i) {
        int percent = i == 0
            ? 0 : percentForPos(m_playLayer, m_rowPositions[i], m_use21);
        m_rowLabels[i]->setString(fmt::format("{}%", percent).c_str());
    }
}

void LevelConfigPopup::saveConfig() {
    LevelConfig config;
    config.enabled = m_enabled;
    config.use21 = m_use21;
    for (size_t index = 0; index < m_inputs.size(); ++index) {
        if (auto percent = parsePercent(m_inputs[index]->getString())) {
            config.sp[int(index)] = *percent;
        }
    }
    auto& store = ConfigStore::get();
    if (!config.enabled && config.sp.empty()) {
        store.removeLevelConfig(m_levelKey);  // keep levels.json tidy
    } else {
        store.setLevelConfig(m_levelKey, std::move(config));
    }
    if (auto result = store.save(); !result.isOk()) {
        log::warn("AutoDeafen: failed to save level config: {}", result.unwrapErr());
    }
}

void LevelConfigPopup::onClose(CCObject* sender) {
    this->saveConfig();
    Popup::onClose(sender);
}

} // namespace autodeafen
