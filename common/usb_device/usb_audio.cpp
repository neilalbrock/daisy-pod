#include "usb_audio.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"

// Everything below runs in the USB interrupt except UsbAudio's methods, which
// run in the audio callback. The two sides share only the ring buffers and a
// few atomics, so neither ever waits for the other.

namespace
{
// pid.codes test IDs, which are free for private use. Pick a real PID from
// pid.codes before distributing a device.
constexpr uint16_t kVendorId  = 0x1209;
constexpr uint16_t kProductId = 0x0001;

constexpr uint32_t kSampleRate      = 48000;
constexpr size_t   kBytesPerSample  = 3;
constexpr size_t   kBytesPerFrame   = 2 * kBytesPerSample;
constexpr size_t   kFramesPerPacket = kSampleRate / 1000;
// One extra frame per packet lets either side catch up on the other's clock
constexpr size_t   kMaxPacketFrames = kFramesPerPacket + 1;
constexpr uint16_t kMaxPacketBytes  = kMaxPacketFrames * kBytesPerFrame;

constexpr uint8_t kIfControl = 0;
constexpr uint8_t kIfOut     = 1; // computer -> Daisy
constexpr uint8_t kIfIn      = 2; // Daisy -> computer

constexpr uint8_t kEpOut      = 0x01;
constexpr uint8_t kEpFeedback = 0x81;
constexpr uint8_t kEpIn       = 0x82;

// The computer reads the feedback endpoint every 2^kFeedbackRefresh ms
constexpr uint8_t kFeedbackRefresh = 1;

// Audio class request codes and the sampling frequency control
constexpr uint8_t kReqSetCur          = 0x01;
constexpr uint8_t kReqGetCur          = 0x81;
constexpr uint8_t kSamplingFreqControl = 0x01;

#define LE16(x) uint8_t((x) & 0xFF), uint8_t(((x) >> 8) & 0xFF)
#define LE24(x) LE16(x), uint8_t(((x) >> 16) & 0xFF)

/* Descriptors ---------------------------------------------------------------*/

// Audio function topology: USB streaming input terminal 1 -> line out
// terminal 2, and line in terminal 3 -> USB streaming output terminal 4.
// There are no feature units, so the computer handles volume itself.
constexpr uint16_t kAcClassBytes = 10 + 12 + 9 + 12 + 9;
constexpr uint16_t kConfigBytes  = 9 + (9 + kAcClassBytes)
                                  + (9 + 9 + 7 + 11 + 9 + 7 + 9)
                                  + (9 + 9 + 7 + 11 + 9 + 7);

#define FORMAT_TYPE_I                                                   \
    11, 0x24, 0x02, 0x01, /* FORMAT_TYPE, FORMAT_TYPE_I */              \
        2, kBytesPerSample, 8 * kBytesPerSample, 1, /* one frequency */ \
        LE24(kSampleRate)

alignas(4) uint8_t config_desc[] = {
    // Configuration: 3 interfaces, bus powered, 250 mA
    9, USB_DESC_TYPE_CONFIGURATION, LE16(kConfigBytes), 3, 1, 0, 0x80, 125,

    // Interface 0: audio control
    9, USB_DESC_TYPE_INTERFACE, kIfControl, 0, 0, 0x01, 0x01, 0x00, 0,
    // Class-specific header: UAC 1.0, streaming interfaces 1 and 2
    10, 0x24, 0x01, LE16(0x0100), LE16(kAcClassBytes), 2, kIfOut, kIfIn,
    // Input terminal 1: USB streaming, stereo (left and right front)
    12, 0x24, 0x02, 1, LE16(0x0101), 0, 2, LE16(0x0003), 0, 0,
    // Output terminal 2: line connector, fed by terminal 1
    9, 0x24, 0x03, 2, LE16(0x0603), 0, 1, 0,
    // Input terminal 3: line connector, stereo
    12, 0x24, 0x02, 3, LE16(0x0603), 0, 2, LE16(0x0003), 0, 0,
    // Output terminal 4: USB streaming, fed by terminal 3
    9, 0x24, 0x03, 4, LE16(0x0101), 0, 3, 0,

    // Interface 1, alternate 0: computer -> Daisy, no bandwidth
    9, USB_DESC_TYPE_INTERFACE, kIfOut, 0, 0, 0x01, 0x02, 0x00, 0,
    // Interface 1, alternate 1: streaming, data and feedback endpoints
    9, USB_DESC_TYPE_INTERFACE, kIfOut, 1, 2, 0x01, 0x02, 0x00, 0,
    // AS general: linked to terminal 1, 1 frame delay, PCM
    7, 0x24, 0x01, 1, 1, LE16(0x0001),
    FORMAT_TYPE_I,
    // Isochronous, asynchronous data endpoint, synchronised by kEpFeedback
    9, USB_DESC_TYPE_ENDPOINT, kEpOut, 0x05, LE16(kMaxPacketBytes), 1, 0,
    kEpFeedback,
    // Class-specific endpoint: no controls
    7, 0x25, 0x01, 0x00, 0, LE16(0),
    // Isochronous feedback endpoint, 3 bytes (10.14 samples per frame)
    9, USB_DESC_TYPE_ENDPOINT, kEpFeedback, 0x11, LE16(3), 1,
    kFeedbackRefresh, 0,

    // Interface 2, alternate 0: Daisy -> computer, no bandwidth
    9, USB_DESC_TYPE_INTERFACE, kIfIn, 0, 0, 0x01, 0x02, 0x00, 0,
    // Interface 2, alternate 1: streaming, one data endpoint
    9, USB_DESC_TYPE_INTERFACE, kIfIn, 1, 1, 0x01, 0x02, 0x00, 0,
    // AS general: linked to terminal 4, 1 frame delay, PCM
    7, 0x24, 0x01, 4, 1, LE16(0x0001),
    FORMAT_TYPE_I,
    // Isochronous, asynchronous data endpoint
    9, USB_DESC_TYPE_ENDPOINT, kEpIn, 0x05, LE16(kMaxPacketBytes), 1, 0, 0,
    // Class-specific endpoint: no controls
    7, 0x25, 0x01, 0x00, 0, LE16(0),
};
static_assert(sizeof(config_desc) == kConfigBytes, "wTotalLength is wrong");

alignas(4) uint8_t device_desc[] = {
    USB_LEN_DEV_DESC, USB_DESC_TYPE_DEVICE, LE16(0x0200),
    0x00, 0x00, 0x00, // class defined per interface
    USB_MAX_EP0_SIZE, LE16(kVendorId), LE16(kProductId), LE16(0x0100),
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR, 1,
};

alignas(4) uint8_t lang_id_desc[] = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, LE16(0x0409)};

