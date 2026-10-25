// The on-screen controls, laid out as the original Xbox pad's: the left stick walks, the four face buttons are the
// pad's, and a drag on the right turns the camera. Their art is the pad's own colours, from native/ui. Touches come
// from SDL's finger events in window coordinates; what they leave is overlaid on the pad the keyboard left
// (src/common/keyboard.cpp), so the game reads one report as on the console.
#include <SDL3/SDL.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "common/options.h"
#include "core/input.h"
#include "core/log.h"
#include "host.h"
#include "loose.h"
#include "ui_icons.h"
#include "vk_ui.h"

namespace touch {

namespace {

// what a finger is doing; the gear is past the pad's buttons
enum Role : int { kNone = -1, kStick = 0, kCamera = 1, kButton = 2 };
// the pad's analog buttons, in input::Pad::analog order
enum Analog : int { kA = 0, kB = 1, kX = 2, kY = 3, kWhite = 4, kBlack = 5, kLeftTrigger = 6, kRightTrigger = 7 };
enum Bit : uint16_t { kUp = 1 << 0, kDown = 1 << 1, kLeft = 1 << 2, kRight = 1 << 3, kStart = 1 << 4, kBack = 1 << 5,
                       kLeftThumb = 1 << 6, kRightThumb = 1 << 7 };
// the on-screen buttons, in the order the layout and the art have them
enum Button : int { kBtnA = 0, kBtnB = 1, kBtnX = 2, kBtnY = 3, kBtnStart = 4, kBtnBack = 5, kBtnDpad = 6,
                     kBtnLT = 7, kBtnRT = 8, kBtnL3 = 9, kBtnR3 = 10, kBtnCount = 11 };

std::atomic<float> g_lx{0}, g_ly{0};  // the stick, -1 to 1, y up
std::atomic<float> g_rx{0}, g_ry{0};  // the camera's pending turn, -1 to 1
std::atomic<uint64_t> g_analog{0};    // one byte per analog button
std::atomic<uint32_t> g_buttons{0};   // Bit
std::atomic<bool> g_pad{false};       // a controller is plugged in: the screen's controls are hidden while one is

struct Touch {
    SDL_FingerID id;
    int role;
    float x, y;  // window pixels
};
std::mutex g_lock;
std::vector<Touch> g_touches;

// what the fingers held lets go: the panel opened, or a controller took over
void ReleaseAll() {  // g_lock held
    g_touches.clear();
    g_analog = 0;
    g_buttons = 0;
    g_lx = g_ly = g_rx = g_ry = 0;
}

struct Layout {
    float w, h, s;
    float stick[2], stick_r;
    float btn[4][2], btn_r;  // A B X Y, a diamond as the pad has them: Y up, X left, B right, A down
    float dpad[2];           // the arrows' middle
    float arrow[4][2], arrow_r;  // up, down, left, right: apart, each a button of its own
    float pill[kBtnCount][2];  // START BACK LT RT, by their button
    float pill_w, pill_h;
    float gear[2], gear_r;  // the settings panel's button, between BACK and START
};
constexpr int kGear = kButton + kBtnCount;
constexpr int kArrow = kGear + 1;  // the four arrows after it, up down left right
constexpr uint32_t kArrowBits[4] = {kUp, kDown, kLeft, kRight};

// where each control sits: its centre as a fraction of the screen, in this order. These are the pad's own layout,
// what a fresh install gets and what the editor's RESET puts back.
enum Unit : int {
    kUnitStick, kUnitA, kUnitB, kUnitX, kUnitY, kUnitDpad, kUnitStart, kUnitBack, kUnitLT, kUnitRT, kUnitL3, kUnitR3,
    kUnitGear, kUnitCount
};
struct Point {
    float x, y;
};
constexpr Point kDefaultUnits[kUnitCount] = {
    {0.130f, 0.760f},  // the stick
    {0.855f, 0.820f},  // A, down
    {0.909f, 0.700f},  // B, right
    {0.801f, 0.700f},  // X, left
    {0.855f, 0.580f},  // Y, up
    {0.130f, 0.393f},  // the arrows' middle
    {0.580f, 0.090f},  // START
    {0.420f, 0.090f},  // BACK
    {0.090f, 0.090f},  // LT
    {0.910f, 0.090f},  // RT
    {0.235f, 0.920f},  // L3
    {0.700f, 0.920f},  // R3
    {0.500f, 0.090f},  // the gear
};
std::array<Point, kUnitCount> g_units = [] {
    std::array<Point, kUnitCount> a{};
    std::copy(std::begin(kDefaultUnits), std::end(kDefaultUnits), a.begin());
    return a;
}();
// the pills, in the order the editor numbers them
constexpr int kPillButton[6] = {kBtnStart, kBtnBack, kBtnLT, kBtnRT, kBtnL3, kBtnR3};
constexpr int kPillUnit[6] = {kUnitStart, kUnitBack, kUnitLT, kUnitRT, kUnitL3, kUnitR3};

std::atomic<bool> g_editing{false};
int g_drag = -1;            // the unit a finger is moving
SDL_FingerID g_drag_id = 0;
float g_grab[2] = {0, 0};   // where the finger took it, from its centre

Layout Make(float w, float h) {
    Layout l{};
    l.w = w;
    l.h = h;
    l.s = std::min(w, h);
    // the panel's size setting scales the controls, not where they sit
    const float k = std::clamp(game::Opt().controls_scale / 100.0f, 0.5f, 2.0f);
    l.stick_r = l.s * 0.11f * k;
    l.btn_r = l.s * 0.075f * k;
    l.arrow_r = l.s * 0.057f * k;
    l.pill_w = l.s * 0.165f * k;
    l.pill_h = l.s * 0.075f * k;
    l.gear_r = l.s * 0.0322f * k;
    auto place = [&](int u, float out[2]) {
        out[0] = g_units[size_t(u)].x * w;
        out[1] = g_units[size_t(u)].y * h;
    };
    place(kUnitStick, l.stick);
    place(kUnitA, l.btn[kBtnA]);
    place(kUnitB, l.btn[kBtnB]);
    place(kUnitX, l.btn[kBtnX]);
    place(kUnitY, l.btn[kBtnY]);
    place(kUnitDpad, l.dpad);
    place(kUnitStart, l.pill[kBtnStart]);
    place(kUnitBack, l.pill[kBtnBack]);
    place(kUnitLT, l.pill[kBtnLT]);
    place(kUnitRT, l.pill[kBtnRT]);
    place(kUnitL3, l.pill[kBtnL3]);
    place(kUnitR3, l.pill[kBtnR3]);
    place(kUnitGear, l.gear);
    // the arrows keep their ring around the d-pad's middle
    const float off = l.s * 0.100f * k;
    const float dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (int a = 0; a < 4; a++) {
        l.arrow[a][0] = l.dpad[0] + dirs[a][0] * off;
        l.arrow[a][1] = l.dpad[1] + dirs[a][1] * off;
    }
    return l;
}

bool InCircle(float x, float y, const float c[2], float r) {
    return (x - c[0]) * (x - c[0]) + (y - c[1]) * (y - c[1]) <= r * r;
}

bool InPill(const Layout& l, float x, float y, const float c[2]) {
    return std::fabs(x - c[0]) <= l.pill_w / 2 && std::fabs(y - c[1]) <= l.pill_h / 2;
}

// the control under a finger, for the editor: -1 when none is
int HitUnit(const Layout& l, float x, float y) {
    if (InCircle(x, y, l.gear, l.gear_r * 1.4f)) return kUnitGear;
    for (int i = 0; i < 6; i++)
        if (InPill(l, x, y, l.pill[kPillButton[i]])) return kPillUnit[i];
    for (int b = 0; b < 4; b++)
        if (InCircle(x, y, l.btn[b], l.btn_r)) return kUnitA + b;
    if (InCircle(x, y, l.dpad, l.arrow_r * 2.0f)) return kUnitDpad;
    if (InCircle(x, y, l.stick, l.stick_r)) return kUnitStick;
    return -1;
}

int HitTest(const Layout& l, float x, float y) {
    if (InCircle(x, y, l.gear, l.gear_r * 1.4f)) return kGear;
    // a pad takes every touch while one is plugged in, whether SDL opened it or the phone handed it over as a
    // virtual device; the settings panel is the activity's own window and takes its touches before they get here
    if (input::Connected() || loose::Connected()) return kNone;
    for (int b = 0; b < 4; b++)
        if (InCircle(x, y, l.btn[b], l.btn_r * 1.15f)) return kButton + b;
    for (int b : {kBtnStart, kBtnBack, kBtnLT, kBtnRT, kBtnL3, kBtnR3})
        if (InPill(l, x, y, l.pill[b])) return kButton + b;
    for (int a = 0; a < 4; a++)
        if (InCircle(x, y, l.arrow[a], l.arrow_r * 1.1f)) return kArrow + a;
    if (x < l.w * 0.45f && InCircle(x, y, l.stick, l.stick_r * 1.7f)) return kStick;
    if (x > l.w * 0.45f) return kCamera;
    return kNone;
}

void PressButton(int index, bool down) {
    static const int kAnalogOf[4] = {kA, kB, kX, kY};
    if (index < 4) {
        uint64_t v = g_analog.load();
        uint32_t shift = uint32_t(kAnalogOf[index]) * 8;
        v = down ? (v | (0xFFull << shift)) : (v & ~(0xFFull << shift));
        g_analog = v;
    } else if (index == kBtnStart || index == kBtnBack || index == kBtnL3 || index == kBtnR3) {
        uint16_t bit = index == kBtnStart ? kStart
                     : index == kBtnBack  ? kBack
                     : index == kBtnL3    ? kLeftThumb
                                          : kRightThumb;
        uint32_t v = g_buttons.load();
        v = down ? (v | bit) : (v & ~uint32_t(bit));
        g_buttons = v;
    } else if (index == kBtnLT || index == kBtnRT) {
        uint64_t v = g_analog.load();
        uint32_t shift = uint32_t(index == kBtnLT ? kLeftTrigger : kRightTrigger) * 8;
        v = down ? (v | (0xFFull << shift)) : (v & ~(0xFFull << shift));
        g_analog = v;
    }
}

// an arrow held or let go: its bit alone, the others as they are
void PressArrow(int arrow, bool down) {
    uint32_t v = g_buttons.load();
    v = down ? (v | kArrowBits[arrow]) : (v & ~kArrowBits[arrow]);
    g_buttons = v;
}

void StickTo(float x, float y, const Layout& l) {
    float dx = (x - l.stick[0]) / l.stick_r;
    float dy = (y - l.stick[1]) / l.stick_r;
    float len = std::hypot(dx, dy);
    if (len > 1) {
        dx /= len;
        dy /= len;
    }
    g_lx = std::clamp(dx, -1.0f, 1.0f);
    g_ly = std::clamp(-dy, -1.0f, 1.0f);  // screen y down, the stick's up
}

// the buttons' art, made into textures the first frame they are drawn: the pad's letters, or what each control does in
// the game, the panel's button style choosing which. The pills have no letters worth drawing in the game's style.
const char* const kArtPad[4] = {ui_icons::kBtnA, ui_icons::kBtnB, ui_icons::kBtnX, ui_icons::kBtnY};
// A jump, B musou, X attack, Y charge: the pad's diamond read the way the game's manual reads it, not the order the
// buttons are numbered in
const char* const kArtGame[4] = {ui_icons::kActJump, ui_icons::kActMusou, ui_icons::kActLight, ui_icons::kActHeavy};
const char* const kPillArt[6] = {ui_icons::kActOrders, ui_icons::kActPause, ui_icons::kActGuard,
                                 ui_icons::kActShoot,  ui_icons::kActMove,  ui_icons::kActRage};
ImTextureID g_tex[4];
ImTextureID g_pill_tex[6];
int g_art_style = -1;  // the style the textures were made for, the panel's setting having changed since

int ArtStyle() { return int(std::min<uint32_t>(game::Opt().controls_style, 1)); }

ImTextureID Icon(const char* bytes) {
    return vkui::Texture(reinterpret_cast<const uint8_t*>(bytes), ui_icons::kIconSize, ui_icons::kIconSize);
}

void EnsureArt() {
    const int style = ArtStyle();
    if (g_tex[0] && g_art_style == style) return;
    const char* const* art = style ? kArtGame : kArtPad;
    for (int b = 0; b < 4; b++) g_tex[b] = Icon(art[b]);
    if (style)
        for (int i = 0; i < 6; i++) g_pill_tex[i] = Icon(kPillArt[i]);
    g_art_style = style;
}

}  // namespace

void Init() { LOGI("touch: on-screen controls"); }

void Event(const SDL_Event& e) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(host::Window(), &w, &h);
    Layout lay = Make(float(std::max(w, 1)), float(std::max(h, 1)));
    // the layout editor takes the fingers: one moves the control it took, and nothing reaches the game
    if (g_editing.load(std::memory_order_relaxed)) {
        switch (e.type) {
            case SDL_EVENT_FINGER_DOWN: {
                float x = e.tfinger.x * lay.w, y = e.tfinger.y * lay.h;
                int u = HitUnit(lay, x, y);
                if (u < 0) break;
                g_drag = u;
                g_drag_id = e.tfinger.fingerID;
                g_grab[0] = x - g_units[size_t(u)].x * lay.w;
                g_grab[1] = y - g_units[size_t(u)].y * lay.h;
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                if (g_drag < 0 || e.tfinger.fingerID != g_drag_id) break;
                float x = e.tfinger.x * lay.w - g_grab[0], y = e.tfinger.y * lay.h - g_grab[1];
                g_units[size_t(g_drag)].x = std::clamp(x / lay.w, 0.0f, 1.0f);
                g_units[size_t(g_drag)].y = std::clamp(y / lay.h, 0.0f, 1.0f);
                break;
            }
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                if (g_drag >= 0 && e.tfinger.fingerID == g_drag_id) {
                    g_drag = -1;
                    app::Save();
                }
                break;
            default:
                break;
        }
        return;
    }
    switch (e.type) {
        case SDL_EVENT_FINGER_DOWN: {
            float x = e.tfinger.x * lay.w, y = e.tfinger.y * lay.h;
            int role = HitTest(lay, x, y);
            if (role == kNone) return;
            std::lock_guard lock(g_lock);
            if (role == kGear) {
                // what the fingers held lets go: the panel opens over the game, which reads no touch until it closes
                ReleaseAll();
                host::ShowSettings();
                return;
            }
            g_touches.push_back({e.tfinger.fingerID, role, x, y});
            if (role == kStick) StickTo(x, y, lay);
            else if (role >= kArrow) PressArrow(role - kArrow, true);
            else if (role >= kButton && role < kButton + kBtnCount) PressButton(role - kButton, true);
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            std::lock_guard lock(g_lock);
            for (Touch& t : g_touches) {
                if (t.id != e.tfinger.fingerID) continue;
                float x = e.tfinger.x * lay.w, y = e.tfinger.y * lay.h;
                float dx = x - t.x, dy = y - t.y;
                t.x = x;
                t.y = y;
                if (t.role == kStick) {
                    StickTo(x, y, lay);
                } else if (t.role == kCamera) {
                    // a drag turns the view: the deflection the stick would give, eased back in Apply
                    g_rx = std::clamp(g_rx.load() + dx / (lay.s * 0.12f), -1.0f, 1.0f);
                    g_ry = std::clamp(g_ry.load() - dy / (lay.s * 0.12f), -1.0f, 1.0f);
                }
                // an arrow stays the one first touched, wherever the thumb slides: no direction changes by itself
                break;
            }
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            std::lock_guard lock(g_lock);
            for (size_t i = 0; i < g_touches.size(); i++) {
                Touch& t = g_touches[i];
                if (t.id != e.tfinger.fingerID) continue;
                if (t.role == kStick) {
                    g_lx = 0;
                    g_ly = 0;
                } else if (t.role >= kArrow) {
                    PressArrow(t.role - kArrow, false);
                } else if (t.role >= kButton && t.role < kButton + kBtnCount) {
                    PressButton(t.role - kButton, false);
                }
                g_touches.erase(g_touches.begin() + long(i));
                break;
            }
            break;
        }
        default:
            break;
    }
}

