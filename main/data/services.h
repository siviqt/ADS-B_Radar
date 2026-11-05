#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ADSB_BACKGROUND_ROUTE_TOKEN UINT32_C(0x80000000)
// All values are copied; callers never retain pointers into worker-owned state.
typedef struct {
    bool metric, trails, cycle, alert_military, alert_emergency, alert_autofocus, reduce_motion;
    unsigned trail_points, trail_style, cycle_seconds, cycle_idle_seconds;
    unsigned map_zoom, radar_zoom, arrivals_filter;
} adsb_preferences_t;
typedef struct { double lat, lon; unsigned radius_nm, poll_seconds; bool configured; unsigned feed_provider; adsb_preferences_t preferences; uint32_t city_id; char city_name[48], city_country[40]; } adsb_settings_t;
typedef struct { char hex[7], callsign[9], registration[16], type[8];
    char category[3], description[40], operator_name[32];
    double lat, lon; int altitude, speed, heading, vertical_rate, squawk, ias, tas, nav_altitude;
    float mach, roll, nav_qnh; bool ground, emergency, military;
    uint64_t seen_ms, stale_ms; unsigned trail_count;
    struct { float lat, lon; int altitude; } trail[60];
} adsb_aircraft_t;
typedef struct { unsigned count; uint64_t updated_ms; adsb_settings_t home;
    adsb_aircraft_t aircraft[200]; } adsb_snapshot_t;
typedef struct { char hex[7], callsign[9]; unsigned token; char origin[8], destination[8],
    registration[16], type[48], operator_name[64], credit[96]; bool valid; } adsb_enrichment_t;
typedef struct { uint64_t at_ms; char text[160]; } adsb_error_t;
typedef struct {
    unsigned fetch_ok, fetch_fail, enrich_ok, enrich_fail;
    uint64_t bytes_received, last_fetch_ms;
    unsigned effective_poll_seconds;
    uint64_t retry_at_ms;
    bool rate_limited, network_ready;
    unsigned error_count;
    adsb_error_t errors[16]; // oldest first, bounded, copied under the service mutex
} adsb_diagnostics_t;
void adsb_diagnostics_copy(adsb_diagnostics_t *out);
void adsb_errors_clear(void);
bool adsb_services_start(void);
bool adsb_settings_get(adsb_settings_t *out);
bool adsb_settings_save(const adsb_settings_t *settings);
bool adsb_snapshot_copy(adsb_snapshot_t *out);
void adsb_service_status(char *out, size_t size);
bool adsb_enrich_request(const char *hex, const char *callsign, unsigned token);
bool adsb_enrich_receive(adsb_enrichment_t *out);
void adsb_services_stop(void);
double adsb_distance_nm(double lat, double lon, const adsb_settings_t *home);
#ifdef __cplusplus
}
#endif
