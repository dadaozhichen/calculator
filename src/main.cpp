// ═══════════════════════════════════════════════════════════════════════════════
//  C++20 Calculator — ImGui + SDL3 + OpenGL 3.3
//  Features: RAII resource guards, std::optional state machine,
//            std::to_chars formatting, C++20 ranges, proper error handling
// ═══════════════════════════════════════════════════════════════════════════════

#include <imgui.h>
#include <imgui_stdlib.h>

// Backend headers are bundled in src/ (xmake's imgui package ships core only).
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#ifdef __APPLE__
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
//  RAII guards  —  each owns exactly one resource
// ─────────────────────────────────────────────────────────────────────────────

/// SDL library init / quit.
struct SdlContext {
    SdlContext()  { if (!SDL_Init(SDL_INIT_VIDEO))  throw std::runtime_error{SDL_GetError()}; }
    ~SdlContext() { SDL_Quit(); }
    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;
};

/// Owns an SDL_Window and its OpenGL 3.3 core-profile context.
struct GlWindow {
    SDL_Window*   window = nullptr;
    SDL_GLContext gl_ctx = nullptr;

    GlWindow(const char* title, int w, int h, SDL_WindowFlags flags) {
        // Request OpenGL 3.3 core BEFORE creating the window.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        window = SDL_CreateWindow(title, w, h, flags | SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error{SDL_GetError()};

        gl_ctx = SDL_GL_CreateContext(window);
        if (!gl_ctx) {
            SDL_DestroyWindow(window);
            throw std::runtime_error{SDL_GetError()};
        }
    }

    ~GlWindow() { release(); }

    GlWindow(const GlWindow&) = delete;
    GlWindow& operator=(const GlWindow&) = delete;

    GlWindow(GlWindow&& other) noexcept
        : window{std::exchange(other.window, nullptr)}
        , gl_ctx{std::exchange(other.gl_ctx, nullptr)} {}

    GlWindow& operator=(GlWindow&& other) noexcept {
        if (this != &other) { release(); std::swap(window, other.window); std::swap(gl_ctx, other.gl_ctx); }
        return *this;
    }

    void swap() noexcept { SDL_GL_SwapWindow(window); }

private:
    void release() noexcept {
        if (gl_ctx)  { SDL_GL_DestroyContext(gl_ctx); gl_ctx = nullptr; }
        if (window)  { SDL_DestroyWindow(window);    window = nullptr; }
    }
};

/// ImGui context, platform back-end (SDL3) and renderer back-end (OpenGL3).
struct ImGuiContext {
    ImGuiContext(SDL_Window* w, SDL_GLContext g) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename  = nullptr;   // don't persist window layout

        ImGui::StyleColorsDark();

        if (!ImGui_ImplSDL3_InitForOpenGL(w, g))
            throw std::runtime_error{"ImGui_ImplSDL3_InitForOpenGL failed"};
        if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
            throw std::runtime_error{"ImGui_ImplOpenGL3_Init failed"};
    }

    ~ImGuiContext() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    ImGuiContext(const ImGuiContext&) = delete;
    ImGuiContext& operator=(const ImGuiContext&) = delete;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Calculator  —  pure business logic, no rendering concerns
// ─────────────────────────────────────────────────────────────────────────────

class Calculator {
public:
    Calculator() = default;

    // ------------------------------------------------------------------
    //  Public API
    // ------------------------------------------------------------------

    /// Append a digit or decimal point to the current value.
    void process_number(std::string_view digit) {
        if (state_.new_entry) {
            state_.current  = digit;
            state_.new_entry = false;
        } else if (digit == "." && state_.current.find('.') != std::string::npos) {
            return;                              // already has a decimal point
        } else if (state_.current == "0" && digit != ".") {
            state_.current = digit;              // replace leading zero
        } else {
            state_.current += digit;
        }
    }

    /// Store operator for a pending binary operation (evaluates any previous one first).
    void process_operator(char op) {
        if (state_.pending_op) evaluate();
        state_.previous   = state_.current;
        state_.pending_op = op;
        state_.new_entry  = true;
    }

    /// Evaluate the pending binary operation, if any.
    void evaluate() {
        if (!state_.pending_op || state_.previous.empty()) return;

        auto const a = parse_double(state_.previous);
        auto const b = parse_double(state_.current);
        if (!a || !b)  return set_error();

        std::optional<double> result;

        switch (*state_.pending_op) {
            case '+': result = *a + *b;            break;
            case '-': result = *a - *b;            break;
            case '*': result = *a * *b;            break;
            case '/':
                if (std::abs(*b) < std::numeric_limits<double>::epsilon())
                    return set_error();            // division by ~zero
                result = *a / *b;
                break;
            case '%':
                if (std::abs(*b) < std::numeric_limits<double>::epsilon())
                    return set_error();            // modulo by ~zero
                result = std::fmod(*a, *b);
                break;
            default:
                return set_error();                // unknown operator
        }

        if (!std::isfinite(*result))               // overflow, inf, nan
            return set_error();

        state_.current    = fmt_double(*result);
        state_.previous.clear();
        state_.pending_op.reset();
        state_.new_entry  = true;
        state_.error     = false;
    }

