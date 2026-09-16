#include "rtx_video_vsr.h"

#include <QDebug>
#include <QtGlobal>

#if defined(Q_OS_WIN32)
#include <d3d11_4.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_vsr.h>
#include <nvsdk_ngx_helpers_vsr.h>

namespace {
constexpr unsigned long long kApplicationId = 0;
constexpr const wchar_t *kApplicationPath = L".";

class ScopedD3D11Lock
{
public:
    explicit ScopedD3D11Lock(ID3D10Multithread *multithread)
        : m_multithread(multithread)
    {
        if (m_multithread) {
            m_multithread->Enter();
        }
    }

    ~ScopedD3D11Lock()
    {
        if (m_multithread) {
            m_multithread->Leave();
        }
    }

private:
    ID3D10Multithread *m_multithread;
};
}
#endif

namespace qsc {

class RtxVideoVsr::Impl
{
public:
#if defined(Q_OS_WIN32)
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    ID3D10Multithread *multithread = nullptr;
    NVSDK_NGX_Parameter *parameters = nullptr;
    NVSDK_NGX_Handle *feature = nullptr;
    bool ngxInitialized = false;
    ID3D11Texture2D *temporaryOutput = nullptr;
    UINT temporaryWidth = 0;
    UINT temporaryHeight = 0;
#endif
    bool ready = false;
};

RtxVideoVsr::RtxVideoVsr()
    : m_impl(new Impl())
{
}

RtxVideoVsr::~RtxVideoVsr()
{
    shutdown();
    delete m_impl;
}

bool RtxVideoVsr::initialize(void *d3d11Device)
{
    shutdown();

#if defined(Q_OS_WIN32)
    if (!d3d11Device) {
        return false;
    }

    m_impl->device = static_cast<ID3D11Device *>(d3d11Device);
    m_impl->device->AddRef();

    m_impl->device->GetImmediateContext(&m_impl->context);
    if (!m_impl->context) {
        qWarning("[RtxVideoVsr] failed to get D3D11 context");
        shutdown();
        return false;
    }

    m_impl->context->QueryInterface(IID_PPV_ARGS(&m_impl->multithread));
    if (m_impl->multithread) {
        m_impl->multithread->SetMultithreadProtected(TRUE);
    }

    NVSDK_NGX_Result result = NVSDK_NGX_D3D11_Init(
        kApplicationId, kApplicationPath, m_impl->device);
    if (NVSDK_NGX_FAILED(result)) {
        qWarning("[RtxVideoVsr] NVSDK_NGX_D3D11_Init failed: 0x%08x", result);
        shutdown();
        return false;
    }
    m_impl->ngxInitialized = true;

    result = NVSDK_NGX_D3D11_GetCapabilityParameters(&m_impl->parameters);
    if (NVSDK_NGX_FAILED(result) || !m_impl->parameters) {
        qWarning("[RtxVideoVsr] failed to get NGX capability parameters");
        shutdown();
        return false;
    }

    int available = 0;
    result = m_impl->parameters->Get(NVSDK_NGX_Parameter_VSR_Available, &available);
    if (NVSDK_NGX_FAILED(result) || !available) {
        qWarning("[RtxVideoVsr] VSR is unavailable on this GPU/driver");
        shutdown();
        return false;
    }

    NVSDK_NGX_Feature_Create_Params createParams = {};
    {
        ScopedD3D11Lock lock(m_impl->multithread);
        result = NGX_D3D11_CREATE_VSR_EXT(
            m_impl->context, &m_impl->feature, m_impl->parameters, &createParams);
    }
    if (NVSDK_NGX_FAILED(result) || !m_impl->feature) {
        qWarning("[RtxVideoVsr] failed to create VSR feature: 0x%08x", result);
        shutdown();
        return false;
    }

    m_impl->ready = true;
    return true;
#else
    Q_UNUSED(d3d11Device);
    return false;
#endif
}

void RtxVideoVsr::shutdown()
{
#if defined(Q_OS_WIN32)
    if (!m_impl) {
        return;
    }

    if (m_impl->feature || m_impl->parameters || m_impl->ngxInitialized) {
        if (m_impl->multithread) {
            ScopedD3D11Lock lock(m_impl->multithread);
            if (m_impl->feature) {
                NVSDK_NGX_D3D11_ReleaseFeature(m_impl->feature);
                m_impl->feature = nullptr;
            }
            if (m_impl->ngxInitialized && m_impl->device) {
                NVSDK_NGX_D3D11_Shutdown1(m_impl->device);
                m_impl->ngxInitialized = false;
            }
            if (m_impl->parameters) {
                NVSDK_NGX_D3D11_DestroyParameters(m_impl->parameters);
                m_impl->parameters = nullptr;
            }
        } else {
            if (m_impl->feature) {
                NVSDK_NGX_D3D11_ReleaseFeature(m_impl->feature);
                m_impl->feature = nullptr;
            }
            if (m_impl->ngxInitialized && m_impl->device) {
                NVSDK_NGX_D3D11_Shutdown1(m_impl->device);
                m_impl->ngxInitialized = false;
            }
            if (m_impl->parameters) {
                NVSDK_NGX_D3D11_DestroyParameters(m_impl->parameters);
                m_impl->parameters = nullptr;
            }
        }
    }

    if (m_impl->temporaryOutput) {
        m_impl->temporaryOutput->Release();
        m_impl->temporaryOutput = nullptr;
    }
    if (m_impl->multithread) {
        m_impl->multithread->Release();
        m_impl->multithread = nullptr;
    }
    if (m_impl->context) {
        m_impl->context->Release();
        m_impl->context = nullptr;
    }
    if (m_impl->device) {
        m_impl->device->Release();
        m_impl->device = nullptr;
    }
    m_impl->temporaryWidth = 0;
    m_impl->temporaryHeight = 0;
#endif
    m_impl->ready = false;
}

bool RtxVideoVsr::isReady() const
{
    return m_impl && m_impl->ready;
}

bool RtxVideoVsr::evaluate(void *inputTexture, const QSize &inputSize,
                           void *outputTexture, const QSize &outputSize,
                           int quality)
{
#if defined(Q_OS_WIN32)
    if (!isReady() || !inputTexture || !outputTexture
        || inputSize.isEmpty() || outputSize.isEmpty()) {
        return false;
    }

    auto *input = static_cast<ID3D11Texture2D *>(inputTexture);
    auto *output = static_cast<ID3D11Texture2D *>(outputTexture);
    D3D11_TEXTURE2D_DESC inputDesc = {};
    D3D11_TEXTURE2D_DESC outputDesc = {};
    input->GetDesc(&inputDesc);
    output->GetDesc(&outputDesc);

    const bool inputFormatOk = inputDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM
        || inputDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
    const bool outputFormatOk = outputDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM
        || outputDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!inputFormatOk || !outputFormatOk
        || inputSize.width() > static_cast<int>(inputDesc.Width)
        || inputSize.height() > static_cast<int>(inputDesc.Height)
        || outputSize.width() > static_cast<int>(outputDesc.Width)
        || outputSize.height() > static_cast<int>(outputDesc.Height)) {
        return false;
    }

