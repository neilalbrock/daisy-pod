#include "web_link.h"

namespace weblink
{
namespace
{
uint32_t Fit(uint32_t v, uint32_t max)
{
    return v < max ? v : max;
}

float Unit(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
} // namespace

void Writer::Byte(uint8_t b)
{
    if(n_ + 1 < kMaxMessage) // one spare for F7
        buf_[n_++] = b;
    else
        overflow_ = true;
}

void Writer::Begin(Msg type)
{
    n_        = 0;
    overflow_ = false;
    Byte(0xF0);
    Byte(kId);
    Byte(kVersion);
    Byte((uint8_t)type);
}

void Writer::U7(uint32_t v)
{
    Byte((uint8_t)Fit(v, 0x7F));
}

void Writer::U14(uint32_t v)
{
    v = Fit(v, 0x3FFF);
    Byte((uint8_t)(v >> 7));
    Byte((uint8_t)(v & 0x7F));
}

void Writer::U21(uint32_t v)
{
    v = Fit(v, 0x1FFFFF);
    Byte((uint8_t)(v >> 14));
    Byte((uint8_t)((v >> 7) & 0x7F));
    Byte((uint8_t)(v & 0x7F));
}

void Writer::Unit(float v)
{
    U14((uint32_t)(weblink::Unit(v) * 16383.0f + 0.5f));
}

void Writer::Str(const char *s)
{
    while(*s)
        Byte((uint8_t)(*s++ & 0x7F));
    Byte(0);
}

size_t Writer::End()
{
    buf_[n_++] = 0xF7;
    return overflow_ ? 0 : n_;
}

void WriteHello(Writer &w, const Hello &h)
{
    w.Begin(Msg::kHello);
    w.Str(h.app);
    w.U21(h.sample_rate);
    w.U21(h.buffer_frames);
    w.U14(h.overview_columns);
    w.U7(h.num_groups);
    for(int i = 0; i < h.num_groups; i++)
        w.Str(h.groups[i]);
    w.U7(h.num_meters);
    for(int i = 0; i < h.num_meters; i++)
        w.Str(h.meters[i]);
    w.U7(h.num_params);

    // Added after the first version; the page reads them if present.
    w.U7(h.num_traces);
    w.U7(h.trace_period_ms);
    for(int i = 0; i < h.num_traces; i++)
    {
        const Trace &t = h.traces[i];
        w.Str(t.name);
        w.Str(t.unit);
        w.U14((uint32_t)(t.min + 8192));
        w.U14((uint32_t)(t.max + 8192));
    }
    w.Str(h.xy != nullptr ? h.xy : "");
    w.U7(h.presets);
}

void WriteParam(Writer &w, int index, const Param &p)
{
    w.Begin(Msg::kParam);
    w.U7((uint32_t)index);
    w.U7((uint32_t)p.kind);
    w.U7(p.group);
    w.U7(p.columns);
    w.Str(p.name);
    w.U7(p.num_options);
    for(int i = 0; i < p.num_options; i++)
        w.Str(p.options[i]);
}

void WriteHead(Writer &w, const Head &h)
{
    w.U21(h.start);
    w.U21(h.length > 0.0f ? (uint32_t)h.length : 0);
    w.U21(h.pos);
    w.U14((uint32_t)(h.rate * 1000.0f + 0.5f));
    w.U7((h.reverse ? 1u : 0u) | (h.tap ? 2u : 0u));
    w.U7((uint32_t)(Unit(h.amp) * 127.0f + 0.5f));
    w.U7((uint32_t)(Unit(h.pan) * 127.0f + 0.5f));
}

void WriteTraceValue(Writer &w, const Trace &t, float v)
{
    w.Unit((v - (float)t.min) / (float)(t.max - t.min));
}

void WriteXY(Writer &w, float x, float y)
{
    auto raw = [](float v) { return (uint32_t)((v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v)) * 63.0f + 63.5f); };
    w.U7(raw(x));
    w.U7(raw(y));
}

bool Parse(const uint8_t *data, int length, Command *out)
{
    if(length < 3 || data[0] != kId || data[1] != kVersion)
        return false;
    out->type  = (Msg)data[2];
    out->index = 0;
    out->raw   = 0;
    switch(out->type)
    {
        case Msg::kRequest:
        case Msg::kKeepAlive: return true;
        case Msg::kSet:
            if(length < 6)
                return false;
            out->index = data[3];
            out->raw   = (data[4] << 7) | data[5];
            return true;
        case Msg::kPresetSelect:
        case Msg::kPresetSave:
            if(length < 4)
                return false;
            out->index = data[3];
            return true;
        case Msg::kPresetReset: return true;
        default: return false;
    }
}

bool Session::Receive(const uint8_t *data, int length, uint32_t now_ms, Command *command)
{
    Command c;
    if(!Parse(data, length, &c))
        return false;
    last_heard_ = now_ms;
    heard_      = true;
    if(c.type == Msg::kRequest)
        requested_ = true;
    if(c.type != Msg::kSet && c.type != Msg::kPresetSelect && c.type != Msg::kPresetSave
       && c.type != Msg::kPresetReset)
        return false;
    *command = c;
    return true;
}

Session::Send Session::Next(uint32_t now_ms, bool state_changed, int *param)
{
    if(!heard_ || now_ms - last_heard_ > kTimeoutMs)
        return Send::kNothing;

    if(requested_)
    {
        requested_  = false;
        next_param_ = -1;
    }
    if(next_param_ < num_params_)
    {
        // Hello, then each parameter, one per call.
        int i = next_param_++;
        if(i < 0)
        {
            restart_ = true;
            return Send::kHello;
        }
        *param = i;
        return Send::kParam;
    }
    uint32_t since = now_ms - last_state_;
    if((state_changed && since >= kChangeMs) || since >= kStateMs)
    {
        last_state_ = now_ms;
        return Send::kState;
    }
    if(now_ms - last_frame_ >= kFrameMs)
    {
        last_frame_ = now_ms;
        return Send::kFrame;
    }
    return Send::kNothing;
}

bool Session::TakeRestart()
{
    bool r   = restart_;
    restart_ = false;
    return r;
}
} // namespace weblink
