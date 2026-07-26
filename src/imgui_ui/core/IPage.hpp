#pragma once
#include "AppState.hpp"

class IPage {
public:
    virtual ~IPage() = default;
    virtual UiPage id() const = 0;
    virtual const char* label() const = 0;
    virtual void draw(AppState& state) = 0;
};
