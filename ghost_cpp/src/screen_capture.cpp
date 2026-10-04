#include "screen_capture.hpp"
#include "crypto.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <iostream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace {
    ULONG_PTR g_gdiplusToken = 0;

    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        UINT num = 0;
        UINT size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;

        std::vector<uint8_t> buffer(size);
        Gdiplus::ImageCodecInfo* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
        Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
                *pClsid = pImageCodecInfo[j].Clsid;
                return j;
            }
        }
        return -1;
    }
}

namespace ScreenCapture {

void initialize() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
}

void shutdown() {
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

std::string captureToBase64Jpeg(int x, int y, int width, int height, int quality) {
    if (width <= 0 || height <= 0) {
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY | CAPTUREBLT);

    std::string base64Result;
    {
        Gdiplus::Bitmap gdiBitmap(hBitmap, NULL);
        CLSID clsidJpeg;
        if (GetEncoderClsid(L"image/jpeg", &clsidJpeg) >= 0) {
            Gdiplus::EncoderParameters encoderParameters;
            encoderParameters.Count = 1;
            encoderParameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
            encoderParameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
            encoderParameters.Parameter[0].NumberOfValues = 1;
            ULONG q = static_cast<ULONG>(quality);
            encoderParameters.Parameter[0].Value = &q;

            IStream* pStream = NULL;
            if (SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &pStream))) {
                if (gdiBitmap.Save(pStream, &clsidJpeg, &encoderParameters) == Gdiplus::Ok) {
                    STATSTG stat;
                    if (SUCCEEDED(pStream->Stat(&stat, STATFLAG_NONAME))) {
                        ULONG streamSize = (ULONG)stat.cbSize.QuadPart;
                        std::vector<uint8_t> buffer(streamSize);

                        LARGE_INTEGER liZero;
                        liZero.QuadPart = 0;
                        pStream->Seek(liZero, STREAM_SEEK_SET, NULL);

                        ULONG bytesRead = 0;
                        pStream->Read(buffer.data(), streamSize, &bytesRead);
                        base64Result = Crypto::base64Encode(buffer.data(), bytesRead);
                    }
                }
                pStream->Release();
            }
        }
    }

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    return base64Result;
}

} // namespace ScreenCapture
