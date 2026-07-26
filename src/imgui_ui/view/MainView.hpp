#pragma once
#include "core/AppState.hpp"

class MainView {
public:
    void draw(AppState& state);

private:
    void draw_sidebar(AppState& state);
};