alignas(4) uint8_t string_desc[USBD_MAX_STR_DESC_SIZ];

const char* product_name = "Daisy Pod";

uint8_t* StringDesc(const char* s, uint16_t* length)
{
    USBD_GetString((uint8_t*)s, string_desc, length);
    return string_desc;
}

uint8_t* GetDeviceDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    *length = sizeof(device_desc);
    return device_desc;
}

uint8_t* GetLangIdDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    *length = sizeof(lang_id_desc);
    return lang_id_desc;
}

uint8_t* GetManufacturerDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    return StringDesc("Electrosmith", length); // as libDaisy reports
}

uint8_t* GetProductDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    return StringDesc(product_name, length);
}

// The chip's unique ID, so the computer can tell two Daisies apart
uint8_t* GetSerialDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    static char serial[25];
    uint32_t    uid[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    for(int i = 0; i < 24; i++)
    {
        uint32_t nibble = (uid[i / 8] >> (28 - 4 * (i % 8))) & 0xF;
        serial[i]       = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    serial[24] = '\0';
    return StringDesc(serial, length);
}

uint8_t* GetConfigurationDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    return StringDesc("Audio", length);
}

uint8_t* GetInterfaceDesc(USBD_SpeedTypeDef, uint16_t* length)
{
    return StringDesc("Audio", length);
}

