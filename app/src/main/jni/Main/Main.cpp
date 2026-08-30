//
// Created by reveny on 21/08/2023.
//

#include "../Include/KittyMemory/MemoryPatch.h"
#include "../Include/ImGui.h"
#include "../Include/RemapTools.h"

#include "../Include/Drawing.h"
#include "../Include/Unity.h"

#include <cfloat>
#include <cstdio>
#include <string>

// JNI Support
JavaVM *jvm = nullptr;
JNIEnv *env = nullptr;

// Toggle state for the Basic panel's movement modifications. The injection
// layer (hooks/patches, applied elsewhere) reads these flags each frame to
// decide whether to apply the corresponding modification.
namespace ModState {
    bool modFly = false;         // alters player vertical velocity to simulate creative flight
    bool antiLava = false;       // detects lava tiles and prevents damage events
    bool antiRespawn = false;    // intercepts respawn triggers and restores position
    bool seeLockedDoor = false;  // reads door ownership labels without interacting
    bool noclipGhost = false;    // removes collision boundaries while keeping visual rendering active
    bool visualInvisV2 = false;  // adjusts player render opacity to zero while keeping input handling
}

// Persists the menu window's position/size across sessions.
namespace MenuState {
    ImVec2 position = ImVec2(60.0f, 60.0f);
    ImVec2 size = ImVec2(520.0f, 380.0f);
    bool loaded = false;

    // Resolves the current app's private files directory via JNI so the
    // config file survives regardless of which app the menu is injected into.
    std::string GetConfigPath() {
        std::string path;

        if (jvm != nullptr) {
            JNIEnv *localEnv = nullptr;
            bool attached = false;

            if (jvm->GetEnv((void **)&localEnv, JNI_VERSION_1_6) != JNI_OK) {
                attached = jvm->AttachCurrentThread(&localEnv, nullptr) == JNI_OK;
            }

            if (localEnv != nullptr) {
                jclass activityThreadClass = localEnv->FindClass(OBFUSCATE("android/app/ActivityThread"));
                if (activityThreadClass != nullptr) {
                    jmethodID currentApplication = localEnv->GetStaticMethodID(activityThreadClass, OBFUSCATE("currentApplication"), OBFUSCATE("()Landroid/app/Application;"));
                    jobject app = currentApplication ? localEnv->CallStaticObjectMethod(activityThreadClass, currentApplication) : nullptr;

                    if (app != nullptr) {
                        jclass contextClass = localEnv->GetObjectClass(app);
                        jmethodID getFilesDir = localEnv->GetMethodID(contextClass, OBFUSCATE("getFilesDir"), OBFUSCATE("()Ljava/io/File;"));
                        jobject filesDir = getFilesDir ? localEnv->CallObjectMethod(app, getFilesDir) : nullptr;

                        if (filesDir != nullptr) {
                            jclass fileClass = localEnv->GetObjectClass(filesDir);
                            jmethodID getAbsolutePath = localEnv->GetMethodID(fileClass, OBFUSCATE("getAbsolutePath"), OBFUSCATE("()Ljava/lang/String;"));
                            auto pathStr = (jstring)(getAbsolutePath ? localEnv->CallObjectMethod(filesDir, getAbsolutePath) : nullptr);

                            if (pathStr != nullptr) {
                                const char *chars = localEnv->GetStringUTFChars(pathStr, nullptr);
                                path = std::string(chars) + OBFUSCATE("/.modmenu_state");
                                localEnv->ReleaseStringUTFChars(pathStr, chars);
                                localEnv->DeleteLocalRef(pathStr);
                            }
                            localEnv->DeleteLocalRef(filesDir);
                        }
                        localEnv->DeleteLocalRef(app);
                    }
                    localEnv->DeleteLocalRef(activityThreadClass);
                }

                if (localEnv->ExceptionCheck()) {
                    localEnv->ExceptionClear();
                    path.clear();
                }

                if (attached) {
                    jvm->DetachCurrentThread();
                }
            }
        }

        if (path.empty()) {
            path = OBFUSCATE("/data/local/tmp/.modmenu_state");
        }

        return path;
    }

    void Load() {
        if (loaded) return;
        loaded = true;

        FILE *f = fopen(GetConfigPath().c_str(), "r");
        if (f != nullptr) {
            fscanf(f, "%f %f %f %f", &position.x, &position.y, &size.x, &size.y);
            fclose(f);
        }
    }

    void Save() {
        FILE *f = fopen(GetConfigPath().c_str(), "w");
        if (f != nullptr) {
            fprintf(f, "%f %f %f %f", position.x, position.y, size.x, size.y);
            fclose(f);
        }
    }
}

void menuStyle() {
    ImGui::StyleColorsDark();

    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.93f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.53f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.08f, 0.09f, 0.96f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.25f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.55f, 0.90f, 0.60f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.30f, 0.55f, 0.90f, 0.90f);
    colors[ImGuiCol_Header]                = ImVec4(0.30f, 0.55f, 0.90f, 0.55f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.30f, 0.55f, 0.90f, 0.75f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.30f, 0.55f, 0.90f, 0.90f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.55f, 0.90f, 0.40f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.30f, 0.55f, 0.90f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.30f, 0.55f, 0.90f, 0.95f);
}

