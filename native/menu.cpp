// The settings panel the gear opens, laid out as Black-Android's: a centred panel of rows, each a label with its value
// between two arrows, under section headings, with Back and Defaults at the foot. A tap on an arrow, a key, or the
// pad's D-pad steps a value; the change is applied and saved at once.
#include "menu.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <utility>

#include <imgui.h>

#include "app/app.h"
#include "common/options.h"

namespace menu {

namespace {

std::atomic<bool> g_open{false};

constexpr ImVec4 kPanelBg{0.06f, 0.05f, 0.05f, 0.95f};
constexpr ImVec4 kBorder{0.72f, 0.14f, 0.10f, 0.80f};
constexpr ImVec4 kAccent{0.72f, 0.14f, 0.10f, 1.0f};
constexpr ImVec4 kAccentHovered{0.82f, 0.22f, 0.14f, 1.0f};
constexpr ImVec4 kGold{0.92f, 0.74f, 0.36f, 1.0f};
constexpr ImVec4 kChip{0.16f, 0.14f, 0.13f, 1.0f};
constexpr ImVec4 kChipHovered{0.26f, 0.22f, 0.20f, 1.0f};
constexpr ImVec4 kText{0.93f, 0.91f, 0.88f, 1.0f};
constexpr ImVec4 kDim{0.60f, 0.57f, 0.54f, 1.0f};
constexpr ImVec4 kHighlight{0.30f, 0.11f, 0.09f, 1.0f};
constexpr ImVec4 kLine{0.92f, 0.74f, 0.36f, 0.35f};
constexpr std::pair<ImGuiCol, ImVec4> kPanelColors[] = {
    {ImGuiCol_WindowBg, kPanelBg}, {ImGuiCol_Border, kBorder}, {ImGuiCol_Text, kText},
    {ImGuiCol_Header, kHighlight}, {ImGuiCol_HeaderHovered, kHighlight}, {ImGuiCol_HeaderActive, kHighlight},
    {ImGuiCol_Button, kChip}, {ImGuiCol_ButtonHovered, kChipHovered}, {ImGuiCol_ButtonActive, kAccentHovered},
    {ImGuiCol_FrameBg, kChip}, {ImGuiCol_FrameBgHovered, kChipHovered}, {ImGuiCol_FrameBgActive, kAccent},
    {ImGuiCol_ScrollbarGrab, kChipHovered}, {ImGuiCol_ScrollbarGrabHovered, kAccent},
    {ImGuiCol_ScrollbarGrabActive, kAccentHovered}, {ImGuiCol_Separator, kLine}, {ImGuiCol_NavHighlight, kGold},
};

// the rows, in the order the panel draws them
enum RowId : int { kResolution, kAntiAlias, kAniso, kVsync, kFrameSkip, kAspect, kFpsCounter, kHdr };
enum class Kind { Heading, Value };
struct Row {
    Kind kind;
    int id;
    const char* label;
};
constexpr Row kRows[] = {
    {Kind::Heading, -1, "GRAPHICS"},
    {Kind::Value, kResolution, "RENDER RESOLUTION"},
    {Kind::Value, kAntiAlias, "ANTI-ALIASING"},
    {Kind::Value, kAniso, "ANISOTROPIC FILTERING"},
    {Kind::Heading, -1, "PERFORMANCE"},
    {Kind::Value, kVsync, "VERTICAL SYNC"},
    {Kind::Value, kFrameSkip, "FRAME SKIP"},
    {Kind::Heading, -1, "DISPLAY"},
    {Kind::Value, kAspect, "ASPECT RATIO"},
    {Kind::Value, kFpsCounter, "FPS COUNTER"},
    {Kind::Value, kHdr, "HDR"},
};

// the render's height against the output's, 20 to 200% in steps of 5, with the pixels it makes shown beside it
constexpr uint32_t kResMin = 20, kResMax = 200, kResStep = 5;
constexpr uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"GAME DEFAULT", "2X", "4X", "8X", "16X"};
const char* const kAspectNames[] = {"ORIGINAL", "16:9", "FULL"};
const char* const kAntiAliasNames[] = {"OFF", "SMAA", "MSAA 2X", "MSAA 4X"};

int Wrap(int index, int step, int count) {
    if (count <= 0) return 0;
    index = (index + step) % count;
    return index < 0 ? index + count : index;
}

// the index of the value nearest v, for a setting that may hold one the table does not
int Nearest(const uint32_t* values, int count, uint32_t v) {
    int at = 0;
    uint32_t far = UINT32_MAX;
    for (int i = 0; i < count; i++) {
        uint32_t d = values[i] > v ? values[i] - v : v - values[i];
        if (d < far) {
            far = d;
            at = i;
        }
    }
    return at;
}

// v clamped to lo to hi, stepped by inc through the values that wraps over
uint32_t StepRange(uint32_t v, uint32_t lo, uint32_t hi, uint32_t inc, int step) {
    int count = int((hi - lo) / inc) + 1;
    int at = Wrap(int((std::clamp(v, lo, hi) - lo) / inc), step, count);
    return lo + uint32_t(at) * inc;
}

int AntiAliasIndex(const game::Options& o) {
    if (o.msaa >= 4) return 3;
    if (o.msaa >= 2) return 2;
    return o.smaa ? 1 : 0;
}

// HDR needs a display that offers it; the switch is shown greyed when none does
bool Available(int id) { return id != kHdr || game::g_hdr_display.load(); }

// the row's value as it shows now, the step applied first when it is not zero
void StepValue(int id, int step, char (&text)[64]) {
    app::Settings& s = app::Get();
    game::Options& o = game::Opt();
    switch (id) {
        case kResolution: {
            if (step) s.render_percent = StepRange(s.render_percent, kResMin, kResMax, kResStep, step);
            app::Mode out = app::Output();
            std::snprintf(text, sizeof(text), "%u%% (%ux%u)", s.render_percent, out.width * s.render_percent / 100,
                          out.height * s.render_percent / 100);
            break;
        }
        case kAntiAlias: {
            int i = AntiAliasIndex(o);
            if (step) {
                i = Wrap(i, step, int(std::size(kAntiAliasNames)));
                o.smaa = i == 1;
                o.msaa = i == 2 ? 2 : i == 3 ? 4 : 0;
            }
            std::snprintf(text, sizeof(text), "%s", kAntiAliasNames[i]);
            break;
        }
        case kAniso: {
            int i = Nearest(kAnisoValues, int(std::size(kAnisoValues)), s.aniso);
            if (step) {
                i = Wrap(i, step, int(std::size(kAnisoValues)));
                s.aniso = kAnisoValues[i];
            }
            std::snprintf(text, sizeof(text), "%s", kAnisoNames[i]);
            break;
        }
        case kVsync:
            if (step) s.vsync = !s.vsync;
            std::snprintf(text, sizeof(text), "%s", s.vsync ? "ON" : "OFF");
            break;
        case kFrameSkip:
            if (step) o.frame_skip = !o.frame_skip;
            std::snprintf(text, sizeof(text), "%s", o.frame_skip ? "AUTO" : "OFF");
            break;
        case kAspect: {
            uint32_t v = std::min<uint32_t>(o.aspect, 2);
            if (step) v = StepRange(v, 0, 2, 1, step);
            o.aspect = v;
            std::snprintf(text, sizeof(text), "%s", kAspectNames[v]);
            break;
        }
        case kFpsCounter:
            if (step) o.fps_counter = !o.fps_counter;
            std::snprintf(text, sizeof(text), "%s", o.fps_counter ? "ON" : "OFF");
            break;
        case kHdr:
            if (!Available(id)) {
                std::snprintf(text, sizeof(text), "NOT AVAILABLE");
                break;
            }
            if (step) o.hdr_output = !o.hdr_output;
            std::snprintf(text, sizeof(text), "%s", o.hdr_output ? "ON" : "OFF");
            break;
    }
}

void Heading(const char* text) {
    float em = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                                        ImGui::GetColorU32(ImVec4(kGold.x, kGold.y, kGold.z, 0.4f)), 1.0f);
    ImGui::Dummy(ImVec2(0, em * 0.2f));
}