USBD_DescriptorsTypeDef descriptors = {
    GetDeviceDesc,
    GetLangIdDesc,
    GetManufacturerDesc,
    GetProductDesc,
    GetSerialDesc,
    GetConfigurationDesc,
    GetInterfaceDesc,
};

/* Ring buffers --------------------------------------------------------------*/

// Single-producer, single-consumer ring of stereo frames, held as 24-bit
// integers. The indices run freely and wrap, so the fill level is always
// write - read. The producer drops frames rather than overwrite unread ones.
struct FrameRing
{
    static constexpr uint32_t kSize = 512; // frames, a power of two

    int32_t               frames[kSize][2];
    std::atomic<uint32_t> write{0};
    std::atomic<uint32_t> read{0};

    uint32_t Level() const
    {
        return write.load(std::memory_order_acquire)
               - read.load(std::memory_order_acquire);
    }
};

// Each side aims to keep this many frames buffered: about 2.7 ms, enough to
// ride out a late packet. Less buffering means less latency.
constexpr uint32_t kTargetFrames = 128;

FrameRing out_ring; // computer -> Daisy: USB writes, audio callback reads
FrameRing in_ring;  // Daisy -> computer: audio callback writes, USB reads

/* USB streaming state -------------------------------------------------------*/

USBD_HandleTypeDef usbd;

uint8_t alt_setting[3];

std::atomic<bool> out_streaming{false};
std::atomic<bool> in_streaming{false};
// Bumped each time the computer starts sending, so the audio callback knows
// to drop what's left of the last stream
std::atomic<uint32_t> out_generation{0};

alignas(4) uint8_t out_packet[kMaxPacketBytes];
alignas(4) uint8_t in_packet[kMaxPacketBytes];
size_t             in_packet_bytes;
bool               in_primed;

alignas(4) uint8_t feedback_packet[4];
float              average_level;

alignas(4) uint8_t control_data[4];

// The feedback value tells the computer how many frames to send per 1 ms
// frame, as a 10.14 fixed point number. It's the nominal 48 nudged by how far
// the receive buffer is from its target, so the buffer settles near the
// target whatever the two clocks are doing. The nudge takes about half a
// second to correct a whole packet, far slower than the computer's response,
// so the loop stays stable. The level is sampled at each start of frame,
// always at the same point in the packet cycle.
void SetFeedback(float frames_per_packet)
{
    uint32_t value     = uint32_t(frames_per_packet * 16384.0f);
    feedback_packet[0] = uint8_t(value);
    feedback_packet[1] = uint8_t(value >> 8);
    feedback_packet[2] = uint8_t(value >> 16);
}

void UpdateFeedback()
{
    constexpr float kGain          = 1.0f / 512.0f;
    constexpr float kMaxCorrection = 0.25f; // frames per packet

    float level = float(int32_t(out_ring.Level()));
    average_level += (level - average_level) * (1.0f / 64.0f);

    float correction = (kTargetFrames - average_level) * kGain;
    correction = std::clamp(correction, -kMaxCorrection, kMaxCorrection);

    SetFeedback(kFramesPerPacket + correction);
}

void StartOutStream()
{
    USBD_LL_OpenEP(&usbd, kEpOut, USBD_EP_TYPE_ISOC, kMaxPacketBytes);
    USBD_LL_OpenEP(&usbd, kEpFeedback, USBD_EP_TYPE_ISOC, 3);
    usbd.ep_out[kEpOut & 0xF].is_used     = 1;
    usbd.ep_in[kEpFeedback & 0xF].is_used = 1;
    USBD_LL_FlushEP(&usbd, kEpFeedback);

    average_level = kTargetFrames;
    SetFeedback(kFramesPerPacket);
    out_generation.fetch_add(1, std::memory_order_release);
    out_streaming.store(true, std::memory_order_release);

    USBD_LL_PrepareReceive(&usbd, kEpOut, out_packet, kMaxPacketBytes);
    USBD_LL_Transmit(&usbd, kEpFeedback, feedback_packet, 3);
}