    ID3D11Texture2D *ngxOutput = output;
    if (!(outputDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)) {
        if (!m_impl->temporaryOutput
            || m_impl->temporaryWidth != outputDesc.Width
            || m_impl->temporaryHeight != outputDesc.Height) {
            if (m_impl->temporaryOutput) {
                m_impl->temporaryOutput->Release();
                m_impl->temporaryOutput = nullptr;
            }
            D3D11_TEXTURE2D_DESC tempDesc = outputDesc;
            tempDesc.BindFlags = D3D11_BIND_RENDER_TARGET
                | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            tempDesc.CPUAccessFlags = 0;
            tempDesc.Usage = D3D11_USAGE_DEFAULT;
            if (FAILED(m_impl->device->CreateTexture2D(
                    &tempDesc, nullptr, &m_impl->temporaryOutput))) {
                return false;
            }
            m_impl->temporaryWidth = outputDesc.Width;
            m_impl->temporaryHeight = outputDesc.Height;
        }
        ngxOutput = m_impl->temporaryOutput;
    }

    quality = qBound(1, quality, 4);
    NVSDK_NGX_D3D11_VSR_Eval_Params evalParams = {};
    evalParams.pInput = input;
    evalParams.pOutput = ngxOutput;
    evalParams.InputSubrectBase.X = 0;
    evalParams.InputSubrectBase.Y = 0;
    evalParams.InputSubrectSize.Width = inputSize.width();
    evalParams.InputSubrectSize.Height = inputSize.height();
    evalParams.OutputSubrectBase.X = 0;
    evalParams.OutputSubrectBase.Y = 0;
    evalParams.OutputSubrectSize.Width = outputSize.width();
    evalParams.OutputSubrectSize.Height = outputSize.height();
    evalParams.QualityLevel = static_cast<NVSDK_NGX_VSR_QualityLevel>(quality);

    ScopedD3D11Lock lock(m_impl->multithread);
    const NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_VSR_EXT(
        m_impl->context, m_impl->feature, m_impl->parameters, &evalParams);
    if (NVSDK_NGX_FAILED(result)) {
        return false;
    }

    if (ngxOutput != output) {
        m_impl->context->CopySubresourceRegion(output, 0, 0, 0, 0, ngxOutput, 0, nullptr);
    }
    return true;
#else
    Q_UNUSED(inputTexture);
    Q_UNUSED(inputSize);
    Q_UNUSED(outputTexture);
    Q_UNUSED(outputSize);
    Q_UNUSED(quality);
    return false;
#endif
}

} // namespace qsc