// a row's place and size: the label left, the value block (an arrow each side of the value) right
struct RowBox {
    float em;
    float row_w;
    float row_h;
    float value_w;
    float block_w;
    float row_x;
    float row_y;
};

RowBox MeasureRow() {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox box;
    box.em = ImGui::GetFontSize();
    box.row_w = ImGui::GetContentRegionAvail().x;
    box.row_h = ImGui::GetFrameHeight();
    box.value_w = box.em * 12.0f;
    box.block_w = box.row_h * 2 + box.value_w + style.ItemSpacing.x * 2;
    box.row_x = ImGui::GetCursorPosX();
    box.row_y = ImGui::GetCursorPosY();
    return box;
}

// the cursor back to the row's start, then past the whole row
void EndRow(const RowBox& box) {
    ImGui::SetCursorPos(ImVec2(box.row_x, box.row_y));
    ImGui::Dummy(ImVec2(box.row_w, box.row_h));
}

// label left, arrows and value right, the row the only nav stop: a tap on an arrow, the left and right keys or the
// pad's D-pad step it, and a held arrow keeps stepping
void ValueRow(int id, const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox box = MeasureRow();
    bool available = Available(id);
    char text[64];
    StepValue(id, 0, text);

    ImGui::PushID(label);
    int step = 0;
    if (available) {
        ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(box.row_w, box.row_h));
        if (ImGui::IsItemFocused()) {
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                      ImGui::GetColorU32(ImGuiCol_Header));
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) step = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) step = 1;
        }
    } else {
        ImGui::Dummy(ImVec2(box.row_w, box.row_h));
    }

    ImGui::SetCursorPos(ImVec2(box.row_x + style.ItemInnerSpacing.x, box.row_y));
    ImGui::AlignTextToFramePadding();
    if (!available) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(label);
    if (!available) ImGui::PopStyleColor();

    float block_x = box.row_x + box.row_w - box.block_w;
    if (available) {
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_ButtonRepeat, true);  // held arrows keep stepping
        ImGui::SetCursorPos(ImVec2(block_x, box.row_y));
        if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) step = -1;
    }
    float value_x = block_x + box.row_h + style.ItemSpacing.x;
    float pad = (box.value_w - ImGui::CalcTextSize(text).x) * 0.5f;
    ImGui::SetCursorPos(ImVec2(value_x + std::max(pad, 0.0f), box.row_y));
    ImGui::AlignTextToFramePadding();
    if (!available) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(text);
    if (!available) ImGui::PopStyleColor();
    if (available) {
        ImGui::SetCursorPos(ImVec2(value_x + box.value_w + style.ItemSpacing.x, box.row_y));
        if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) step = 1;
        ImGui::PopItemFlag();
    }
    ImGui::PopID();
    EndRow(box);

    if (step) {
        StepValue(id, step, text);
        app::Save();
    }
}

