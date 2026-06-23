#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the TFT display (TFT_eSPI, ST7796S) */
void display_init(void);

/* Update display with BACnet object values */
void display_update_values(float av1, float av2, float av3, float av4);

/* Update header link indicators (WiFi and MS/TP). */
void display_set_link_status(bool wifi_connected, bool mstp_connected);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