void Apply(input::Pad& pad) {
    if (g_editing.load(std::memory_order_relaxed)) return;
    uint64_t analog = g_analog.load();
    uint32_t buttons = g_buttons.load();
    float lx = g_lx.load(), ly = g_ly.load();
    float rx = g_rx.load(), ry = g_ry.load();
    if (analog || buttons || lx || ly || rx || ry) pad.connected = true;
    for (int i = 0; i < 8; i++) {
        uint8_t v = uint8_t(analog >> (i * 8));
        if (v) pad.analog[i] = v;
    }
    pad.buttons |= uint16_t(buttons);
    if (lx || ly) {
        pad.lx = int16_t(std::lround(std::clamp(lx, -1.0f, 1.0f) * 32767.0f));
        pad.ly = int16_t(std::lround(std::clamp(ly, -1.0f, 1.0f) * 32767.0f));
    }
    if (rx || ry) {
        pad.rx = int16_t(std::lround(std::clamp(rx, -1.0f, 1.0f) * 32767.0f));
        pad.ry = int16_t(std::lround(std::clamp(ry, -1.0f, 1.0f) * 32767.0f));
        // the drag's turn eases out over the next few polls, as a stick held for a moment would
        g_rx = std::fabs(rx) < 0.02f ? 0.0f : rx * 0.55f;
        g_ry = std::fabs(ry) < 0.02f ? 0.0f : ry * 0.55f;
    }
    // a controller the phone took over comes in over the top of both
    loose::Merge(pad);
}

