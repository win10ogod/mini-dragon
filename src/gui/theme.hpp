#pragma once
#include <imgui.h>

namespace minidragon {

inline void apply_scifi_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    // Main background - deep dark blue
    c[ImGuiCol_WindowBg]           = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.04f, 0.04f, 0.08f, 1.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.06f, 0.06f, 0.12f, 0.95f);

    // Borders - subtle cyan glow
    c[ImGuiCol_Border]             = ImVec4(0.00f, 0.50f, 0.60f, 0.30f);
    c[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame background
    c[ImGuiCol_FrameBg]            = ImVec4(0.08f, 0.08f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.10f, 0.15f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);

    // Title bar
    c[ImGuiCol_TitleBg]            = ImVec4(0.04f, 0.04f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.00f, 0.20f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.02f, 0.02f, 0.05f, 0.50f);

    // Menu & scrollbar
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.04f, 0.04f, 0.08f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.00f, 0.40f, 0.50f, 0.50f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.55f, 0.65f, 0.70f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.00f, 0.70f, 0.80f, 0.90f);

    // Buttons - cyan accent
    c[ImGuiCol_Button]             = ImVec4(0.00f, 0.35f, 0.45f, 0.60f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.00f, 0.50f, 0.60f, 0.80f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.00f, 0.60f, 0.70f, 1.00f);

    // Headers
    c[ImGuiCol_Header]             = ImVec4(0.00f, 0.30f, 0.40f, 0.40f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.00f, 0.45f, 0.55f, 0.50f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.00f, 0.55f, 0.65f, 0.60f);

    // Separator
    c[ImGuiCol_Separator]          = ImVec4(0.00f, 0.40f, 0.50f, 0.30f);
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.00f, 0.55f, 0.65f, 0.60f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.00f, 0.70f, 0.80f, 0.90f);

    // Tab
    c[ImGuiCol_Tab]                = ImVec4(0.04f, 0.04f, 0.08f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.00f, 0.45f, 0.55f, 0.80f);

    // Text
    c[ImGuiCol_Text]               = ImVec4(0.70f, 0.90f, 0.70f, 1.00f); // Green terminal text
    c[ImGuiCol_TextDisabled]       = ImVec4(0.40f, 0.50f, 0.40f, 1.00f);

    // Check/Slider
    c[ImGuiCol_CheckMark]          = ImVec4(0.00f, 0.80f, 0.90f, 1.00f);
    c[ImGuiCol_SliderGrab]         = ImVec4(0.00f, 0.60f, 0.70f, 1.00f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.00f, 0.80f, 0.90f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.00f, 0.40f, 0.50f, 0.25f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.00f, 0.55f, 0.65f, 0.67f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.00f, 0.70f, 0.80f, 0.95f);

    // Rounded corners for sci-fi feel
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
}

// Color helpers for chat messages
inline ImVec4 color_user()      { return ImVec4(0.00f, 0.85f, 0.95f, 1.00f); } // Cyan
inline ImVec4 color_assistant() { return ImVec4(0.50f, 0.95f, 0.50f, 1.00f); } // Green
inline ImVec4 color_tool()      { return ImVec4(0.95f, 0.85f, 0.30f, 1.00f); } // Yellow
inline ImVec4 color_system()    { return ImVec4(0.60f, 0.60f, 0.70f, 1.00f); } // Gray
inline ImVec4 color_error()     { return ImVec4(0.95f, 0.30f, 0.30f, 1.00f); } // Red

} // namespace minidragon
