/*
 * common.c
 *
 * This file is part of libnetmd, a library for accessing Sony NetMD devices.
 *
 * Copyright (C) 2004 Bertrik Sikken
 * Copyright (C) 2011 Alexander Sulfrian
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include <unistd.h>

#include "common.h"
#include "const.h"
#include "log.h"

/* music-md-manager patch: were 1000 ms. A recorder busy writing the disc (an MZ-NE410 after
   "commit track") can take longer to answer a control transfer; libusb then cancels it, and the
   recorder stops answering until it's reset or replugged. Web MiniDisc waits without a limit. */
#define NETMD_POLL_TIMEOUT 20000	/* miliseconds */
#define NETMD_SEND_TIMEOUT 20000
#define NETMD_RECV_TIMEOUT 20000
#define NETMD_RECV_TRIES 30

#ifdef WIN32
    #include <windows.h>
    #define msleep(x) Sleep(x)
#else
    #define msleep(x) usleep(1000*x)
#endif

/*
  polls to see if minidisc wants to send data

  @param dev USB device handle
  @param buf pointer to poll buffer
  @param tries maximum attempts to poll the minidisc
  @return if error <0, else number of bytes that md wants to send
*/
static int netmd_poll(libusb_device_handle *dev, unsigned char *buf, int tries)
{
    int i;
    int result;

    for (i = 0; i < tries; i++) {
        /* send a poll message */
        memset(buf, 0, 4);

        result = libusb_control_transfer(dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
                            LIBUSB_RECIPIENT_INTERFACE, 0x01, 0, 0, buf, 4,
                            NETMD_POLL_TIMEOUT);
        if (result < 0) {
            netmd_set_last_usb_error(result);
            netmd_log(NETMD_LOG_ERROR, "netmd_poll: libusb_control_transfer failed: %s\n", libusb_error_name(result));
            return NETMDERR_USB;
        }

        if (buf[0] != 0) {
            break;
        }

        if (i > 0) {
            msleep(1000);
        }
    }

    return buf[2];
}


int netmd_exch_message(netmd_dev_handle *devh, unsigned char *cmd,
                       const size_t cmdlen, unsigned char *rsp)
{
    netmd_send_message(devh, cmd, cmdlen);
    return netmd_recv_message(devh, rsp);
}

/* music-md-manager patch 17 (see common.h). */
int netmd_exch_message_final(netmd_dev_handle *devh, unsigned char *cmd,
                             const size_t cmdlen, unsigned char *rsp)
{
    int len;
    int interims = 0;
    int timeouts = 0;

    len = netmd_send_message(devh, cmd, cmdlen);
    if (len < 0)
        return len;
    for (;;) {
        len = netmd_recv_message(devh, rsp);
        if (len == NETMDERR_TIMEOUT && timeouts++ < 3) {
            netmd_log(NETMD_LOG_ERROR, "no reply yet, still waiting (%d)\n", timeouts);
            continue;
        }
        if (len < 1)
            return len < 0 ? len : NETMDERR_USB;
        if (rsp[0] == NETMD_STATUS_INTERIM && interims++ < 30) {
            netmd_log(NETMD_LOG_VERBOSE, "interim reply, waiting for the final one\n");
            continue;
        }
        break;
    }
    if (rsp[0] == NETMD_STATUS_ACCEPTED)
        return len;
    /* Still working after all that: its answer would come as the reply to the next command.
       A timeout, so the caller starts over (reopens the device). */
    if (rsp[0] == NETMD_STATUS_INTERIM)
        return NETMDERR_TIMEOUT;
    if (cmdlen >= 3)
        netmd_log(NETMD_LOG_ERROR, "command 0x%02x 0x%02x: status 0x%02x\n", cmd[1], cmd[2], rsp[0]);
    return rsp[0] == NETMD_STATUS_REJECTED ? NETMDERR_CMD_INVALID : NETMDERR_CMD_FAILED;
}


int netmd_send_message(netmd_dev_handle *devh, unsigned char *cmd,
                       const size_t cmdlen)
{
    unsigned char pollbuf[4];
    int	len;
    libusb_device_handle *dev;

    dev = (libusb_device_handle *)devh;

    /* poll to see if we can send data */
    len = netmd_poll(dev, pollbuf, 1);
    if (len != 0) {
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: netmd_poll failed\n");
        return (len > 0) ? NETMDERR_NOTREADY : len;
    }

    /* send data */
    netmd_log(NETMD_LOG_DEBUG, "Command:\n");
    netmd_log_hex(NETMD_LOG_DEBUG, cmd, cmdlen);
    if (libusb_control_transfer(dev, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
                        LIBUSB_RECIPIENT_INTERFACE, 0x80, 0, 0, cmd, (int)cmdlen,
                        NETMD_SEND_TIMEOUT) < 0) {
        netmd_set_last_usb_error(LIBUSB_ERROR_IO);
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: libusb_control_transfer failed\n");
        return NETMDERR_USB;
    }

    return 0;
}

int netmd_recv_message(netmd_dev_handle *devh, unsigned char* rsp)
{
    int len;
    unsigned char pollbuf[4];
    libusb_device_handle *dev;

    dev = (libusb_device_handle *)devh;

    /* poll for data that minidisc wants to send */
    len = netmd_poll(dev, pollbuf, NETMD_RECV_TRIES);
    if (len <= 0) {
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: netmd_poll failed\n");
        return (len == 0) ? NETMDERR_TIMEOUT : len;
    }

    /* receive data */
    if (libusb_control_transfer(dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
                        LIBUSB_RECIPIENT_INTERFACE, pollbuf[1], 0, 0, rsp, len,
                        NETMD_RECV_TIMEOUT) < 0) {
        netmd_set_last_usb_error(LIBUSB_ERROR_IO);
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: libusb_control_transfer failed\n");
        return NETMDERR_USB;
    }

    netmd_log(NETMD_LOG_DEBUG, "Response:\n");
    netmd_log_hex(NETMD_LOG_DEBUG, rsp, (size_t)len);

    /* return length */
    return len;
}