void StopOutStream()
{
    out_streaming.store(false, std::memory_order_release);
    USBD_LL_CloseEP(&usbd, kEpOut);
    USBD_LL_CloseEP(&usbd, kEpFeedback);
    usbd.ep_out[kEpOut & 0xF].is_used     = 0;
    usbd.ep_in[kEpFeedback & 0xF].is_used = 0;
}

// Fills in_packet with the next packet for the computer. Normally that's
// 48 frames, but one more or one less when the buffer has drifted from its
// target, which matches the computer's rate to the codec's.
void PrepareInPacket()
{
    uint32_t level  = in_ring.Level();
    size_t   frames = kFramesPerPacket;
    if(level > kTargetFrames + 16)
        frames++;
    else if(level < kTargetFrames - 16)
        frames--;

    if(!in_primed && level >= kTargetFrames)
        in_primed = true;
    if(in_primed && level < frames)
        in_primed = false; // underrun: send silence until the buffer refills

    uint8_t* p = in_packet;
    if(in_primed)
    {
        uint32_t r = in_ring.read.load(std::memory_order_relaxed);
        for(size_t i = 0; i < frames; i++, r++)
        {
            for(int ch = 0; ch < 2; ch++)
            {
                int32_t s = in_ring.frames[r % FrameRing::kSize][ch];
                *p++      = uint8_t(s);
                *p++      = uint8_t(s >> 8);
                *p++      = uint8_t(s >> 16);
            }
        }
        in_ring.read.store(r, std::memory_order_release);
    }
    else
    {
        std::fill(p, p + frames * kBytesPerFrame, 0);
    }
    in_packet_bytes = frames * kBytesPerFrame;
}

void StartInStream()
{
    USBD_LL_OpenEP(&usbd, kEpIn, USBD_EP_TYPE_ISOC, kMaxPacketBytes);
    usbd.ep_in[kEpIn & 0xF].is_used = 1;
    USBD_LL_FlushEP(&usbd, kEpIn);

    // Drop whatever built up while nobody was recording
    in_ring.read.store(in_ring.write.load(std::memory_order_acquire),
                       std::memory_order_release);
    in_primed = false;
    in_streaming.store(true, std::memory_order_release);

    PrepareInPacket();
    USBD_LL_Transmit(&usbd, kEpIn, in_packet, in_packet_bytes);
}

void StopInStream()
{
    in_streaming.store(false, std::memory_order_release);
    USBD_LL_CloseEP(&usbd, kEpIn);
    usbd.ep_in[kEpIn & 0xF].is_used = 0;
}

/* USB class callbacks -------------------------------------------------------*/

uint8_t ClassInit(USBD_HandleTypeDef*, uint8_t)
{
    std::fill(alt_setting, alt_setting + 3, 0);
    return USBD_OK;
}

uint8_t ClassDeInit(USBD_HandleTypeDef*, uint8_t)
{
    if(alt_setting[kIfOut])
        StopOutStream();
    if(alt_setting[kIfIn])
        StopInStream();
    std::fill(alt_setting, alt_setting + 3, 0);
    return USBD_OK;
}

uint8_t SetInterface(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req)
{
    uint8_t iface = LOBYTE(req->wIndex);
    uint8_t alt   = LOBYTE(req->wValue);
    if(pdev->dev_state != USBD_STATE_CONFIGURED || iface > kIfIn
       || alt > (iface == kIfControl ? 0 : 1))
    {
        USBD_CtlError(pdev, req);
        return USBD_FAIL;
    }
    if(alt == alt_setting[iface])
        return USBD_OK;

    alt_setting[iface] = alt;
    if(iface == kIfOut)
        alt ? StartOutStream() : StopOutStream();
    else if(iface == kIfIn)
        alt ? StartInStream() : StopInStream();
    return USBD_OK;
}