    /// Full reset.
    void clear() noexcept { state_ = State{}; }

    /// Remove the last digit of the current value.
    void backspace() noexcept {
        if (state_.new_entry || state_.current.empty()) return;
        state_.current.pop_back();
        if (state_.current.empty() || state_.current == "-") {
            state_.current = "0";
            state_.new_entry = true;
        }
    }

    /// Toggle sign of the current value.
    void negate() {
        auto val = parse_double(state_.current);
        if (!val) return;
        state_.current = fmt_double(-*val);
        state_.new_entry = true;
    }

    // ------------------------------------------------------------------
    //  Read-only accessors
    // ------------------------------------------------------------------

    [[nodiscard]] const std::string& display() const noexcept { return state_.current; }
    [[nodiscard]] bool error()                        const noexcept { return state_.error; }

private:
    struct State {
        std::string          current = "0";
        std::string          previous;
        std::optional<char>  pending_op;
        bool                 new_entry = true;
        bool                 error     = false;
    };

    State state_;

    // ------------------------------------------------------------------
    //  Helpers
    // ------------------------------------------------------------------

    [[nodiscard]] static std::optional<double> parse_double(std::string_view s) noexcept {
        if (s.empty() || s == "Error") return std::nullopt;
        try {
            return std::stod(std::string{s});
        } catch (...) {
            return std::nullopt;
        }
    }

    void set_error() noexcept {
        state_.current    = "Error";
        state_.error      = true;
        state_.new_entry  = true;
        state_.pending_op.reset();
        state_.previous.clear();
    }

    /// Format a double as a short, human-friendly string.
    ///
    /// Uses std::to_chars (C++17 for floating-point) for locale-independent
    /// fixed-point output, then trims trailing zeros via C++20 ranges.
    [[nodiscard]] static std::string fmt_double(double value) noexcept {
        std::array<char, 64> buf{};
        auto [end, ec] = std::to_chars(
            buf.data(), buf.data() + buf.size(),
            value, std::chars_format::fixed, 10
        );

        if (ec != std::errc{}) {
            // Fallback — rare path (shouldn't happen with a 64-char buffer).
            auto s = std::to_string(value);
            if (auto dot = s.find('.'); dot != std::string::npos)
                s.erase(s.find_last_not_of('0') + 1);
            if (s.ends_with('.')) s.pop_back();
            return s;
        }

        std::string result{buf.data(), end};

        // Trim trailing zeros from the fractional part.
        if (auto dot = result.find('.'); dot != std::string::npos) {
            auto const last = std::ranges::find_if_not(
                result | std::views::reverse,
                [](char c) noexcept { return c == '0'; }
            ).base();
            result.erase(last, result.end());
            if (result.ends_with('.')) result.pop_back();
        }

        return result;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Entry-point
// ─────────────────────────────────────────────────────────────────────────────

int main() try
{
    SdlContext  sdl;
    GlWindow   win{"C++20 Calculator", 400, 550, SDL_WINDOW_RESIZABLE};
    ImGuiContext ig{win.window, win.gl_ctx};

    Calculator calc;
    bool running = true;

    // ── Layout ratios (relative to window size) ──────────────────────────
    auto constexpr PAD       = 12.0f;         // window inner padding
    auto constexpr GAP       =  6.0f;         // between buttons
    auto constexpr BTN_COLS  = 4;
    auto constexpr DSP_RATIO = 0.18f;         // display height / total height
    auto constexpr BTN_RATIO = 0.15f;         // button height / content area height

    // ── Event / render loop ────────────────────────────────────────────────

    while (running) {
        // ── Event dispatch ──────────────────────────────────────────────────
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);

            if (ev.type == SDL_EVENT_QUIT) { running = false; continue; }

            if (ev.type == SDL_EVENT_KEY_DOWN && !ImGui::GetIO().WantCaptureKeyboard) {
                // Handle digit keys (0-9) first.
                if (ev.key.key >= SDLK_0 && ev.key.key <= SDLK_9) {
                    char d = static_cast<char>('0' + (ev.key.key - SDLK_0));
                    calc.process_number(std::string_view{&d, 1});
                } else switch (ev.key.key) {
                    case SDLK_ESCAPE:    running = false;      break;
                    case SDLK_C:         calc.clear();         break;
                    case SDLK_PERIOD:    calc.process_number(".");  break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:  calc.evaluate();           break;
                    case SDLK_BACKSPACE: calc.backspace();          break;
                    case SDLK_KP_PLUS:   calc.process_operator('+'); break;
                    case SDLK_KP_MINUS:  calc.process_operator('-'); break;
                    case SDLK_KP_MULTIPLY: calc.process_operator('*'); break;
                    case SDLK_KP_DIVIDE: calc.process_operator('/');  break;
                    case SDLK_N:
                        if (ev.key.mod & SDL_KMOD_SHIFT) calc.negate();
                        break;
                    default: break;
                }
            }
        }

        // ── ImGui frame ────────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ── Responsive layout — everything computed from current window size
        auto const win_w = ImGui::GetIO().DisplaySize.x;
        auto const win_h = ImGui::GetIO().DisplaySize.y;
        auto const dsp_h = std::max(60.0f, win_h * DSP_RATIO);
        auto const btn_h = std::max(40.0f, (win_h - dsp_h) * BTN_RATIO);
        auto const btn_w = (win_w - 2.0f * PAD - (BTN_COLS - 1.0f) * GAP)
                           / static_cast<float>(BTN_COLS);

        // Font scale for buttons: 1.0 at 400px window width.
        auto const font_scale = std::clamp(win_w / 400.0f, 0.45f, 3.0f);

        ImGui::SetNextWindowPos(ImVec2{0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{win_w, win_h}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{PAD, PAD});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2{GAP, GAP});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

        ImGui::Begin("Calculator", nullptr,
                     ImGuiWindowFlags_NoTitleBar     |
                     ImGuiWindowFlags_NoMove         |
                     ImGuiWindowFlags_NoCollapse     |
                     ImGuiWindowFlags_NoScrollbar    |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::SetWindowFontScale(font_scale);

        // ── Display ────────────────────────────────────────────────────────
        // Font size is derived from display height so the text always fits.
        {
            float const dsp_avail = dsp_h - ImGui::GetStyle().WindowPadding.y * 2.0f;
            float const dsp_scale = std::max(1.0f, dsp_avail * 0.60f)
                                    / ImGui::GetFontSize();

            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                  ImVec4{0.12f, 0.12f, 0.16f, 1.0f});
            ImGui::BeginChild("display", ImVec2{0, dsp_h},
                              ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AlwaysUseWindowPadding);

            auto const& text = calc.display();
            auto const  fg   = calc.error()
                                   ? ImVec4{1.0f, 0.30f, 0.30f, 1.0f}
                                   : ImVec4{0.92f, 0.92f, 1.00f, 1.0f};

            float avail_w = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Text, fg);
            ImGui::SetWindowFontScale(dsp_scale);
            float txt_w = ImGui::CalcTextSize(text.c_str()).x;
            float txt_h = ImGui::GetFontSize();
            ImGui::SetCursorPosY((dsp_h - txt_h) / 2.0f);
            ImGui::SetCursorPosX((std::max)(avail_w - txt_w - PAD, 0.0f));
            ImGui::TextUnformatted(text.c_str());
            ImGui::SetWindowFontScale(font_scale);
            ImGui::PopStyleColor(2);

            ImGui::EndChild();
        }

