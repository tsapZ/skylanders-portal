#pragma once

// GPU crash diagnostics for testing (gpu_crash_diagnostics): D3D12 DRED, see gpu_diagnostics.cpp.

namespace skylanders::dev {

// Call before the graphics device is created (ReXApp::OnPreSetup); does nothing unless
// gpu_crash_diagnostics is on.
void EnableGpuCrashDiagnostics();

}  // namespace skylanders::dev
