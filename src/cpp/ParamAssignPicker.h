#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

struct ParamAssignCandidate {
    int32_t index;
    std::wstring name;
};

int32_t ShowParamAssignPicker(
    HINSTANCE hInstance,
    HWND parentWindow,
    const std::wstring& dialogTitle,
    const std::vector<ParamAssignCandidate>& candidates);
