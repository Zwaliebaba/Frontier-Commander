#pragma once

// The precompiled header of Client: the standard library, then the platform in the order
// AGENTS.md §4 fixes -- Core's WindowsHeader.h, the one owner of the Windows macro family, before
// Direct3D 12 and DXGI; <unknwn.h> before <winrt/base.h>, so that winrt::com_ptr and
// winrt::check_hresult (R12, R14) work on classic COM interfaces; and last the vendored d3dx12.h
// (ADR-004). Every translation unit of Client draws or sits beside one that does, so the platform
// is precompiled once here. A header of Client still includes what it names, because the
// executable's pch.h is the standard library alone (ADR-001).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "WindowsHeader.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <unknwn.h>
#include <winrt/base.h>

// The state-object helpers are the only part of d3dx12.h that includes WRL's ComPtr, which R14
// excludes, and the feature-support class is the part that tracks the newest SDK; without both the
// header needs the SDK's d3d12.h and the standard library alone, and neither is used here.
#define D3DX12_NO_STATE_OBJECT_HELPERS
#define D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#include "d3dx12.h"

#include "Assertion.h"