uint8_t ClassSetup(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req)
{
    static uint16_t status = 0;

    switch(req->bmRequest & USB_REQ_TYPE_MASK)
    {
        case USB_REQ_TYPE_STANDARD:
            switch(req->bRequest)
            {
                case USB_REQ_GET_STATUS:
                    USBD_CtlSendData(pdev, (uint8_t*)&status, 2);
                    return USBD_OK;
                case USB_REQ_GET_INTERFACE:
                    if(LOBYTE(req->wIndex) > kIfIn)
                        break;
                    USBD_CtlSendData(
                        pdev, &alt_setting[LOBYTE(req->wIndex)], 1);
                    return USBD_OK;
                case USB_REQ_SET_INTERFACE: return SetInterface(pdev, req);
                case USB_REQ_CLEAR_FEATURE: return USBD_OK;
                default: break;
            }
            break;

        case USB_REQ_TYPE_CLASS:
            // Hosts may read or set the sampling frequency on an endpoint,
            // even though only 48 kHz is offered. Report 48 kHz, and accept
            // and ignore a set.
            if((req->bmRequest & 0x1F) == USB_REQ_RECIPIENT_ENDPOINT
               && HIBYTE(req->wValue) == kSamplingFreqControl
               && req->wLength == 3)
            {
                if(req->bRequest == kReqGetCur)
                {
                    control_data[0] = uint8_t(kSampleRate);
                    control_data[1] = uint8_t(kSampleRate >> 8);
                    control_data[2] = uint8_t(kSampleRate >> 16);
                    USBD_CtlSendData(pdev, control_data, 3);
                    return USBD_OK;
                }
                if(req->bRequest == kReqSetCur)
                {
                    USBD_CtlPrepareRx(pdev, control_data, 3);
                    return USBD_OK;
                }
            }
            break;

        default: break;
    }
    USBD_CtlError(pdev, req);
    return USBD_FAIL;
}

uint8_t ClassDataIn(USBD_HandleTypeDef* pdev, uint8_t epnum)
{
    if(epnum == (kEpFeedback & 0x7F) && out_streaming.load())
    {
        USBD_LL_Transmit(pdev, kEpFeedback, feedback_packet, 3);
    }
    else if(epnum == (kEpIn & 0x7F) && in_streaming.load())
    {
        PrepareInPacket();
        USBD_LL_Transmit(pdev, kEpIn, in_packet, in_packet_bytes);
    }
    return USBD_OK;
}

// The packet wasn't collected in the frame it was queued for (the computer
// reads feedback less often than every frame, or a frame was missed). The
// HAL has already flushed it, so queue the same data again.
uint8_t ClassIsoInIncomplete(USBD_HandleTypeDef* pdev, uint8_t epnum)
{
    if(epnum == (kEpFeedback & 0x7F) && out_streaming.load())
        USBD_LL_Transmit(pdev, kEpFeedback, feedback_packet, 3);
    else if(epnum == (kEpIn & 0x7F) && in_streaming.load())
        USBD_LL_Transmit(pdev, kEpIn, in_packet, in_packet_bytes);
    return USBD_OK;
}

uint8_t ClassDataOut(USBD_HandleTypeDef* pdev, uint8_t epnum)
{
    if(epnum != kEpOut || !out_streaming.load())
        return USBD_OK;

    uint32_t frames = USBD_LL_GetRxDataSize(pdev, kEpOut) / kBytesPerFrame;
    uint32_t w      = out_ring.write.load(std::memory_order_relaxed);
    if(out_ring.Level() + frames <= FrameRing::kSize)
    {
        const uint8_t* p = out_packet;
        for(uint32_t i = 0; i < frames; i++, w++)
        {
            for(int ch = 0; ch < 2; ch++, p += 3)
            {
                // Little-endian 24-bit, sign-extended to 32
                int32_t s = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16
                                    | uint32_t(p[2]) << 24)
                            >> 8;
                out_ring.frames[w % FrameRing::kSize][ch] = s;
            }
        }
        out_ring.write.store(w, std::memory_order_release);
    }
    // else the audio callback has stopped reading, so drop the packet

    USBD_LL_PrepareReceive(pdev, kEpOut, out_packet, kMaxPacketBytes);
    return USBD_OK;
}

uint8_t ClassIsoOutIncomplete(USBD_HandleTypeDef* pdev, uint8_t epnum)
{
    if(epnum == kEpOut && out_streaming.load())
        USBD_LL_PrepareReceive(pdev, kEpOut, out_packet, kMaxPacketBytes);
    return USBD_OK;
}