        // ── Button grid ────────────────────────────────────────────────────

        auto btn = [&](const char* label, auto&& action) {
            if (ImGui::Button(label, ImVec2{btn_w, btn_h})) action();
        };
        auto btn_wide = [&](const char* label, auto&& action, float extra_w) {
            if (ImGui::Button(label, ImVec2{btn_w + extra_w, btn_h})) action();
        };

        int col = 0;
        auto next = [&] {
            ++col;
            if (col % BTN_COLS != 0) ImGui::SameLine(0.0f, GAP);
        };
        auto newline = [&] { col = 0; };

        // Row 0 :  C      ±      %       ÷
        btn("C",  [&]{ calc.clear(); });                    next();
        btn("±",  [&]{ calc.negate(); });                   next();
        btn("%",  [&]{ calc.process_operator('%'); });      next();
        btn("÷",  [&]{ calc.process_operator('/'); });      newline();

        // Row 1 :  7      8      9       ×
        btn("7",  [&]{ calc.process_number("7"); });        next();
        btn("8",  [&]{ calc.process_number("8"); });        next();
        btn("9",  [&]{ calc.process_number("9"); });        next();
        btn("×",  [&]{ calc.process_operator('*'); });      newline();

        // Row 2 :  4      5      6       −
        btn("4",  [&]{ calc.process_number("4"); });        next();
        btn("5",  [&]{ calc.process_number("5"); });        next();
        btn("6",  [&]{ calc.process_number("6"); });        next();
        btn("−",  [&]{ calc.process_operator('-'); });      newline();

        // Row 3 :  1      2      3       +
        btn("1",  [&]{ calc.process_number("1"); });        next();
        btn("2",  [&]{ calc.process_number("2"); });        next();
        btn("3",  [&]{ calc.process_number("3"); });        next();
        btn("+",  [&]{ calc.process_operator('+'); });      newline();

        // Row 4 :  0 (span-2)      .       =
        btn_wide("0",  [&]{ calc.process_number("0"); },
                       btn_w + GAP);                        next();
        btn(".",  [&]{ calc.process_number("."); });        next();
        btn("=",  [&]{ calc.evaluate(); });

        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();   // Calculator window
        ImGui::PopStyleVar(3);

        // ── Render ─────────────────────────────────────────────────────────
        ImGui::Render();
        {
            auto const& io = ImGui::GetIO();
            glViewport(0, 0,
                       static_cast<GLint>(io.DisplaySize.x),
                       static_cast<GLint>(io.DisplaySize.y));
            glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        win.swap();
    }

    return 0;
}
catch (std::exception const& e) {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
    return 1;
}
catch (...) {
    std::fputs("Fatal: unknown error\n", stderr);
    return 1;
}