// the settings back to a fresh install's
void Defaults() {
    app::Settings& s = app::Get();
    game::Options& o = game::Opt();
    s.render_percent = 67;
    s.aniso = 0;
    s.vsync = true;
    o.smaa = false;
    o.msaa = 0;
    o.frame_skip = true;
    o.aspect = 0;
    o.fps_counter = true;
    o.hdr_output = false;
    app::Save();
}

// the hint line, then Back and Defaults on the right
void Footer(float em) {
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted("Changes are saved as you make them");
    ImGui::PopStyleColor();
    float bw = em * 8.0f;
    float buttons_pad = ImGui::GetContentRegionAvail().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(buttons_pad, 0.0f));
    if (ImGui::Button("BACK", ImVec2(bw, 0))) Toggle();
    ImGui::SameLine();
    if (ImGui::Button("DEFAULTS", ImVec2(bw, 0))) Defaults();
}

void DrawPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(em * 50.0f, vp->Size.x * 0.94f),
                                   std::min(em * 32.0f, vp->Size.y * 0.92f)),
                             ImGuiCond_Always);
    for (const auto& [col, color] : kPanelColors) ImGui::PushStyleColor(col, color);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, em * 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, em * 0.45f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 1.6f, em * 1.2f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, em * 1.5f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##settings", nullptr, kFlags)) {
        const char* title = "SETTINGS";
        float title_pad = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(title_pad, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Separator();
        float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + em * 0.5f;
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        for (const Row& r : kRows) {
            if (r.kind == Kind::Heading) Heading(r.label);
            else ValueRow(r.id, r.label);
        }
        ImGui::EndChild();
        Footer(em);
    }
    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(int(std::size(kPanelColors)));
}

}  // namespace

bool Open() { return g_open.load(std::memory_order_relaxed); }

void Toggle() { g_open.store(!Open(), std::memory_order_relaxed); }

void Draw() {
    if (!Open()) return;
    ImGuiIO& io = ImGui::GetIO();
    // finger sized: the font twice ImGui's 13 pixels on a 1080 line screen
    io.FontGlobalScale = std::max(1.0f, io.DisplaySize.y / 540.0f);
    DrawPanel();
}

}  // namespace menu
