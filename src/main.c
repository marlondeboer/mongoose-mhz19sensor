#include <stdio.h>

#include "common/mbuf.h"
#include "common/platform.h"
#include "fw/src/mgos_app.h"
#include "mgos_gpio.h"
#include "mgos_timers.h"
#include "mgos_uart.h"
#include "mgos_hal.h"
#include "mgos_mongoose.h"
#include "mgos_uart_hal.h"
#include "mgos_utils.h"
#include "mgos_sys_config.h"
#include "mgos_http_server.h"
#include "mgos_shadow.h"
#include "mgos_dht.h"

#define UART_NO 1
#define SCMD_LEN 9

static struct mgos_dht *s_dht = NULL;
float t, h;

/* the command we send over UART serial to the mhz-19 sensor */
const char abc_enable[SCMD_LEN]         = { 0xFF, 0x01, 0x79, 0xA0, 0x00, 0x00, 0x00, 0x00, 0xE6 }; //auto baseline correction enabled
const char abc_disable[SCMD_LEN]        = { 0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86 }; //auto baseline correction disabled
const char read_value[SCMD_LEN]         = { 0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79 }; //read co2 ppm
const char cal_zero[SCMD_LEN]           = { 0xFF, 0x01, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78 }; //0 point calibration (0point == 400 ppm)
const char cal_span2k[SCMD_LEN]         = { 0xFF, 0x01, 0x88, 0x07, 0x00, 0x00, 0x00, 0x00, 0xA0 }; //span point calibration of 2000ppm, use only after cal_zero
const char sensor_reset[SCMD_LEN]       = { 0xFF, 0x01, 0x8d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78 }; //reset sensor
const char set_range2k[SCMD_LEN]        = { 0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x07, 0xD0, 0x8F }; //sets sensor range from 0 to 2000ppm
const char set_range5k[SCMD_LEN]        = { 0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x13, 0x88, 0xCB }; //sets sensor range from 0 to 5000ppm

/* the http get translation of the UART serial commands */
const char *abc_en_cmd		= "abc_enable";
const char *abc_dis_cmd         = "abc_disable";
const char *s_read_cmd          = "read";
const char *cal_zero_cmd	= "cal_zero";
const char *cal_span2k_cmd      = "cal_span2k";
const char *s_rst_cmd           = "reset";
const char *set_range2k_cmd     = "set_range2k";
const char *set_range5k_cmd     = "set_range5k";

int shared_co2 = 0 , shared_temp = 0, shared_status = 0;

char getchecksum(char *str) {
        int i, checksum = 0;

        for (i = 1; i < 8; i++) {
                checksum += str[i];
        }

        checksum = 0xFF - checksum;
        checksum += 1;

        return checksum;
}

static void write_to_sensor(void *arg) {
	t = mgos_dht_get_temp(s_dht);
	h = mgos_dht_get_humidity(s_dht);

	mgos_shadow_updatef(0, "{sensor: {temperature: %.1f, humidity: %.1f}}", t, h);
	mgos_uart_write(UART_NO, (char *)arg, SCMD_LEN);
}

