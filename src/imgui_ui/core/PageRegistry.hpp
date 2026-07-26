#pragma once
#include <vector>
#include <memory>
#include "IPage.hpp"

class PageRegistry {
public:
    static PageRegistry& instance() {
        static PageRegistry s;
        return s;
    }

    void add(std::unique_ptr<IPage> page) {
        pages_.push_back(std::move(page));
    }

    IPage* get(UiPage id) const {
        for (auto& p : pages_)
            if (p->id() == id) return p.get();
        return nullptr;
    }

    const std::vector<std::unique_ptr<IPage>>& all() const { return pages_; }

    PageRegistry(const PageRegistry&) = delete;
    PageRegistry& operator=(const PageRegistry&) = delete;

private:
    PageRegistry() = default;
    std::vector<std::unique_ptr<IPage>> pages_;
};
