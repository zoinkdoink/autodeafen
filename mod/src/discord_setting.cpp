// The "Discord connection" row in the mod's settings page: a contextual
// Connect / Log out button with a live, color-coded status line. Declared in
// mod.json as "type": "custom:discord-connect".
//
// Refreshes are event-driven: immediately after a button press, and via a
// main-thread ping from the RPC worker whenever the connection state changes
// (plus a per-frame call as belt and braces).

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include "discord_rpc.hpp"
#include "keysender.hpp"

using namespace geode::prelude;

namespace {

bool isAuthorized() {
    return autodeafen::rpc::state() == autodeafen::rpc::State::Connected
        || !Mod::get()->getSavedValue<std::string>("discord-access-token", "").empty();
}

} // namespace

class DiscordConnectSetting : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        auto res = std::make_shared<DiscordConnectSetting>();
        auto root = checkJson(json, "DiscordConnectSetting");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }

    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}

    SettingNodeV3* createNode(float width) override;
};

class DiscordConnectSettingNode : public SettingNodeV3 {
protected:
    ButtonSprite* m_buttonSprite = nullptr;
    CCMenuItemSpriteExtra* m_button = nullptr;
    CCLabelBMFont* m_status = nullptr;
    std::string m_lastStatus;
    std::string m_buttonLabel = "Connect";

    static DiscordConnectSettingNode*& active() {
        static DiscordConnectSettingNode* s_active = nullptr;
        return s_active;
    }

    bool init(std::shared_ptr<DiscordConnectSetting> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;

        m_buttonSprite = ButtonSprite::create("Connect", "goldFont.fnt", "GJ_button_01.png", .8f);
        m_buttonSprite->setScale(.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this, menu_selector(DiscordConnectSettingNode::onButton));

        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setAnchorPoint({1.f, .5f});

        this->getButtonMenu()->setContentWidth(190);
        this->getButtonMenu()->addChildAtPosition(m_button, Anchor::Right, ccp(-32, 0));
        this->getButtonMenu()->addChildAtPosition(m_status, Anchor::Right, ccp(-68, 0));
        this->getButtonMenu()->updateLayout();

        active() = this;
        this->scheduleUpdate();
        this->updateState(nullptr);
        this->refreshDisplay();
        return true;
    }

    void update(float) override {
        this->refreshDisplay();
    }

    void onButton(CCObject*) {
        if (isAuthorized()) autodeafen::rpc::logout();
        else autodeafen::rpc::connect();
        this->refreshDisplay();
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        this->refreshDisplay();
    }

    void onCommit() override {}
    void onResetToDefault() override {}

public:
    void refreshDisplay() {
        using autodeafen::rpc::State;
        auto state = autodeafen::rpc::state();
        bool authorized = isAuthorized();
        bool busy = state == State::Connecting || state == State::AwaitingConsent;

        // Contextual button: Connect until authorized, then Log out; grayed
        // while a connect/consent is in flight.
        char const* label = authorized ? "Log out" : "Connect";
        if (label != m_buttonLabel) {
            m_buttonLabel = label;
            m_buttonSprite->setString(label);
        }
        bool enable = this->getSetting()->shouldEnable() && !busy;
        m_button->setEnabled(enable);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setOpacity(enable ? 255 : 155);
        m_buttonSprite->setColor(enable ? ccWHITE : ccGRAY);

        std::string text;
        ccColor3B color;
        if (!authorized && state == State::Connected) {
            // logout clicked; worker hasn't torn the connection down yet
            text = "logging out...";
            color = ccc3(200, 200, 200);
        } else switch (state) {
            case State::Connected:
                text = autodeafen::rpc::status();  // "connected as <user>"
                color = ccc3(128, 255, 128);
                break;
            case State::AwaitingConsent:
                text = "click Authorize in Discord!";
                color = ccc3(255, 220, 100);
                break;
            case State::Connecting:
                text = autodeafen::rpc::status();
                color = ccc3(200, 200, 200);
                break;
            case State::NotConnected: {
                // distinguish "never authorized" from "authorized but Discord
                // isn't reachable right now"
                if (!authorized) {
                    text = "not authorized";
                    color = ccc3(255, 120, 120);
                } else {
                    auto name = Mod::get()->getSavedValue<std::string>("discord-username", "");
                    text = name.empty() ? "authorized, not connected"
                                        : "authorized as " + name + ", not connected";
                    color = ccc3(255, 200, 120);
                }
                break;
            }
        }
        if (text == m_lastStatus) return;
        m_lastStatus = text;
        m_status->setString(text.c_str());
        m_status->setColor(color);
        m_status->setScale(1.f);
        constexpr float MAX_WIDTH = 115.f;
        if (m_status->getContentWidth() > 0.f) {
            m_status->setScale(std::min(.45f, MAX_WIDTH / m_status->getContentWidth()));
        }
    }

    static DiscordConnectSettingNode* activeNode() {
        return active();
    }

    ~DiscordConnectSettingNode() override {
        if (active() == this) active() = nullptr;
    }

    static DiscordConnectSettingNode* create(
        std::shared_ptr<DiscordConnectSetting> setting, float width
    ) {
        auto ret = new DiscordConnectSettingNode();
        if (ret && ret->init(setting, width)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }

    std::shared_ptr<DiscordConnectSetting> getSetting() const {
        return std::static_pointer_cast<DiscordConnectSetting>(SettingNodeV3::getSetting());
    }
};

SettingNodeV3* DiscordConnectSetting::createNode(float width) {
    return DiscordConnectSettingNode::create(
        std::static_pointer_cast<DiscordConnectSetting>(shared_from_this()), width);
}

namespace {

void syncKeystrokeModeMirror() {
    // enable-if can only reference bool settings or saved values, so the
    // string delivery-mode is mirrored into a saved value for the
    // "saved:keystroke-mode" conditions in mod.json.
    Mod::get()->setSavedValue(
        "keystroke-mode",
        Mod::get()->getSettingValue<std::string>("delivery-mode") == "keystroke");
}

} // namespace

$execute {
    (void)Mod::get()->registerCustomSettingType(
        "discord-connect", &DiscordConnectSetting::parse);

    autodeafen::rpc::setStatusListener([] {
        if (auto* node = DiscordConnectSettingNode::activeNode()) {
            node->refreshDisplay();
        }
    });

    listenForSettingChanges<std::string>("delivery-mode", [](std::string mode) {
        syncKeystrokeModeMirror();
        // Kick off each mode's one-time setup right away, while the user is
        // looking at the settings screen: Discord consent popup, or the macOS
        // Accessibility prompt for keystroke mode.
        if (mode == "discord") autodeafen::rpc::connect();
        else autodeafen::prepareKeystrokeMode();
    });
}

$on_mod(Loaded) {
    syncKeystrokeModeMirror();
    if (Mod::get()->getSettingValue<std::string>("delivery-mode") == "discord") {
        // Silently resume a previous authorization at startup so the status
        // reads "connected" before the first deafen. Never triggers the OAuth
        // popup: only runs when a token is already stored.
        if (!Mod::get()->getSavedValue<std::string>("discord-access-token", "").empty()) {
            autodeafen::rpc::connect();
        }
    } else {
        // surface the macOS Accessibility prompt at launch, not mid-attempt
        autodeafen::prepareKeystrokeMode();
    }
}