uint8_t ClassSof(USBD_HandleTypeDef*)
{
    if(out_streaming.load())
        UpdateFeedback();
    return USBD_OK;
}

uint8_t* GetConfigDesc(uint16_t* length)
{
    *length = sizeof(config_desc);
    return config_desc;
}

USBD_ClassTypeDef audio_class = {
    ClassInit,
    ClassDeInit,
    ClassSetup,
    nullptr, // EP0_TxSent
    nullptr, // EP0_RxReady: a sampling frequency set is ignored
    ClassDataIn,
    ClassDataOut,
    ClassSof,
    ClassIsoInIncomplete,
    ClassIsoOutIncomplete,
    GetConfigDesc,
    GetConfigDesc,
    GetConfigDesc,
    nullptr, // device qualifier: full speed only
};

// Keeps the USB clock (the internal 48 MHz oscillator) locked to the
// computer's start-of-frame packets. The audio clock is separate (PLL3).
void StartClockRecovery()
{
    __HAL_RCC_CRS_CLK_ENABLE();
    RCC_CRSInitTypeDef crs = {};
    crs.Prescaler             = RCC_CRS_SYNC_DIV1;
    crs.Source                = RCC_CRS_SYNC_SOURCE_USB2;
    crs.Polarity              = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue           = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
    crs.ErrorLimitValue       = RCC_CRS_ERRORLIMIT_DEFAULT;
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&crs);
}

float ToFloat(int32_t s)
{
    return float(s) * (1.0f / 8388608.0f);
}

int32_t FromFloat(float x)
{
    return int32_t(std::clamp(x, -1.0f, 1.0f) * 8388607.0f);
}

// Audio callback side of the receive ring
uint32_t seen_out_generation;
bool     out_primed;

} // namespace

void UsbAudio::Init(const char* name)
{
    product_name = name;
    USBD_Init(&usbd, &descriptors, DEVICE_FS);
    USBD_RegisterClass(&usbd, &audio_class);
    USBD_Start(&usbd);
    StartClockRecovery();
}

void UsbAudio::Read(float* left, float* right, size_t size)
{
    uint32_t generation = out_generation.load(std::memory_order_acquire);
    if(generation != seen_out_generation)
    {
        // A new stream: drop what's left of the old one and buffer afresh
        seen_out_generation = generation;
        out_ring.read.store(out_ring.write.load(std::memory_order_acquire),
                            std::memory_order_release);
        out_primed = false;
    }

    uint32_t level = out_ring.Level();
    if(!out_primed && level >= kTargetFrames)
        out_primed = true;
    if(out_primed && level < size)
        out_primed = false; // underrun: play silence until the buffer refills

    if(!out_primed || !out_streaming.load(std::memory_order_acquire))
    {
        std::fill(left, left + size, 0.0f);
        std::fill(right, right + size, 0.0f);
        return;
    }

    uint32_t r = out_ring.read.load(std::memory_order_relaxed);
    for(size_t i = 0; i < size; i++, r++)
    {
        left[i]  = ToFloat(out_ring.frames[r % FrameRing::kSize][0]);
        right[i] = ToFloat(out_ring.frames[r % FrameRing::kSize][1]);
    }
    out_ring.read.store(r, std::memory_order_release);
}

void UsbAudio::Write(const float* left, const float* right, size_t size)
{
    if(in_ring.Level() + size > FrameRing::kSize)
        return; // nobody is reading, so drop the block

    uint32_t w = in_ring.write.load(std::memory_order_relaxed);
    for(size_t i = 0; i < size; i++, w++)
    {
        in_ring.frames[w % FrameRing::kSize][0] = FromFloat(left[i]);
        in_ring.frames[w % FrameRing::kSize][1] = FromFloat(right[i]);
    }
    in_ring.write.store(w, std::memory_order_release);
}

bool UsbAudio::Playing() const
{
    return out_streaming.load(std::memory_order_acquire);
}

bool UsbAudio::Recording() const
{
    return in_streaming.load(std::memory_order_acquire);
}
