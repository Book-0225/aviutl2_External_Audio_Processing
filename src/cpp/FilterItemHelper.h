#pragma once
#include "filter2.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace FilterItemHelper {

struct HideCond {
    LPCWSTR name;
    FILTER_ITEM_HIDE_RULE::OPERATOR op;
    int32_t value;
};

inline FILTER_ITEM_HIDE_RULE MakeHideRule(LPCWSTR target, const HideCond& c) {
    return FILTER_ITEM_HIDE_RULE(target, c.name, c.op, c.value);
}

template <class T>
struct WithHide : T {
    FILTER_ITEM_HIDE_RULE hide;

    template <class... Args>
    explicit WithHide(const HideCond& cond, Args&&... args)
        : T(std::forward<Args>(args)...),
          hide(MakeHideRule(this->name, cond)) {}
};

class ItemList {
  public:
    template <class... Ts>
    ItemList& Add(Ts&&... items) {
        static_assert(((std::is_lvalue_reference_v<Ts> || std::is_pointer_v<std::remove_reference_t<Ts>>) && ...), "ItemList::Add: pass an lvalue item or a pointer to one (temporaries are not allowed)");

        (AddOne(items), ...);
        return *this;
    }

    std::vector<void*> Build() const {
        std::vector<void*> r = items_;
        r.push_back(nullptr);
        return r;
    }

  private:
    template <class T>
    void AddOne(T& item) {
        if constexpr (std::is_pointer_v<T>) {
            if (item) AddOne(*item);
        } else {
            items_.push_back(&item);
        }
    }

    template <class T>
    void AddOne(WithHide<T>& item) {
        items_.push_back(static_cast<T*>(&item));
        items_.push_back(&item.hide);
    }

    std::vector<void*> items_;
};

struct NumberedName {
    std::wstring s;

    explicit NumberedName(std::wstring_view fmt, int32_t n)
        : s(std::vformat(fmt, std::make_wformat_args(n))) {}
};

} // namespace FilterItemHelper