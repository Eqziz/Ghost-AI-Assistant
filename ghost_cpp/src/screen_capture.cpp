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
    if (width < 32) width = 32;
    if (height < 32) height = 32;

    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) {
        return "";
    }

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        ReleaseDC(NULL, hScreenDC);
        return "";
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return "";
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    // Первичная попытка с CAPTUREBLT для учета слоистых окон
    BOOL bltSuccess = BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY | CAPTUREBLT);
    if (!bltSuccess) {
        // Fallback без CAPTUREBLT, если система отклонила захват композитных слоев
        bltSuccess = BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY);
    }

    std::string base64Result;
    if (bltSuccess) {
        Gdiplus::Bitmap gdiBitmap(hBitmap, NULL);
        if (gdiBitmap.GetLastStatus() == Gdiplus::Ok) {
            CLSID clsidJpeg;
            if (GetEncoderClsid(L"image/jpeg", &clsidJpeg) >= 0) {
                Gdiplus::EncoderParameters encoderParameters;
                encoderParameters.Count = 1;
                encoderParameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
                encoderParameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                encoderParameters.Parameter[0].NumberOfValues = 1;
                ULONG q = static_cast<ULONG>((std::max)(10, (std::min)(100, quality)));
                encoderParameters.Parameter[0].Value = &q;

                IStream* pStream = NULL;
                if (SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &pStream))) {
                    if (gdiBitmap.Save(pStream, &clsidJpeg, &encoderParameters) == Gdiplus::Ok) {
                        STATSTG stat;
                        if (SUCCEEDED(pStream->Stat(&stat, STATFLAG_NONAME))) {
                            ULONG streamSize = (ULONG)stat.cbSize.QuadPart;
                            if (streamSize > 0) {
                                std::vector<uint8_t> buffer(streamSize);
                                LARGE_INTEGER liZero;
                                liZero.QuadPart = 0;
                                pStream->Seek(liZero, STREAM_SEEK_SET, NULL);

                                ULONG bytesRead = 0;
                                pStream->Read(buffer.data(), streamSize, &bytesRead);
                                if (bytesRead > 0) {
                                    base64Result = Crypto::base64Encode(buffer.data(), bytesRead);
                                }
                            }
                        }
                    }
                    pStream->Release();
                }
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