void EndEditing() {
    g_editing = false;
    g_drag = -1;
    app::Save();
}

void ResetUnits() {
    std::copy(std::begin(kDefaultUnits), std::end(kDefaultUnits), g_units.begin());
    app::Save();
}

// the editor: every control ringed, so it is clear what a finger takes. Its Done and Reset are the activity's own bar
// (LayoutEditor.java), at the bottom of the screen, out of the way of the controls.
void DrawEditor(const Layout& l, ImDrawList* dl) {
    const ImU32 ring = IM_COL32(235, 190, 95, 200);
    dl->AddCircle(ImVec2(l.stick[0], l.stick[1]), l.stick_r, ring, 48, 2.0f);
    for (int b = 0; b < 4; b++) dl->AddCircle(ImVec2(l.btn[b][0], l.btn[b][1]), l.btn_r, ring, 32, 2.0f);
    dl->AddCircle(ImVec2(l.dpad[0], l.dpad[1]), l.arrow_r * 2.0f, ring, 32, 2.0f);
    for (int i = 0; i < 6; i++) {
        const float* c = l.pill[kPillButton[i]];
        dl->AddRect(ImVec2(c[0] - l.pill_w / 2, c[1] - l.pill_h / 2),
                    ImVec2(c[0] + l.pill_w / 2, c[1] + l.pill_h / 2), ring, l.pill_h / 2, 0, 2.0f);
    }
    dl->AddCircle(ImVec2(l.gear[0], l.gear[1]), l.gear_r * 1.4f, ring, 32, 2.0f);
}

