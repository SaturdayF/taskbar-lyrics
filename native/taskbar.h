#pragma once
#include <UIAutomation.h>
#include "layout.h"
struct BarSnapshot {
    HWND handle = nullptr;
    RECT bounds{};
    std::vector<layout::Span> occupied;
    bool reliable = false;
    std::chrono::steady_clock::time_point sampled{};
};
inline BarSnapshot inspectBar(IUIAutomation* automation, HWND handle) {
    BarSnapshot result;
    result.handle = handle; result.sampled = std::chrono::steady_clock::now();
    if (!IsWindowVisible(handle) || !GetWindowRect(handle, &result.bounds)) return result;
    winrt::com_ptr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(handle, root.put()))) return result;
    std::vector<winrt::com_ptr<IUIAutomationCondition>> conditions;
    std::vector<IUIAutomationCondition*> pointers;
    for (int type : {UIA_ButtonControlTypeId, UIA_MenuItemControlTypeId, UIA_TabItemControlTypeId, UIA_ListItemControlTypeId,
        UIA_CheckBoxControlTypeId, UIA_RadioButtonControlTypeId, UIA_SliderControlTypeId, UIA_HyperlinkControlTypeId}) {
        VARIANT value{}; value.vt = VT_I4; value.lVal = type;
        winrt::com_ptr<IUIAutomationCondition> condition;
        if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, condition.put()))) return result;
        pointers.push_back(condition.get()); conditions.push_back(std::move(condition));
    }
    winrt::com_ptr<IUIAutomationCondition> combined;
    if (FAILED(automation->CreateOrConditionFromNativeArray(pointers.data(), static_cast<int>(pointers.size()), combined.put()))) return result;
    winrt::com_ptr<IUIAutomationCacheRequest> cache;
    if (FAILED(automation->CreateCacheRequest(cache.put()))) return result;
    cache->AddProperty(UIA_BoundingRectanglePropertyId); cache->AddProperty(UIA_IsOffscreenPropertyId);
    cache->put_AutomationElementMode(AutomationElementMode_None);
    winrt::com_ptr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, combined.get(), cache.get(), elements.put()))) return result;
    int length = 0; elements->get_Length(&length);
    for (int i = 0; i < length; ++i) {
        winrt::com_ptr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, element.put()))) continue;
        BOOL offscreen = TRUE; RECT bounds{};
        if (FAILED(element->get_CachedIsOffscreen(&offscreen)) || offscreen || FAILED(element->get_CachedBoundingRectangle(&bounds))) continue;
        RECT intersection{};
        if (IntersectRect(&intersection, &bounds, &result.bounds)) result.occupied.push_back({bounds.left, bounds.right});
    }
    EnumChildWindows(handle, [](HWND child, LPARAM parameter) -> BOOL {
        auto& bar = *reinterpret_cast<BarSnapshot*>(parameter);
        wchar_t name[128]{}; GetClassNameW(child, name, 128);
        if (wcscmp(name, L"TrayNotifyWnd") == 0 || wcscmp(name, L"TrayClockWClass") == 0 || wcscmp(name, L"TrayShowDesktopButtonWClass") == 0) {
            RECT bounds{}, intersection{};
            if (IsWindowVisible(child) && GetWindowRect(child, &bounds) && IntersectRect(&intersection, &bounds, &bar.bounds)) bar.occupied.push_back({bounds.left, bounds.right});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    result.reliable = !result.occupied.empty();
    return result;
}
