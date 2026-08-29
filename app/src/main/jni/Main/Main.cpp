//
// Created by reveny on 21/08/2023.
//

#include "../Include/KittyMemory/MemoryPatch.h"
#include "../Include/ImGui.h"
#include "../Include/RemapTools.h"
#include "../Include/Drawing.h"
#include "../Include/Unity.h"

// ==================== FEATURE STATES ====================

// Menu > Basic
static bool antiban       = true;
static bool protectPacket = false;
static bool antibounce    = false;
static bool growzV2       = false;
static bool moonwalk      = false;

// Menu > Visual
static bool visESP        = false;
static bool nameTag       = false;

// Menu > FindPath
static bool findpathEnabled = false;

// Menu > Fast
static bool fastAction    = false;

// Menu > ESP
static bool espPlayers    = false;
static bool espItems      = false;

// Menu > Extract
static bool extractEnabled = false;

// Automation > Spammer
static bool autoReenter   = false;
static bool autoReplay    = false;

// Automation > Autofarm
static bool autofarmEnabled = false;
static bool autoPlant       = false;
static bool autoHarvest     = false;
static bool autoDrop        = false;

// Automation > Automation
static bool autoPlantSeed   = false;
static bool autoTakeSeed    = false;

// Automation > Collect
static bool collectEnabled  = false;

// Automation > Fishing
static bool fishingEnabled  = false;

// Automation > AutoCrime
static bool autoCrimeEnabled = false;

// ==================== SELECTION STATE ====================
static int selectedTab     = 0;
static int selectedFeature = 0;

// ==================== HELPERS ====================

static void DrawToggle(const char* id, bool* val) {
    ImVec2 p   = ImGui::GetCursorScreenPos();
    float  h   = 24.0f;
    float  w   = h * 1.85f;
    float  r   = h * 0.5f;

    ImGui::InvisibleButton(id, ImVec2(w, h));
    if (ImGui::IsItemClicked()) *val = !*val;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = *val ? 1.0f : 0.0f;

    ImU32 bgCol   = *val ? IM_COL32(130, 55, 215, 255) : IM_COL32(55, 50, 70, 255);
    ImU32 knobCol = IM_COL32(255, 255, 255, 255);

    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bgCol, r);
    dl->AddCircleFilled(ImVec2(p.x + r + t * (w - r * 2.0f), p.y + r), r - 3.0f, knobCol);
}

static void ToggleRow(const char* title, const char* desc, bool* val, int uid) {
    ImGui::PushID(uid);

    float availW = ImGui::GetContentRegionAvail().x;
    float toggleW = 46.0f;
    float labelW  = availW - toggleW - 12.0f;

    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + labelW);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", title);
    if (desc && desc[0]) {
        ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "%s", desc);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::SameLine(availW - toggleW);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (desc && desc[0] ? 8.0f : 2.0f));
    DrawToggle("##t", val);

    ImGui::Spacing();
    ImGui::PopID();
}

static void SectionHeader(const char* icon, const char* title, const char* subtitle) {
    ImVec2 p  = ImGui::GetCursorScreenPos();
    float  iconSz = 38.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + iconSz, p.y + iconSz), IM_COL32(120, 50, 200, 255), 8.0f);
    ImVec2 textPos = ImVec2(p.x + iconSz * 0.5f - 5.0f, p.y + iconSz * 0.5f - 7.0f);
    dl->AddText(textPos, IM_COL32(255, 255, 255, 255), icon);

    ImGui::Dummy(ImVec2(iconSz, iconSz));
    ImGui::SameLine(iconSz + 10.0f);

    float prevY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(prevY - iconSz + 4.0f);
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", title);
    if (subtitle && subtitle[0]) {
        ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "%s", subtitle);
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

