// D3D12 Device Removed Extended Data (DRED) without the debug layer.
//
// Three runs on 2026-10-19 lost the D3D12 device ("DEVICE_RESET (bad GPU command)", no driver
// timeout in the system log), each at a different point of start-up and each after creating GPU
// resources started failing. The SDK turns DRED on only together with the slow debug layer
// (d3d12_debug); its device removal handler then logs the breadcrumbs (the last commands the GPU
// finished) and the page fault address. DRED alone costs little, so it can stay on for testing:
// its settings only have to be made before the device is created.

#include "dev/gpu_diagnostics.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#include <d3d12.h>
#endif

REXCVAR_DEFINE_BOOL(gpu_crash_diagnostics, false, "GPU",
                    "Record what the GPU did last (D3D12 DRED), so a lost graphics device "
                    "logs the command and address that caused it. Small cost; for testing")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace skylanders::dev {

void EnableGpuCrashDiagnostics() {
#if defined(_WIN32)
  if (!REXCVAR_GET(gpu_crash_diagnostics)) return;
  HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
  if (!d3d12) return;
  auto get_debug_interface = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(
      GetProcAddress(d3d12, "D3D12GetDebugInterface"));
  ID3D12DeviceRemovedExtendedDataSettings* settings = nullptr;
  if (get_debug_interface && SUCCEEDED(get_debug_interface(IID_PPV_ARGS(&settings)))) {
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->Release();
    REXLOG_INFO("GPU crash diagnostics: D3D12 DRED on (breadcrumbs, page faults)");
  } else {
    REXLOG_WARN("GPU crash diagnostics: D3D12 DRED is not available");
  }
  // d3d12.dll stays loaded; the GPU plugin loads it again for the device.
#endif
}

}  // namespace skylanders::dev
