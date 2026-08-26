#pragma once

#include "IAudioPluginHost.h"

#include <string>
#include <vector>
#include <windows.h>

std::string ShowSubPluginPicker(
    HINSTANCE hInstance,
    HWND parentWindow,
    const std::wstring& dialogTitle,
    const std::vector<IAudioPluginHost::SubPluginInfo>& candidates);