static bool FeatureItem(const char* icon, const char* title, const char* desc,
                        bool selected, int uid) {
    ImGui::PushID(uid);

    float availW = ImGui::GetContentRegionAvail().x;
    float h      = 58.0f;
    ImVec2 p     = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##fi", ImVec2(availW, h));
    bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg;
    if (selected) {
        bg = IM_COL32(120, 48, 200, 255);
    } else if (ImGui::IsItemHovered()) {
        bg = IM_COL32(45, 42, 60, 255);
    } else {
        bg = IM_COL32(0, 0, 0, 0);
    }
    dl->AddRectFilled(p, ImVec2(p.x + availW, p.y + h), bg, 8.0f);

    // Icon circle
    ImVec2 ctr = ImVec2(p.x + 28.0f, p.y + h * 0.5f);
    ImU32  icBg = selected ? IM_COL32(150, 70, 225, 255) : IM_COL32(55, 50, 72, 255);
    dl->AddCircleFilled(ctr, 16.0f, icBg);
    dl->AddText(ImVec2(ctr.x - 5.0f, ctr.y - 7.0f), IM_COL32(255, 255, 255, 255), icon);

    // Text
    dl->AddText(ImVec2(p.x + 52.0f, p.y + 11.0f), IM_COL32(255, 255, 255, 255), title);

    // Clip description to ~2 lines
    ImGui::PushFont(nullptr);
    float smallSize = ImGui::GetFontSize() * 0.85f;
    dl->AddText(nullptr, smallSize, ImVec2(p.x + 52.0f, p.y + 30.0f),
                IM_COL32(160, 155, 175, 255), desc, nullptr,
                availW - 58.0f);
    ImGui::PopFont();

    ImGui::PopID();
    return clicked;
}

static bool TabButton(const char* icon, const char* label, bool selected, int uid) {
    ImGui::PushID(uid + 9000);

    float sz = 52.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##tb", ImVec2(sz, sz));
    bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg;
    if (selected) {
        bg = IM_COL32(120, 48, 200, 255);
    } else if (ImGui::IsItemHovered()) {
        bg = IM_COL32(45, 42, 60, 255);
    } else {
        bg = IM_COL32(0, 0, 0, 0);
    }
    dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), bg, 10.0f);
    dl->AddText(ImVec2(p.x + sz * 0.5f - 5.0f, p.y + sz * 0.5f - 7.0f),
                IM_COL32(255, 255, 255, 255), icon);

    ImGui::PopID();
    return clicked;
}

// ==================== MENU STYLE ====================

void menuStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 14.0f;
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 6.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 6.0f);
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.ScrollbarSize     = 6.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.09f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.13f, 0.12f, 0.18f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.11f, 0.16f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.22f, 0.20f, 0.30f, 0.60f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.18f, 0.16f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.24f, 0.21f, 0.34f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.25f, 0.40f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.09f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.09f, 0.14f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.09f, 0.14f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.48f, 0.18f, 0.80f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.25f, 0.88f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.62f, 0.30f, 0.94f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.48f, 0.18f, 0.80f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.48f, 0.18f, 0.80f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.62f, 0.30f, 0.94f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.48f, 0.18f, 0.80f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.56f, 0.25f, 0.88f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.36f, 0.12f, 0.66f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.48f, 0.18f, 0.80f, 0.45f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.48f, 0.18f, 0.80f, 0.70f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.48f, 0.18f, 0.80f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.25f, 0.22f, 0.34f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.47f, 0.58f, 1.00f);
}

// ==================== DETAIL PANELS ====================

static void DrawBasicPanel() {
    SectionHeader("B", "Basic", "Basic cheats for growtopia.");
    ToggleRow("Antiban",
              "Basic antiban, this will prevent basic anticheat from growtopia. "
              "Other antiban will always be applied automatically in other features.",
              &antiban, 0);
    ToggleRow("Protect Packet",
              "Block all suspicious non standard packet to be send into server. "
              "Disable this when you are in Private Server",
              &protectPacket, 1);
    ToggleRow("Antibounce", "Prevent death from deadly tiles.", &antibounce, 2);
    ToggleRow("Growz V2",   "Give more speed to your player",  &growzV2,    3);
    ToggleRow("Moonwalk",   "Allows moving backward while facing forward.", &moonwalk, 4);
}

static void DrawVisualPanel() {
    SectionHeader("V", "Visual", "A cheat that only you can see");
    ToggleRow("ESP",       "Show player boxes on screen", &visESP,  10);
    ToggleRow("Name Tags", "Show player names above them", &nameTag, 11);
}

static void DrawFindPathPanel() {
    SectionHeader("F", "FindPath", "Teleport to target block");
    ToggleRow("Enable FindPath", "Teleport to target block position", &findpathEnabled, 20);
}

static void DrawFastPanel() {
    SectionHeader("+", "Fast", "Faster ability for action");
    ToggleRow("Fast Action", "Increase the speed of your actions", &fastAction, 30);
}

