// USB audio interface on the Seed's own USB port
//
// Makes the Daisy a class-compliant USB sound card (USB Audio Class 1, stereo
// in and out, 24-bit, 48 kHz) that needs no driver on macOS, Windows, Linux
// or iOS. The computer sees one output and one input, each named after the
// line connector they mirror.
//
// The codec keeps its own clock, so line I/O works whether or not a computer
// is connected. The computer's clock and the codec's drift apart, and the
// class absorbs that: the computer is told how fast to send (an asynchronous
// feedback endpoint), and the packets sent back vary between 47 and 49
// samples. Each direction buffers about 2.7 ms.
//
// To use it, set USB_DEVICE = 1 in the app's app.mk. Apps that do can't use
// libDaisy's USB device classes (MidiUsbHandler, USB logging, UsbHandle);
// the link fails if they try. There's one USB port, so make one UsbAudio.
//
//     UsbAudio usb;
//     usb.Init("Grainfall");        // in main(), before hw.StartAudio
//
//     // in AudioCallback, once per block:
//     usb.Read(out[0], out[1], size);   // computer -> Daisy
//     usb.Write(in[0], in[1], size);    // Daisy -> computer

#pragma once

#include <cstddef>

class UsbAudio
{
  public:
    // `name` is the product name the computer shows. It must outlive the
    // object (a string literal is fine).
    void Init(const char* name = "Daisy Pod");

    // Fills `left` and `right` with `size` samples from the computer, or
    // silence while it isn't sending. Call from the audio callback.
    void Read(float* left, float* right, size_t size);

    // Sends `size` samples to the computer. They're dropped while it isn't
    // recording. Call from the audio callback.
    void Write(const float* left, const float* right, size_t size);

    // True while the computer is streaming audio to the Daisy.
    bool Playing() const;
    // True while the computer is recording from the Daisy.
    bool Recording() const;
};