static void sensor_handler(struct mg_connection *c, int ev, void *p, void *user_data) {
  int co2 = 0, temp = 0, status = 0;
  char *sbuf = NULL;

  if (ev == MG_EV_HTTP_REQUEST) {
    struct http_message *hm = (struct http_message *) p;
    struct mg_str *s = hm->body.len > 0 ? &hm->body : &hm->query_string;
    sbuf = calloc(sizeof(char), (int)s->len  + 1);

    /* do we need mutex lock and var copies here? */
    co2 = shared_co2;
    temp = shared_temp;
    status = shared_status;

    memcpy(sbuf, s->p, (int)s->len);
    sbuf[(int)s->len] = '\0';

    /* abc enable */
    if (strcmp(abc_en_cmd, sbuf) == 0) {
        write_to_sensor((void *)abc_enable);
        mg_printf(c, "HTTP/1.0 200 OK\n\nEnabled auto correction baseline\n");
    }

    /* abc disable */
    else if (strcmp(abc_dis_cmd, sbuf) == 0) {
        write_to_sensor((void *)abc_disable);
        mg_printf(c, "HTTP/1.0 200 OK\n\nDisabled auto correction baseline\n");
    }

    /* read sensor values */
    else if (strcmp(s_read_cmd, sbuf) == 0) {
        write_to_sensor((void *)read_value);
        mg_printf(c, "HTTP/1.0 200 OK\n\nPerformed a read request from the sensor\n");
    }

    /* zero calibration */
    else if (strcmp(cal_zero_cmd, sbuf) == 0) {
        write_to_sensor((void *)cal_zero);
	mg_printf(c, "HTTP/1.0 200 OK\n\nReset the baseline to 400ppm\n");
    }

    /* calibrate span point of 2000ppm */
    else if (strcmp(cal_span2k_cmd, sbuf) == 0) {
        write_to_sensor((void *)cal_span2k);
        mg_printf(c, "HTTP/1.0 200 OK\n\nSet span point to 2000ppm\n");
    }

    /* sensor reset */
    else if (strcmp(s_rst_cmd, sbuf) == 0) {
        write_to_sensor((void *)sensor_reset);
        mg_printf(c, "HTTP/1.0 200 OK\n\nPerformed a reset of the sensor\n");
    }

    /* set range to 2000ppm */
    else if (strcmp(set_range2k_cmd, sbuf) == 0) {
        write_to_sensor((void *)set_range2k);
        mg_printf(c, "HTTP/1.0 200 OK\n\nSet sensor range to 2000ppm\n");
    }

    /* set range to 5000ppm */
    else if (strcmp(set_range5k_cmd, sbuf) == 0) {
        write_to_sensor((void *)set_range5k);
        mg_printf(c, "HTTP/1.0 200 OK\n\nSet sensor range to 5000ppm\n");
    }

    /* default, print the co2 ppm, temp in c and status byte (64 means valid readout) */
    else {
        mg_printf(c, "HTTP/1.0 200 OK\n\n{ \"co2\": %d, \"temp\": %d, \"status\": %d, temp2: %.1f, humid: %.1f }\n", co2, temp, status, t, h);
    }
    c->flags |= MG_F_SEND_AND_CLOSE;
    free(sbuf);
  }
  (void) user_data;
}

static void uart_dispatcher(int uart_no, void *arg) {
  assert(uart_no == UART_NO);
  size_t rx_av = mgos_uart_read_avail(uart_no);
  if (rx_av > 0) {
    struct mbuf rxb;
    mbuf_init(&rxb, 0);
    mgos_uart_read_mbuf(uart_no, &rxb, rx_av);
    if (rxb.len ==  SCMD_LEN) {
    	if (getchecksum(rxb.buf) == rxb.buf[8]) {
        	shared_co2 = (256 * rxb.buf[2]) + rxb.buf[3];
        	shared_temp = rxb.buf[4] - 37;
	        shared_status = rxb.buf[5];

		mgos_shadow_updatef(0, "{sensor: {co2: %d, temp: %d, status: %d}}", (256 * rxb.buf[2]) + rxb.buf[3], rxb.buf[4] - 37, rxb.buf[5]);

		/*
        	printf("co2: %d ppm\n", (256 * rxb.buf[2]) + rxb.buf[3]);
        	printf("temp: %dc\n", rxb.buf[4] - 37);
		printf("status: %d\n", rxb.buf[5]);
		printf("temperature: %.1f\n", t);
		printf("humidity: %.1f\n", h);
		*/
    	} else {
		printf("crc checksum error\n");
	}
    }
    mbuf_free(&rxb);
  }
}

enum mgos_app_init_result mgos_app_init(void) {
  struct mgos_uart_config ucfg;
  
  mgos_uart_config_set_defaults(UART_NO, &ucfg);
  ucfg.baud_rate = 9600;
  
  if (!mgos_uart_configure(UART_NO, &ucfg)) {
    return MGOS_APP_INIT_ERROR;
  }

  if ((s_dht = mgos_dht_create(14, DHT22)) == NULL) {
        return MGOS_APP_INIT_ERROR;
  }

  mgos_set_timer(30000 /* ms */, true /* repeat */, write_to_sensor, (void *) read_value);

  mgos_uart_set_dispatcher(UART_NO, uart_dispatcher, NULL /* arg */);
  mgos_uart_set_rx_enabled(UART_NO, true);
  mgos_register_http_endpoint("/sensor", sensor_handler, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