static void DrawESPPanel() {
    SectionHeader("E", "ESP", "Extra Sensory Perception");
    ToggleRow("Player ESP", "Show all players on the map", &espPlayers, 40);
    ToggleRow("Item ESP",   "Show all dropped items",      &espItems,   41);
}

static void DrawExtractPanel() {
    SectionHeader("X", "Extract", "Extract some floating items in tile.");
    ToggleRow("Enable Extract", "Automatically collect floating items in your tile", &extractEnabled, 50);
}

static void DrawInfoPanel() {
    SectionHeader("i", "Info", "Display raw information");
    ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "Raw game information will appear here.");
}

static void DrawSpammerPanel() {
    SectionHeader("S", "Spammer", "Automatically send chat without typing it manually.");

    float bw = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("Start", ImVec2(bw, 38.0f))) {}

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Spammer Setting");
    ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "Click to open settings");
    ImGui::Spacing();

    ToggleRow("Auto Re-enter",            "",                        &autoReenter, 60);
    ToggleRow("Auto replay after enter",  "Auto replay when enter",  &autoReplay,  61);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "Selected replay file");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "Select replay");
    ImGui::TextColored(ImVec4(0.50f, 0.47f, 0.58f, 1.0f), "List replay files to action when enter");
    ImGui::Spacing();

    if (ImGui::Button("Get all actions", ImVec2(ImGui::GetContentRegionAvail().x, 38.0f))) {}
}

static void DrawAutofarmPanel() {
    SectionHeader("F", "Autofarm", "Automatically farming without effort touching the screen");
    ToggleRow("Enable Autofarm", "Start automated farming", &autofarmEnabled, 70);
    ToggleRow("Auto Plant",      "Automatically plant seeds", &autoPlant,    71);
    ToggleRow("Auto Harvest",    "Automatically harvest crops", &autoHarvest, 72);
    ToggleRow("Auto Drop",       "Automatically drop items",  &autoDrop,     73);
}

static void DrawAutomationPanel() {
    SectionHeader("A", "Automation", "Auto plant and harvest with auto drop and auto take seed features.");
    ToggleRow("Auto Plant Seed", "Automatically plant seeds", &autoPlantSeed, 80);
    ToggleRow("Auto Take Seed",  "Automatically collect seeds", &autoTakeSeed, 81);
}

static void DrawCollectPanel() {
    SectionHeader("C", "Collect", "Automation about collect utils");
    ToggleRow("Enable Collect", "Automatically collect nearby items", &collectEnabled, 90);
}

static void DrawFishingPanel() {
    SectionHeader("~", "Fishing", "Automatically pull when catch a fish.");
    ToggleRow("Auto Fishing", "Automatically reel in fish when caught", &fishingEnabled, 100);
}

static void DrawAutoCrimePanel() {
    SectionHeader("!", "AutoCrime", "Automatically defeat a crime");
    ToggleRow("Enable AutoCrime", "Automatically fight criminals", &autoCrimeEnabled, 110);
}

// ==================== DATA TABLES ====================

struct TabInfo   { const char* icon; const char* label; };
struct FeatInfo  { const char* icon; const char* title; const char* desc; };

static const TabInfo TABS[] = {
    { "M", "Menu"       },
    { "A", "Automation" },
    { "W", "World"      },
    { "P", "Plugins"    },
    { "S", "Settings"   },
    { "L", "Log"        },
};
static const int TAB_COUNT = 6;

static const FeatInfo MENU_FEATS[] = {
    { "B", "Basic",    "Basic cheats for growtopia."              },
    { "V", "Visual",   "A cheat that only you can see"            },
    { "F", "FindPath", "Teleport to target block"                 },
    { "+", "Fast",     "Faster ability for action"                },
    { "E", "ESP",      "Extra Sensory Perception"                 },
    { "X", "Extract",  "Extract some floating items in tile."     },
    { "i", "Info",     "Display raw information"                  },
};
static const int MENU_FEAT_COUNT = 7;

static const FeatInfo AUTO_FEATS[] = {
    { "S", "Spammer",    "Automatically send chat without typing it manually."          },
    { "F", "Autofarm",   "Automatically farming without effort touching the screen"     },
    { "A", "Automation", "Auto plant and harvest with auto drop and auto take seed features." },
    { "C", "Collect",    "Automation about collect utils"                               },
    { "~", "Fishing",    "Automatically pull when catch a fish."                       },
    { "!", "AutoCrime",  "Automatically defeat a crime"                                 },
};
static const int AUTO_FEAT_COUNT = 6;