// Category content panels

static void DrawBasicPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Basic"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox(OBFUSCATE("ModFly"), &ModState::modFly);
    ImGui::Checkbox(OBFUSCATE("Anti Lava"), &ModState::antiLava);
    ImGui::Checkbox(OBFUSCATE("Anti Respawn"), &ModState::antiRespawn);
    ImGui::Checkbox(OBFUSCATE("See Locked Door"), &ModState::seeLockedDoor);
    ImGui::Checkbox(OBFUSCATE("Noclip and Ghost"), &ModState::noclipGhost);
    ImGui::Checkbox(OBFUSCATE("Visual Invis V2"), &ModState::visualInvisV2);
}

static void DrawVisualPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Visual"));
    ImGui::Separator();
    ImGui::Spacing();

    static bool skinChanger = false;
    static float fov = 90.0f;

    ImGui::Checkbox(OBFUSCATE("Skin Changer"), &skinChanger);
    ImGui::SliderFloat(OBFUSCATE("FOV"), &fov, 60.0f, 120.0f);
}

static void DrawFindPathPanel() {
    ImGui::TextUnformatted(OBFUSCATE("FindPath"));
    ImGui::Separator();
    ImGui::Spacing();

    static bool autoPath = false;
    static float pathSpeed = 1.0f;

    ImGui::Checkbox(OBFUSCATE("Auto Path"), &autoPath);
    ImGui::SliderFloat(OBFUSCATE("Path Speed"), &pathSpeed, 0.5f, 3.0f);
}

static void DrawFastPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Fast"));
    ImGui::Separator();
    ImGui::Spacing();

    static bool fastFarm = false;
    static float speedMultiplier = 1.0f;

    ImGui::Checkbox(OBFUSCATE("Fast Farm"), &fastFarm);
    ImGui::SliderFloat(OBFUSCATE("Speed Multiplier"), &speedMultiplier, 1.0f, 5.0f);
}

static void DrawEspPanel() {
    ImGui::TextUnformatted(OBFUSCATE("ESP"));
    ImGui::Separator();
    ImGui::Spacing();

    static bool boxEsp = false;
    static bool nameEsp = false;
    static bool distanceEsp = false;

    ImGui::Checkbox(OBFUSCATE("Box ESP"), &boxEsp);
    ImGui::Checkbox(OBFUSCATE("Name ESP"), &nameEsp);
    ImGui::Checkbox(OBFUSCATE("Distance ESP"), &distanceEsp);
}

static void DrawExtractPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Extract"));
    ImGui::Separator();
    ImGui::Spacing();

    static bool autoExtract = false;

    ImGui::Checkbox(OBFUSCATE("Auto Extract"), &autoExtract);
}

static void DrawInfoPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Info"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted(OBFUSCATE("Android ImGui Mod Menu"));
    ImGui::TextWrapped(OBFUSCATE("Use the category list on the left to switch between panels."));
}

struct Category {
    const char *name;
    void (*draw)();
};

static Category categories[] = {
    { "Basic",    DrawBasicPanel },
    { "Visual",   DrawVisualPanel },
    { "FindPath", DrawFindPathPanel },
    { "Fast",     DrawFastPanel },
    { "ESP",      DrawEspPanel },
    { "Extract",  DrawExtractPanel },
    { "Info",     DrawInfoPanel },
};

static int activeCategory = 0;

void DrawMenu() {
    MenuState::Load();

    ImGui::SetNextWindowPos(MenuState::position, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(MenuState::size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 280.0f), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin(OBFUSCATE("Mod Menu"), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##sidebar", ImVec2(130.0f, 0.0f), true);
    for (int i = 0; i < IM_ARRAYSIZE(categories); i++) {
        bool selected = (activeCategory == i);

        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }

        if (ImGui::Button(categories[i].name, ImVec2(-FLT_MIN, 0.0f))) {
            activeCategory = i;
        }

        if (selected) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), true);
    categories[activeCategory].draw();
    ImGui::EndChild();

    // Persist position/size once the drag or resize has settled.
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (pos.x != MenuState::position.x || pos.y != MenuState::position.y ||
        size.x != MenuState::size.x || size.y != MenuState::size.y) {
        MenuState::position = pos;
        MenuState::size = size;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            MenuState::Save();
        }
    }

    ImGui::End();
}

void *thread(void *) {
    LOGI(OBFUSCATE("Main Thread Loaded: %d"), gettid());
    initModMenu((void *)DrawMenu);

    //Hooks, Patches and Pointers here
    //Example:
    /*
     * DobbyHook(getAbsoluteAddress("libIl2cpp.so", 0x0), FunctionExample, old_FunctionExample);
     * SetAimRotation = (void (*)(void *, Quaternion)) getAbsoluteAddress("libIl2cpp.so", 0x0);
     */

    LOGI("Main thread done");
    pthread_exit(0);
}

// Call anything from JNI_OnLoad here
extern "C" {
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

    //Don't leave any traces, remap the loader lib as well
    RemapTools::RemapLibrary("libLoader.so");
}
