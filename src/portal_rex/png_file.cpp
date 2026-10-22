#include "portal_rex/png_file.h"

#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#endif

namespace skylanders {

bool WritePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba) {
#ifdef _WIN32
  if (width <= 0 || height <= 0 || !rgba) return false;
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = false;
  std::filesystem::path temp = path;
  temp += L".part";
  IWICImagingFactory* factory = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  // WIC takes BGRA.
  std::vector<uint8_t> bgra(rgba, rgba + size_t(width) * height * 4);
  for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateStream(&stream)) &&
      SUCCEEDED(stream->InitializeFromFilename(temp.c_str(), GENERIC_WRITE)) &&
      SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
      SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
      SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
      SUCCEEDED(frame->SetSize(UINT(width), UINT(height)))) {
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    ok = SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA &&
         SUCCEEDED(frame->WritePixels(UINT(height), UINT(width) * 4, UINT(bgra.size()), bgra.data())) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
  }
  if (frame) frame->Release();
  if (encoder) encoder->Release();
  if (stream) stream->Release();
  if (factory) factory->Release();
  if (SUCCEEDED(init)) CoUninitialize();
  std::error_code ec;
  if (ok) {
    std::filesystem::rename(temp, path, ec);
    ok = !ec;
  }
  if (!ok) std::filesystem::remove(temp, ec);
  return ok;
#else
  (void)path;
  (void)width;
  (void)height;
  (void)rgba;
  return false;
#endif
}

}  // namespace skylanders
