#include "portal_rex/png_file.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#else
#include <zlib.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

namespace skylanders {

namespace {

// A random, unpredictable sibling name next to `path` for the temporary file: an attacker cannot
// seed a symlink at the name for the write to follow.
std::filesystem::path PngTempPath(const std::filesystem::path& path) {
  std::random_device rd;
  std::seed_seq seed{rd(), rd(), rd(), rd()};
  std::mt19937_64 rng(seed);
  char name[48];
  std::snprintf(name, sizeof(name), "png-%016llx%016llx.tmp",
                static_cast<unsigned long long>(rng()), static_cast<unsigned long long>(rng()));
  return path.parent_path() / std::filesystem::path(name);
}

}  // namespace

#ifndef _WIN32
namespace {

constexpr int kMaxPngDimension = 4096;

void AppendBe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(uint8_t((value >> 24) & 0xFF));
  out.push_back(uint8_t((value >> 16) & 0xFF));
  out.push_back(uint8_t((value >> 8) & 0xFF));
  out.push_back(uint8_t(value & 0xFF));
}

bool WritePngChunk(std::ofstream& out, const char tag[4], const uint8_t* data, size_t size) {
  std::vector<uint8_t> chunk;
  chunk.reserve(4 + 4 + size + 4);
  AppendBe32(chunk, uint32_t(size));
  chunk.insert(chunk.end(), reinterpret_cast<const uint8_t*>(tag),
               reinterpret_cast<const uint8_t*>(tag) + 4);
  if (size > 0) chunk.insert(chunk.end(), data, data + size);
  const uLong crc = crc32(0L, chunk.data() + 4, uInt(4 + size));
  AppendBe32(chunk, uint32_t(crc));
  out.write(reinterpret_cast<const char*>(chunk.data()), std::streamsize(chunk.size()));
  return out.good();
}

}  // namespace
#endif

bool WritePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba) {
#ifdef _WIN32
  if (width <= 0 || height <= 0 || !rgba) return false;
  const std::filesystem::path temp = PngTempPath(path);
  // Create the unpredictable sibling exclusively so a planted symlink at the name cannot be
  // followed; WIC then writes this file.
  const HANDLE reserved =
      CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (reserved == INVALID_HANDLE_VALUE) return false;
  CloseHandle(reserved);
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = false;
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
  if (width <= 0 || height <= 0 || width > kMaxPngDimension || height > kMaxPngDimension || !rgba) {
    return false;
  }
  const size_t stride = size_t(width) * 4;
  std::vector<uint8_t> raw(size_t(height) * (1 + stride));
  for (int y = 0; y < height; ++y) {
    uint8_t* dst = raw.data() + size_t(y) * (1 + stride);
    dst[0] = 0;  // filter type: None
    std::memcpy(dst + 1, rgba + size_t(y) * stride, stride);
  }
  uLongf comp_size = compressBound(uLong(raw.size()));
  std::vector<uint8_t> compressed(comp_size);
  if (compress2(compressed.data(), &comp_size, raw.data(), uLong(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
    return false;
  }

  const std::filesystem::path temp = PngTempPath(path);
  bool ok = false;
  {
    std::ofstream out(temp, std::ios::binary | std::ios::noreplace);
    if (out) {
      static constexpr uint8_t kSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
      out.write(reinterpret_cast<const char*>(kSignature), sizeof(kSignature));
      std::vector<uint8_t> ihdr;
      AppendBe32(ihdr, uint32_t(width));
      AppendBe32(ihdr, uint32_t(height));
      ihdr.push_back(8);  // 8 bits per channel
      ihdr.push_back(6);  // color type: RGBA
      ihdr.push_back(0);  // compression method: deflate
      ihdr.push_back(0);  // filter method: adaptive
      ihdr.push_back(0);  // interlace: none
      ok = out.good() && WritePngChunk(out, "IHDR", ihdr.data(), ihdr.size()) &&
           WritePngChunk(out, "IDAT", compressed.data(), comp_size) &&
           WritePngChunk(out, "IEND", nullptr, 0);
      out.flush();
      ok = ok && out.good();
    }
  }
  std::error_code ec;
  if (ok) {
    std::filesystem::rename(temp, path, ec);
    ok = !ec;
  }
  if (!ok) std::filesystem::remove(temp, ec);
  return ok;
#endif
}

bool ReadPng(const std::filesystem::path& path, int& width, int& height, std::vector<uint8_t>& rgba) {
#ifdef _WIN32
  // On a thread that has COM already (the UI thread), this returns RPC_E_CHANGED_MODE and COM
  // is usable as it is.
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = false;
  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  UINT w = 0, h = 0;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnDemand, &decoder)) &&
      SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom)) &&
      SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
    rgba.resize(size_t(w) * h * 4);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, UINT(rgba.size()), rgba.data()));
  }
  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  if (factory) factory->Release();
  if (SUCCEEDED(init)) CoUninitialize();
  width = ok ? int(w) : 0;
  height = ok ? int(h) : 0;
  if (!ok) rgba.clear();
  return ok;
#else
  width = height = 0;
  rgba.clear();
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const std::streamoff size = in.tellg();
  if (size <= 0 || size > 64 * 1024 * 1024) return false;
  std::vector<uint8_t> encoded(static_cast<size_t>(size));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(encoded.data()), size)) return false;

  int w = 0, h = 0, channels = 0;
  stbi_uc* decoded = stbi_load_from_memory(encoded.data(), int(encoded.size()), &w, &h, &channels, 4);
  if (!decoded) return false;
  const bool ok = w > 0 && h > 0 && w <= kMaxPngDimension && h <= kMaxPngDimension;
  if (ok) {
    width = w;
    height = h;
    rgba.assign(decoded, decoded + size_t(w) * h * 4);
  }
  stbi_image_free(decoded);
  return ok;
#endif
}

bool DecodePng(const uint8_t* data, size_t size, int& width, int& height, std::vector<uint8_t>& rgba) {
  width = height = 0;
  rgba.clear();
  if (!data || size == 0 || size > 64 * 1024 * 1024) return false;
#ifdef _WIN32
  // As ReadPng, from a WIC stream over the bytes instead of a file.
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = false;
  IWICImagingFactory* factory = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  UINT w = 0, h = 0;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateStream(&stream)) &&
      SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(data), DWORD(size))) &&
      SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
      SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom)) &&
      SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
    rgba.resize(size_t(w) * h * 4);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, UINT(rgba.size()), rgba.data()));
  }
  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  if (stream) stream->Release();
  if (factory) factory->Release();
  if (SUCCEEDED(init)) CoUninitialize();
  width = ok ? int(w) : 0;
  height = ok ? int(h) : 0;
  if (!ok) rgba.clear();
  return ok;
#else
  int w = 0, h = 0, channels = 0;
  stbi_uc* decoded = stbi_load_from_memory(data, int(size), &w, &h, &channels, 4);
  if (!decoded) return false;
  const bool ok = w > 0 && h > 0 && w <= kMaxPngDimension && h <= kMaxPngDimension;
  if (ok) {
    width = w;
    height = h;
    rgba.assign(decoded, decoded + size_t(w) * h * 4);
  }
  stbi_image_free(decoded);
  return ok;
#endif
}

}  // namespace skylanders
