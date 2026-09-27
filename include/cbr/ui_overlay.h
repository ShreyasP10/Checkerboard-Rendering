#pragma once

#include <cstdint>

namespace cbr {

class UIOverlay {
public:
    static UIOverlay& Get();

    void Initialize();
    void Shutdown();

    void Render();
    void ToggleVisibility() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }

private:
    UIOverlay() = default;
    ~UIOverlay() = default;

    bool m_visible{ false };
    bool m_initialized{ false };
};

} // namespace cbr