// ==================== DRAW MENU ====================

void DrawMenu() {
    menuStyle();

    ImGuiIO& io  = ImGui::GetIO();
    float    sw  = io.DisplaySize.x;
    float    sh  = io.DisplaySize.y;

    float menuW = sw  * 0.88f;
    float menuH = sh  * 0.78f;
    ImGui::SetNextWindowPos(ImVec2((sw - menuW) * 0.5f, (sh - menuH) * 0.5f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);

    ImGui::Begin("##modmenu", nullptr,
        ImGuiWindowFlags_NoTitleBar       |
        ImGuiWindowFlags_NoResize         |
        ImGuiWindowFlags_NoScrollbar      |
        ImGuiWindowFlags_NoScrollWithMouse);

    float innerW = menuW  - 20.0f;
    float innerH = menuH  - 20.0f;
    float sideW  = 62.0f;
    float midW   = innerW * 0.30f;
    float rightW = innerW - sideW - midW - 16.0f;

    // ---- LEFT SIDEBAR ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.11f, 0.16f, 1.0f));
    ImGui::BeginChild("##sidebar", ImVec2(sideW, innerH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetCursorPosY(6.0f);
    for (int i = 0; i < TAB_COUNT; i++) {
        float btnX = (sideW - 52.0f) * 0.5f;
        ImGui::SetCursorPosX(btnX > 0 ? btnX : 0.0f);
        if (TabButton(TABS[i].icon, TABS[i].label, selectedTab == i, i)) {
            selectedTab     = i;
            selectedFeature = 0;
        }
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 8);

    // ---- MIDDLE PANEL ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.13f, 0.19f, 1.0f));
    ImGui::BeginChild("##middle", ImVec2(midW, innerH), false);

    ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.90f), "%s", TABS[selectedTab].label);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const FeatInfo* feats = nullptr;
    int featCount = 0;
    if (selectedTab == 0) { feats = MENU_FEATS; featCount = MENU_FEAT_COUNT; }
    if (selectedTab == 1) { feats = AUTO_FEATS; featCount = AUTO_FEAT_COUNT; }

    if (feats) {
        for (int i = 0; i < featCount; i++) {
            if (FeatureItem(feats[i].icon, feats[i].title, feats[i].desc,
                            selectedFeature == i, i + 200)) {
                selectedFeature = i;
            }
            ImGui::Spacing();
        }
    } else {
        ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "Coming soon...");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 8);

    // ---- RIGHT PANEL ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.11f, 0.16f, 1.0f));
    ImGui::BeginChild("##right", ImVec2(rightW, innerH), false);
    ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));

    if (selectedTab == 0) {
        switch (selectedFeature) {
            case 0: DrawBasicPanel();    break;
            case 1: DrawVisualPanel();   break;
            case 2: DrawFindPathPanel(); break;
            case 3: DrawFastPanel();     break;
            case 4: DrawESPPanel();      break;
            case 5: DrawExtractPanel();  break;
            case 6: DrawInfoPanel();     break;
        }
    } else if (selectedTab == 1) {
        switch (selectedFeature) {
            case 0: DrawSpammerPanel();    break;
            case 1: DrawAutofarmPanel();   break;
            case 2: DrawAutomationPanel(); break;
            case 3: DrawCollectPanel();    break;
            case 4: DrawFishingPanel();    break;
            case 5: DrawAutoCrimePanel();  break;
        }
    } else {
        ImGui::TextColored(ImVec4(0.60f, 0.57f, 0.70f, 1.0f), "Coming soon...");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
}

void *thread(void *) {
    LOGI(OBFUSCATE("Main Thread Loaded: %d"), gettid());
    initModMenu((void *)DrawMenu);

    //Hooks, Patches and Pointers here

    LOGI("Main thread done");
    pthread_exit(0);
}

// Call anything from JNI_OnLoad here
extern "C" {
    JavaVM *jvm = nullptr;
    JNIEnv *env = nullptr;

    __attribute__((visibility ("default")))
    jint loadJNI(JavaVM *vm) {
        jvm = vm;
        vm->AttachCurrentThread(&env, nullptr);
        LOGI("loadJNI(): Initialized");
        return JNI_VERSION_1_6;
    }
}

__attribute__((constructor))
void init() {
    LOGI("Loaded Mod Menu");

    pthread_t t;
    pthread_create(&t, nullptr, thread, nullptr);

    RemapTools::RemapLibrary("libLoader.so");
}
