#ifndef QTSCRCPY_RTX_VIDEO_VSR_H
#define QTSCRCPY_RTX_VIDEO_VSR_H

#include <QSize>

namespace qsc {

// Thin wrapper around the RTX Video SDK 1.1.0 DX11 VSR API.
// The caller owns the D3D11 device and all input/output textures.
class RtxVideoVsr
{
public:
    RtxVideoVsr();
    ~RtxVideoVsr();

    RtxVideoVsr(const RtxVideoVsr &) = delete;
    RtxVideoVsr &operator=(const RtxVideoVsr &) = delete;

    bool initialize(void *d3d11Device);
    void shutdown();
    bool isReady() const;

    // Input and output must be DXGI_FORMAT_R8G8B8A8_UNORM or B8G8R8A8_UNORM.
    bool evaluate(void *inputTexture, const QSize &inputSize,
                 void *outputTexture, const QSize &outputSize,
                 int quality);

private:
    class Impl;
    Impl *m_impl;
};

} // namespace qsc

#endif // QTSCRCPY_RTX_VIDEO_VSR_H
