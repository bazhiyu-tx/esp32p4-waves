/**
 * @file audio_c_api.cpp
 * @brief C API implementation bridging to AudioMDnsService
 */
#include "audio_c_api.h"
#include "audio_mDNS_service.h"

extern AudioMDnsService *g_audio_svc;

void audio_start(void)
{
    if (g_audio_svc) {
        g_audio_svc->start_audio();
    }
}

void audio_stop(void)
{
    if (g_audio_svc) {
        g_audio_svc->stop_audio();
    }
}
