#ifndef LIBNETMD_COMMON_H
#define LIBNETMD_COMMON_H

#include <libusb.h>

/**
   Typedef that nearly all netmd_* functions use to identify the USB connection
   with the minidisc player.
*/
typedef libusb_device_handle *netmd_dev_handle;

/**
  Function to exchange command/response buffer with minidisc player.

  @param dev device handle
  @param cmd buffer with the command, that should be send to the player
  @param cmdlen length of the command
  @param rsp buffer where the response should be written to
  @return number of bytes received if >0, or error if <0
*/
int netmd_exch_message(netmd_dev_handle *dev, unsigned char *cmd,
                       const size_t cmdlen, unsigned char *rsp);

/**
  Function to send a command to the minidisc player.

  @param dev device handle
  @param cmd buffer with the command, that should be send to the player
  @param cmdlen length of the command
*/
int netmd_send_message(netmd_dev_handle *dev, unsigned char *cmd,
                       const size_t cmdlen);


/**
  Function to recieve a response from the minidisc player.

  @param rsp buffer where the response should be written to
  @return number of bytes received if >0, or error if <0
*/
int netmd_recv_message(netmd_dev_handle *dev, unsigned char *rsp);

/* music-md-manager patch (netmd_dev.c): remembers a USB error for netmd_last_usb_error(). */
void netmd_set_last_usb_error(int error);

/* music-md-manager patch 17: sends a command and waits for its final reply, through INTERIM
   ("working on it") replies and a few receive timeouts, like netmd-js does. Returns the reply's
   length if the recorder ACCEPTED it, NETMDERR_CMD_INVALID if it REJECTED it (write-protected
   disc, protected track, ...), NETMDERR_CMD_FAILED for any other status (NOT IMPLEMENTED), or a
   negative transfer error (NETMDERR_USB, NETMDERR_TIMEOUT, NETMDERR_NOTREADY; also TIMEOUT if it
   still answers INTERIM after 30 replies). */
int netmd_exch_message_final(netmd_dev_handle *dev, unsigned char *cmd, const size_t cmdlen, unsigned char *rsp);


#endif
