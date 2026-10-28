// Dump params - an app's hello and parameter descriptions, on the host
//
// Built by `make m4l` for each app, from the app's sources without main.cpp
// (web_view.cpp and what it calls, all libDaisy-free) and common/. It
// answers a request the way the app's main loop does, and writes the
// messages to stdout as the Pod would send them, F0 to F7, until the state
// is next. m4l/tools/build.mjs decodes them with web/js/protocol.js.
//
// The app is picked with -DAPP_<NAME>; the hello's buffer sizes only matter
// to the page's buffer view, so any will do.

#include <cstdio>

#include "web_link.h"
#include "web_view.h"

namespace
{
void WriteHello(weblink::Writer &w)
{
#if defined(APP_GRAINFALL)
    web_view::WriteHello(w, 48000.0f, 0, 0);
#elif defined(APP_DRIFTNET)
    web_view::WriteHello(w, 48000.0f, 0);
#else
    web_view::WriteHello(w, 48000.0f);
#endif
}
} // namespace

int main()
{
    static uint8_t  buf[weblink::kMaxMessage];
    weblink::Writer w;
    w.Init(buf);

    weblink::Session session;
    session.Init(web_view::kParams, web_view::kNumParams);
    const uint8_t    request[] = {weblink::kId, weblink::kVersion, (uint8_t)weblink::Msg::kRequest};
    weblink::Command command;
    session.Receive(request, sizeof(request), 0, &command);

    for(;;)
    {
        switch(session.Next(0, false))
        {
            case weblink::Session::Send::kHello: WriteHello(w); break;
            case weblink::Session::Send::kParam: session.WriteParams(w); break;
            default: return 0;
        }
        size_t n = w.End();
        if(n == 0)
        {
            fprintf(stderr, "a message overflowed\n");
            return 1;
        }
        fwrite(buf, 1, n, stdout);
    }
}