void Draw() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    EnsureArt();
    ImGuiIO& io = ImGui::GetIO();
    Layout l = Make(io.DisplaySize.x, io.DisplaySize.y);
    // the panel's opacity: everything the controls are drawn with, the gear too, is its own alpha times this
    const float fade = std::clamp(game::Opt().controls_opacity / 100.0f, 0.2f, 1.0f);
    auto at = [fade](int a) { return int(std::lround(float(a) * fade)); };
    const ImU32 fill = IM_COL32(255, 255, 255, at(30)), line = IM_COL32(255, 255, 255, at(80));
    const ImU32 label = IM_COL32(255, 255, 255, at(210));
    ImFont* font = ImGui::GetFont();  // io.FontDefault stays null unless set: the frame's font, the atlas's first

    // the gear, drawn as the pills are: a dark disc with a thin rim, the gear's outline in it, gold while the panel
    // is open
    {
        ImVec2 c(l.gear[0], l.gear[1]);
        float r = l.gear_r;
        ImU32 stroke = label;
        dl->AddCircleFilled(c, r * 1.3f, IM_COL32(20, 20, 20, at(150)), 40);
        dl->AddCircle(c, r * 1.3f, line, 40, 1.5f);
        // eight teeth: each a flat top at the outer radius and a flat valley at the inner one, slanted sides between
        constexpr int kTeeth = 8;
        constexpr float kStep = 6.2831853f / kTeeth;
        float outer = r * 0.86f, inner = r * 0.64f;
        ImVec2 pts[kTeeth * 4];
        for (int i = 0; i < kTeeth; i++) {
            float a = float(i) * kStep;
            const float corners[4][2] = {{-0.30f, inner}, {-0.17f, outer}, {0.17f, outer}, {0.30f, inner}};
            for (int k = 0; k < 4; k++) {
                float ak = a + corners[k][0] * kStep;
                pts[i * 4 + k] = ImVec2(c.x + std::cos(ak) * corners[k][1], c.y + std::sin(ak) * corners[k][1]);
            }
        }
        float width = std::max(r * 0.11f, 1.5f);
        dl->AddPolyline(pts, kTeeth * 4, stroke, ImDrawFlags_Closed, width);
        dl->AddCircle(c, r * 0.27f, stroke, 24, width);
    }
    // a controller plugged in takes the pad's place: its controls are not drawn and take no touch, the gear staying
    // for the panel; whatever a finger held when it came in lets go
    bool pad = input::Connected() || loose::Connected();
    if (pad != g_pad.load()) {
        g_pad = pad;
        if (pad) {
            std::lock_guard lock(g_lock);
            ReleaseAll();
        }
    }
    // a controller takes the pad's place: its controls are not drawn while one is plugged in
    if (pad) return;

    // the stick
    dl->AddCircleFilled(ImVec2(l.stick[0], l.stick[1]), l.stick_r, fill, 48);
    dl->AddCircle(ImVec2(l.stick[0], l.stick[1]), l.stick_r, line, 48, 2.0f);
    dl->AddCircleFilled(ImVec2(l.stick[0] + g_lx * l.stick_r * 0.72f, l.stick[1] - g_ly * l.stick_r * 0.72f),
                        l.stick_r * 0.36f, IM_COL32(255, 255, 255, at(70)), 32);

    // the four face buttons, the pad's own art
    for (int b = 0; b < 4; b++) {
        ImVec2 c(l.btn[b][0], l.btn[b][1]);
        ImVec2 a(c.x - l.btn_r, c.y - l.btn_r), z(c.x + l.btn_r, c.y + l.btn_r);
        if (g_tex[b]) dl->AddImage(g_tex[b], a, z, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, at(255)));
        else {
            dl->AddCircleFilled(c, l.btn_r, fill, 32);
            dl->AddCircle(c, l.btn_r, line, 32, 2.0f);
        }
    }

    // the arrows: four round buttons apart, each with its arrow, lit while held
    {
        uint32_t held = g_buttons.load();
        const float dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int a = 0; a < 4; a++) {
            ImVec2 c(l.arrow[a][0], l.arrow[a][1]);
            float r = l.arrow_r;
            bool on = held & kArrowBits[a];
            dl->AddCircleFilled(c, r, on ? IM_COL32(255, 255, 255, at(90)) : fill, 32);
            dl->AddCircle(c, r, line, 32, 2.0f);
            // the arrow head pointing away from the middle
            float dx = dirs[a][0], dy = dirs[a][1], t = r * 0.42f;
            ImVec2 tip(c.x + dx * t, c.y + dy * t);
            ImVec2 b1(c.x - dx * t * 0.55f - dy * t, c.y - dy * t * 0.55f + dx * t);
            ImVec2 b2(c.x - dx * t * 0.55f + dy * t, c.y - dy * t * 0.55f - dx * t);
            dl->AddTriangleFilled(tip, b1, b2, label);
        }
    }

    // START, BACK and the two triggers
    static const struct {
        int button;
        const char* text;
    } kPills[] = {{kBtnBack, "BACK"}, {kBtnStart, "START"}, {kBtnLT, "LT"}, {kBtnRT, "RT"},
                  {kBtnL3, "L3"},     {kBtnR3, "R3"}};
    for (int i = 0; i < 6; i++) {
        const auto& p = kPills[i];
        ImVec2 c(l.pill[p.button][0], l.pill[p.button][1]);
        dl->AddRectFilled(ImVec2(c.x - l.pill_w / 2, c.y - l.pill_h / 2), ImVec2(c.x + l.pill_w / 2, c.y + l.pill_h / 2),
                          IM_COL32(20, 20, 20, at(150)), l.pill_h / 2);
        dl->AddRect(ImVec2(c.x - l.pill_w / 2, c.y - l.pill_h / 2), ImVec2(c.x + l.pill_w / 2, c.y + l.pill_h / 2),
                    line, l.pill_h / 2, 0, 1.5f);
        // in the game's style the pill shows what it does, the letters standing for nothing there
        if (ArtStyle() && g_pill_tex[i]) {
            float s = l.pill_h * 0.95f;
            dl->AddImage(g_pill_tex[i], ImVec2(c.x - s / 2, c.y - s / 2), ImVec2(c.x + s / 2, c.y + s / 2), ImVec2(0, 0),
                         ImVec2(1, 1), IM_COL32(255, 255, 255, at(235)));
            continue;
        }
        float size = l.pill_h * 0.46f;
        ImVec2 t = font->CalcTextSizeA(size, FLT_MAX, 0, p.text);
        dl->AddText(font, size, ImVec2(c.x - t.x / 2, c.y - t.y / 2), label, p.text);
    }
    if (g_editing.load(std::memory_order_relaxed)) DrawEditor(l, dl);
}

bool Editing() { return g_editing.load(std::memory_order_relaxed); }

void EditBegin() {
    {
        std::lock_guard lock(g_lock);
        ReleaseAll();
    }
    g_editing = true;
}

void EditEnd() { EndEditing(); }

void EditReset() { ResetUnits(); }

// the layout as [[controls]] in the settings file, one entry per unit in the order above
void LoadLayout(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::array* a = t["controls"].as_array();
    if (!a) return;
    for (size_t i = 0; i < a->size() && i < size_t(kUnitCount); i++) {
        const toml::table* e = (*a)[i].as_table();
        if (!e) continue;
        g_units[i].x = float((*e)["x"].value_or(double(kDefaultUnits[i].x)));
        g_units[i].y = float((*e)["y"].value_or(double(kDefaultUnits[i].y)));
    }
}

void SaveLayout(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    toml::array a;
    for (int i = 0; i < kUnitCount; i++)
        a.push_back(toml::table{{"x", double(g_units[size_t(i)].x)}, {"y", double(g_units[size_t(i)].y)}});
    t.insert_or_assign("controls", std::move(a));
}

}  // namespace touch
