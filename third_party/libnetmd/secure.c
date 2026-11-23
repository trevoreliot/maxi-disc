/*
 * secure.c
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

/*
  Set of NetMD commands that start with the sequence
  0x00,0x18,0x00,0x08,0x00,0x46,0xf0,0x03,0x01,0x03

  These commands are used during check-in/check-out.
*/

#include <string.h>
#include <stdio.h>
#include <libusb.h>
#include <errno.h>
#include <gcrypt.h>

#include "secure.h"
#include "const.h"
#include "utils.h"
#include "log.h"
#include "trackinformation.h"


static const unsigned char secure_header[] = { 0x18, 0x00, 0x08, 0x00, 0x46,
                                               0xf0, 0x03, 0x01, 0x03 };

void build_request(unsigned char *request, const unsigned char cmd, unsigned char *data, const size_t data_size)
{
    size_t header_length;
    header_length = 1 + sizeof(secure_header);


    request[0] = NETMD_STATUS_CONTROL;

    memcpy(request + 1, secure_header, sizeof(secure_header));

    request[header_length] = cmd;
    request[header_length + 1] = 0xff;

    memcpy(request + header_length + 2, data, data_size);
}

netmd_error parse_netmd_return_status(unsigned char status, unsigned char expected)
{
    if (status == expected) {
        return NETMD_NO_ERROR;
    }

    switch (status) {
    case NETMD_STATUS_NOT_IMPLEMENTED:
        return NETMD_COMMAND_FAILED_NOT_IMPLEMENTED;

    case NETMD_STATUS_REJECTED:
        return NETMD_COMMAND_FAILED_REJECTED;

    case NETMD_STATUS_ACCEPTED:
        break;

    case NETMD_STATUS_IN_TRANSITION:
        break;

    case NETMD_STATUS_IMPLEMENTED:
        break;

    case NETMD_STATUS_CHANGED:
        break;

    case NETMD_STATUS_INTERIM:
        break;
    }

    return NETMD_COMMAND_FAILED_UNKNOWN_ERROR;
}

void netmd_send_secure_msg(netmd_dev_handle *dev, unsigned char cmd,
                           unsigned char *data, size_t data_size)
{
    unsigned char *request;
    size_t request_length;

    /* alloc memory */
    request_length = 1 + sizeof(secure_header) + 2 + data_size;
    request = malloc(request_length);

    /* build request */
    build_request(request, cmd, data, data_size);

    netmd_send_message(dev, request, request_length);

    /* free memory */
    free(request);
    request = NULL;
}

netmd_error netmd_recv_secure_msg(netmd_dev_handle *dev, unsigned char cmd,
                                  netmd_response *response,
                                  unsigned char expected_response_code)
{
    netmd_error error;
    int len;
    int interims = 0;

    /* recv response. music-md-manager patch: an INTERIM reply ("working on it", e.g. a commit
       while the recorder is still writing the track to the disc) is followed by the real one:
       wait for that, unless INTERIM is what's expected. Was reported as an unknown error. */
    int timeouts = 0;
    for (;;) {
        len = netmd_recv_message(dev, response->content);
        /* music-md-manager patch: a recorder still writing a track to the disc can take more
           than the ~30 s one receive waits before it answers (seen with "commit track" on an
           MZ-NE410): keep waiting, up to ~3 more minutes. */
        if (len == NETMDERR_TIMEOUT && timeouts++ < 6) {
            netmd_log(NETMD_LOG_ERROR, "no reply yet to secure command 0x%02x, still waiting (%d)\n", cmd, timeouts);
            continue;
        }
        if (len < 1) {
            netmd_log(NETMD_LOG_ERROR, "no reply to secure command 0x%02x (%d)\n", cmd, len);
            return NETMD_COMMAND_FAILED_NO_RESPONSE;
        }
        response->length = (size_t)len;
        response->position = 0;
        if (response->content[0] == NETMD_STATUS_INTERIM && expected_response_code != NETMD_STATUS_INTERIM
            && interims++ < 30) {
            netmd_log(NETMD_LOG_VERBOSE, "interim reply to secure command 0x%02x, waiting for the final one\n", cmd);
            continue;
        }
        break;
    }

    error = parse_netmd_return_status(response->content[0], expected_response_code);
    if (error != NETMD_NO_ERROR)
        netmd_log(NETMD_LOG_ERROR, "secure command 0x%02x: status 0x%02x, expected 0x%02x\n", cmd,
                  response->content[0], expected_response_code);
    response->position = 1;

    netmd_check_response_bulk(response, secure_header, sizeof(secure_header), &error);
    netmd_check_response(response, cmd, &error);
    if (cmd != 0x12) {
        netmd_check_response(response, 0x00, &error);
    }
    else {
        netmd_check_response(response, 0x01, &error);
    }

    return error;
}

/** Helper function to make life a little simpler for other netmd_secure_cmd_* functions */
netmd_error netmd_exch_secure_msg(netmd_dev_handle *dev, unsigned char cmd,
                                  unsigned char *data, size_t data_size,
                                  netmd_response *response)
{
    netmd_send_secure_msg(dev, cmd, data, data_size);
    return netmd_recv_secure_msg(dev, cmd, response, NETMD_STATUS_ACCEPTED);
}

/*
  => 00  18 00 08 00 46 f0 03 01 03  80 ff
  <= 09  18 00 08 00 46 f0 03 01 03  80 00
*/
netmd_error netmd_secure_enter_session(netmd_dev_handle *dev)
{
    netmd_response response;
    return netmd_exch_secure_msg(dev, 0x80, NULL, 0, &response);
}

/*
  => 00  18 00 08 00 46 f0 03 01 03  81 ff
  <= 09  18 00 08 00 46 f0 03 01 03  81 00
*/
netmd_error netmd_secure_leave_session(netmd_dev_handle *dev)
{
    netmd_response response;
    return netmd_exch_secure_msg(dev, 0x81, NULL, 0, &response);
}

/*
  => 00  18 00 08 00 46 f0 03 01 03  11 ff
  <= 09  18 00 08 00 46 f0 03 01 03  11 00  01 00 00 21 cf 06 00 00
*/
netmd_error netmd_secure_get_leaf_id(netmd_dev_handle *dev, uint64_t *player_id)
{
    netmd_response response;
    netmd_error error;

    error = netmd_exch_secure_msg(dev, 0x11, NULL, 0, &response);
    if (error == NETMD_NO_ERROR) {
        *player_id = netmd_read_quadword(&response);
    }

    return error;
}

uint16_t netmd_get_chain_length(netmd_keychain *chain)
{
    uint16_t length;

    length = 0;
    while (chain != NULL) {
        chain = chain->next;
        length++;
    }

    return length;
}

void netmd_build_send_key_data_command(unsigned char *buf, uint16_t databytes,
                                       uint16_t chain_length,
                                       uint32_t key_depth, uint32_t key_id,
                                       netmd_keychain *chain, char *signature)
{
    netmd_copy_word_to_buffer(&buf, databytes, 0);
    netmd_copy_word_to_buffer(&buf, 0, 0);
    netmd_copy_word_to_buffer(&buf, databytes, 0);
    netmd_copy_word_to_buffer(&buf, 0, 0);

    /* data */
    netmd_copy_word_to_buffer(&buf, chain_length, 0);
    netmd_copy_doubleword_to_buffer(&buf, key_depth, 0);
    netmd_copy_doubleword_to_buffer(&buf, key_id, 0);
    netmd_copy_doubleword_to_buffer(&buf, 0, 0);

    /* add all keys from the keychain to the buffer */
    while (chain != NULL) {
        memcpy(buf, chain->key, 16);
        buf += 16;

        chain = chain->next;
    }

    memcpy(buf, signature, 24);
}

/*
  => 00  18 00 08 00 46 f0 03 01 03  12 ff  00 38  00 00  00 38  00 00  00 01
     00 00 00 09  00 01 00 01  00 00 00 00  01 ca be 07 2c 4d a7 ae f3 6c 8d 73
     fa 60 2b d1 0f f4 7d 45 9c 72 da 81 85 16 9d 73 49 00 ff 6c 6a b9 61 6b 03
     04 f9 ce
  <= 09  18 00 08 00 46 f0 03 01 03  12 00  00 38  00 00  00 38  00 00
*/
netmd_error netmd_secure_send_key_data(netmd_dev_handle *dev, netmd_ekb* ekb)
{
    unsigned char *cmd;
    size_t size;
    uint16_t chain_length;
    netmd_response response;
    netmd_error error;

    chain_length = netmd_get_chain_length(ekb->chain);
    size = 22 + ((unsigned int)chain_length*16) + 24;

    cmd = malloc(size);
    netmd_build_send_key_data_command(cmd, (size - 6) & 0xffff, chain_length, ekb->depth,
                                      ekb->id, ekb->chain, ekb->signature);

    error = netmd_exch_secure_msg(dev, 0x12, cmd, size, &response);
    free(cmd);

    netmd_check_response_word(&response, (size - 6) & 0xffff, &error);
    netmd_check_response_word(&response, 0, &error);
    netmd_check_response_word(&response, (size - 6) & 0xffff, &error);

    return error;
}

netmd_error netmd_secure_session_key_exchange(netmd_dev_handle *dev,
                                              unsigned char *rand_in,
                                              unsigned char *rand_out)
{
    unsigned char cmdhdr[] = {0x00, 0x00, 0x00};
    unsigned char cmd[sizeof(cmdhdr) + 8];

    netmd_response response;
    netmd_error error;

    memcpy(cmd, cmdhdr, sizeof(cmdhdr));
    memcpy(cmd + sizeof(cmdhdr), rand_in, 8);

    error = netmd_exch_secure_msg(dev, 0x20, cmd, sizeof(cmd), &response);

    netmd_check_response(&response, 0x00, &error);
    netmd_check_response(&response, 0x00, &error);
    netmd_check_response(&response, 0x00, &error);
    netmd_read_response_bulk(&response, rand_out, 8, &error);

    return error;
}


netmd_error netmd_secure_session_key_forget(netmd_dev_handle *dev)
{
    unsigned char cmd[] = {0x00, 0x00, 0x00};
    netmd_response response;
    netmd_error error;

    error = netmd_exch_secure_msg(dev, 0x21, cmd, sizeof(cmd), &response);
    netmd_check_response_bulk(&response, cmd, sizeof(cmd), &error);
    return error;
}

netmd_error netmd_secure_setup_download(netmd_dev_handle *dev,
                                        unsigned char *contentid,
                                        unsigned char *key_encryption_key,
                                        unsigned char *sessionkey)
{
    unsigned char cmdhdr[] = { 0x00, 0x00 };
    unsigned char data[32] = { 0x01, 0x01, 0x01, 0x01 /* ... */};
    unsigned char cmd[sizeof(cmdhdr) + sizeof(data)];

    unsigned char iv[8] = { 0 };
    gcry_cipher_hd_t handle;

    netmd_response response;
    netmd_error error;

    memcpy(data + 4, contentid, 20);
    memcpy(data + 24, key_encryption_key, 8);

    gcry_cipher_open(&handle, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_CBC, 0);
    gcry_cipher_setkey(handle, sessionkey, 8);
    gcry_cipher_setiv(handle, iv, 8);
    gcry_cipher_encrypt(handle, data, sizeof(data), NULL, 0);
    gcry_cipher_close(handle);

    memcpy(cmd, cmdhdr, sizeof(cmdhdr));
    memcpy(cmd + sizeof(cmdhdr), data, 32);

    error = netmd_exch_secure_msg(dev, 0x22, cmd, sizeof(cmd), &response);
    netmd_check_response_bulk(&response, cmdhdr, sizeof(cmdhdr), &error);
    return error;
}


size_t netmd_get_frame_size(netmd_wireformat wireformat)
{
    switch (wireformat) {
    case NETMD_WIREFORMAT_PCM:
        return 2048;

    case NETMD_WIREFORMAT_LP2:
        return 192;
        break;

    case NETMD_WIREFORMAT_105KBPS:
        return 152;

    case NETMD_WIREFORMAT_LP4:
        return 96;
    }

    return 0;
}

/* music-md-manager patch: the packets go out in 64 KB bulk writes (was one write per ~8 MB
   packet), reporting progress after each and stopping on a USB error or when `progress` returns
   non-zero. Returns 0, a libusb error (< 0), or 1 if stopped by `progress`. */
#define NETMD_TRANSFER_CHUNK 65536
static int transfer_song_packets(netmd_dev_handle *dev,
                                 netmd_track_packets *packets,
                                 size_t full_length,
                                 netmd_progress_cb progress, void *user_data)
{
    netmd_track_packets *p;
    unsigned char *packet, *buf;
    size_t packet_size, offset, total = 0, sent = 0, piece;
    int error = 0;
    int transferred = 0;
    int first_packet = 1;

    for (p = packets; p != NULL; p = p->next)
        total += p->length;
    total += 24;

    p = packets;
    while (p != NULL) {
        /* length + key + iv + data */
        if(first_packet)                                     // length, key and iv in first packet only
            packet_size = 8 + 8 + 8 + p->length;
        else
            packet_size = p->length;

        packet = malloc(packet_size);
        if (packet == NULL) {
            error = LIBUSB_ERROR_NO_MEM;
            break;
        }
        buf = packet;

        /* build packet... */
        if(first_packet) {                                   // length, key and iv in first packet only
            netmd_copy_quadword_to_buffer(&buf, full_length);
            memcpy(buf, p->key, 8);
            memcpy(buf + 8, p->iv, 8);
            memcpy(buf + 16, p->data, p->length);
        }
        else {
            memcpy(buf, p->data, p->length);
        }

        /* ... send it, a piece at a time */
        for (offset = 0; offset < packet_size; offset += piece) {
            piece = packet_size - offset;
            if (piece > NETMD_TRANSFER_CHUNK)
                piece = NETMD_TRANSFER_CHUNK;
            error = libusb_bulk_transfer((libusb_device_handle*)dev, 2, packet + offset, (int)piece, &transferred, 30000);
            if (error < 0 || (size_t)transferred != piece) {
                netmd_log(NETMD_LOG_ERROR, "bulk transfer failed: %d (%d of %d bytes)\n", error, transferred, (int)piece);
                if (error >= 0)
                    error = LIBUSB_ERROR_IO;
                break;
            }
            sent += piece;
            if (progress && progress(sent, total, user_data) != 0) {
                error = 1;
                break;
            }
        }

        /* cleanup */
        free(packet);
        buf = NULL;

        if (error != 0)
            break;
        p = p->next;
        first_packet = 0;
    }
    return error;
}

void netmd_transfer_song_packets(netmd_dev_handle *dev,
                                 netmd_track_packets *packets,
                                 size_t full_length)
{
    transfer_song_packets(dev, packets, full_length, NULL, NULL);
}

netmd_error netmd_prepare_packets(unsigned char* data, size_t data_lenght,
                                  netmd_track_packets **packets,
                                  size_t *packet_count, size_t *frames, size_t channels, size_t *packet_length,
                                  unsigned char *key_encryption_key, netmd_wireformat format)
{
    size_t position = 0;
    size_t chunksize, packet_data_length, first_chunk = 0x00800000U;     // limit chunksize to multiple of 16384 bytes (incl. 24 byte header data for first packet)
    size_t frame_size = netmd_get_frame_size(format);
    int padding = 0;
    netmd_track_packets *last = NULL;
    netmd_track_packets *next = NULL;

    gcry_cipher_hd_t key_handle;
    gcry_cipher_hd_t data_handle;
    unsigned char iv[8] = { 0 };
    unsigned char rand[8] = { 0 };
    unsigned char key[8] = { 0 };

    netmd_error error = NETMD_NO_ERROR;

    if(channels == NETMD_CHANNELS_MONO)
        frame_size /= 2;

    gcry_cipher_open(&key_handle, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_open(&data_handle, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_CBC, 0);
    gcry_cipher_setkey(key_handle, key_encryption_key, 8);


    /* generate initial iv */
    gcry_create_nonce(iv, sizeof(iv));

    /* generate key, use same key for all packets */
    gcry_randomize(rand, sizeof(rand), GCRY_STRONG_RANDOM);
    gcry_cipher_decrypt(key_handle, key, 8, rand, sizeof(rand));

    *packet_count = 0;
    while (position < data_lenght) {

        /* decrease chunksize by 24 (length, iv and key) for 1st packet */
        if ((*packet_count) > 0)
            chunksize = first_chunk;
        else
            chunksize = first_chunk - 24U;

       packet_data_length = chunksize;

        if ((data_lenght - position) < chunksize) {                  // last packet
            packet_data_length = data_lenght - position;             // do not encrypt padding bytes
            /* adjust size for DES encryption, should not happen if input file is not corrupt, ensure buffer for input file is large enough */
            if((packet_data_length % 8) != 0) {
                padding = 8 - (packet_data_length % 8);
                packet_data_length += padding;
            }
            /* do not truncate if last frame is incomplete, include padding bytes for DES encryption in size calculation */
            if((data_lenght % frame_size) != 0 || padding != 0) {
                padding = frame_size - (data_lenght % frame_size) - padding;
                if(padding < 0)
                    padding += frame_size;
            }
            chunksize = packet_data_length + padding;
        }

        /* alloc memory */
        next = malloc(sizeof(netmd_track_packets));
        next->length = chunksize;
        next->data = malloc(next->length);
        memset(next->data, 0, next->length);
        next->iv = malloc(8);
        next->key = malloc(8);
        next->next = NULL;

        /* linked list */
        if (last != NULL) {
            last->next = next;
        }
        else {
            *packets = next;
        }

        /* crypt data */
        memcpy(next->iv, iv, 8);
        memcpy(next->key, key, 8);
        gcry_cipher_setiv(data_handle, iv, 8);
        gcry_cipher_setkey(data_handle, rand, sizeof(rand));
        gcry_cipher_encrypt(data_handle, next->data, chunksize, data + position, packet_data_length);
        /* set last encrypted block as iv for the next packet */
        memcpy(iv, data + position + packet_data_length - 8, 8);

        /* next packet */
        position = position + chunksize;
        (*packet_count)++;
        last = next;
        netmd_log(NETMD_LOG_VERBOSE, "generating packet %d : %d bytes\n", *packet_count, chunksize);
    }

    gcry_cipher_close(key_handle);
    gcry_cipher_close(data_handle);

    *frames = position/frame_size;
    *packet_length = position;

    return error;
}

void netmd_cleanup_packets(netmd_track_packets **packets)
{
    netmd_track_packets *current = *packets;
    netmd_track_packets *last;

    while (current != NULL) {
        last = current;
        current = last->next;

        free(last->data);
        free(last->iv);
        free(last->key);
        free(last);
        last = NULL;
    }
}

netmd_error netmd_secure_send_track(netmd_dev_handle *dev,
                                    netmd_wireformat wireformat,
                                    unsigned char discformat,
                                    unsigned int frames,
                                    netmd_track_packets *packets,
                                    size_t packet_length,
                                    unsigned char *sessionkey,

                                    uint16_t *track, unsigned char *uuid,
                                    unsigned char *content_id)
{
    return netmd_secure_send_track_ex(dev, wireformat, discformat, frames, packets, packet_length,
                                      sessionkey, track, uuid, content_id, NULL, NULL);
}

/* music-md-manager patch: netmd_secure_send_track with progress (see transfer_song_packets). */
netmd_error netmd_secure_send_track_ex(netmd_dev_handle *dev,
                                       netmd_wireformat wireformat,
                                       unsigned char discformat,
                                       unsigned int frames,
                                       netmd_track_packets *packets,
                                       size_t packet_length,
                                       unsigned char *sessionkey,
                                       uint16_t *track, unsigned char *uuid,
                                       unsigned char *content_id,
                                       netmd_progress_cb progress, void *user_data)
{
    int transfer_error;
    unsigned char cmdhdr[] = {0x00, 0x01, 0x00, 0x10, 0x01};
    unsigned char cmd[sizeof(cmdhdr) + 13];
    unsigned char *buf;
    size_t totalbytes;

    netmd_response response;
    netmd_error error;

    gcry_cipher_hd_t handle;
    unsigned char encryptedreply[32] = { 0 };
    unsigned char iv[8] = { 0 };

    memcpy(cmd, cmdhdr, sizeof(cmdhdr));
    buf = cmd + sizeof(cmdhdr);
    netmd_copy_word_to_buffer(&buf, 0xffffU, 0);
    *(buf++) = 0;
    *(buf++) = wireformat & 0xffU;
    *(buf++) = discformat & 0xffU;
    netmd_copy_doubleword_to_buffer(&buf, frames, 0);

    totalbytes = netmd_get_frame_size(wireformat) * frames + 24U;
    netmd_log(NETMD_LOG_VERBOSE, "total transfer size : %d bytes, %d frames of %d bytes\n", totalbytes, frames, netmd_get_frame_size(wireformat));
    netmd_copy_doubleword_to_buffer(&buf, totalbytes, 0);

    netmd_send_secure_msg(dev, 0x28, cmd, sizeof(cmd));
    error = netmd_recv_secure_msg(dev, 0x28, &response, NETMD_STATUS_INTERIM);
    netmd_check_response_bulk(&response, cmdhdr, sizeof(cmdhdr), &error);
    netmd_read_response_bulk(&response, NULL, 2, &error);
    netmd_check_response(&response, 0x00, &error);

    if (error == NETMD_NO_ERROR) {
        transfer_error = transfer_song_packets(dev, packets, packet_length, progress, user_data);
        if (transfer_error != 0) /* music-md-manager patch: was ignored (then waited for an answer) */
            return transfer_error == 1 ? NETMD_ERROR : NETMD_USB_ERROR;

        error = netmd_recv_secure_msg(dev, 0x28, &response, NETMD_STATUS_ACCEPTED);
        netmd_check_response_bulk(&response, cmdhdr, sizeof(cmdhdr), &error);
        *track = netmd_read_word(&response);
        netmd_check_response(&response, 0x00, &error);
        netmd_read_response_bulk(&response, NULL, 10, &error);
        netmd_read_response_bulk(&response, encryptedreply,
                                 sizeof(encryptedreply), &error);
    }

    if (error == NETMD_NO_ERROR) {
        gcry_cipher_open(&handle, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_CBC, 0);
        gcry_cipher_setiv(handle, iv, 8);
        gcry_cipher_setkey(handle, sessionkey, 8);
        gcry_cipher_decrypt(handle, encryptedreply, sizeof(encryptedreply), NULL, 0);
        gcry_cipher_close(handle);
        
        memcpy(uuid, encryptedreply, 8);
        memcpy(content_id, encryptedreply + 12, 20);
    }

    return error;
}

netmd_error netmd_secure_real_recv_track(netmd_dev_handle *dev, uint32_t length, FILE *file, size_t chunksize)
{
    uint32_t done = 0;
    unsigned char *data;
    int status;
    netmd_error error = NETMD_NO_ERROR;
    int transferred = 0;

    data = malloc(chunksize);
    while (done < length) {
        if ((length - done) < chunksize) {
            chunksize = length - done;
        }

        status = libusb_bulk_transfer((libusb_device_handle*)dev, 0x81, data, (int)chunksize, &transferred, 10000);

        if (status >= 0) {
            done += transferred;
            fwrite(data, transferred, 1, file);

            netmd_log(NETMD_LOG_DEBUG, "%.1f%%\n", (double)done/(double)length * 100);
        }
        else if (status != -LIBUSB_ERROR_TIMEOUT) {
            error = NETMD_USB_ERROR;
        }
    }
    free(data);

    return error;
}

uint8_t netmd_get_channel_count(unsigned char channel)
{
    if (channel == NETMD_CHANNELS_MONO) {
        return 1;
    }
    else if (channel == NETMD_CHANNELS_STEREO) {
        return 2;
    }
    else {
        return 0;
    }
}

void netmd_write_aea_header(char *name, uint32_t frames, unsigned char channel, FILE* f)
{
    unsigned char header[2048] = { 0 };
    unsigned char *buf;

    buf = header;
    netmd_copy_doubleword_to_buffer(&buf, 2048, 1);
    strncpy((char *)buf, name, 255);
    buf += 256;

    netmd_copy_doubleword_to_buffer(&buf, frames, 1);
    *(buf++) = netmd_get_channel_count(channel);
    *(buf++) = 0; /* flags */
    netmd_copy_doubleword_to_buffer(&buf, 0, 1);
    netmd_copy_doubleword_to_buffer(&buf, 0, 1); /* encrypted*/
    netmd_copy_doubleword_to_buffer(&buf, 0, 1); /*groupstart*/

    netmd_log_hex(NETMD_LOG_DEBUG, header, sizeof(header));
    fwrite(header, sizeof(header), 1, f);
}

void netmd_write_wav_header(unsigned char format, uint32_t bytes, FILE *f)
{
    unsigned char header[60] = { 0 };
    unsigned char *buf;
    unsigned char maskedformat;
    uint16_t bytespersecond;
    uint16_t bytesperframe;
    uint16_t jointstereo;

    maskedformat = format & 0x06;
    if (maskedformat == NETMD_DISKFORMAT_LP4) {
        bytesperframe = 96;
        jointstereo = 1;
    }
    else if (maskedformat == NETMD_DISKFORMAT_LP2) {
        bytesperframe = 192;
        jointstereo = 0;
    } else {
        netmd_log(NETMD_LOG_ERROR, "unknown disk format (format=%#02x, maskedformat=%#02x) in %s\n",
                  format, maskedformat, __func__);
        return;
    }
    bytespersecond = ((bytesperframe * 44100U) / 512U) & 0xffff;

    buf = header;

    /* RIFF header */
    memcpy(buf, "RIFF", 4);
    buf += 4;

    netmd_copy_doubleword_to_buffer(&buf, bytes + 60, 1);
    memcpy(buf, "WAVE", 4);
    buf += 4;

    /* fmt chunk - standard part */
    memcpy(buf, "fmt ", 4);
    buf += 4;
    netmd_copy_doubleword_to_buffer(&buf, 32, 1);
    netmd_copy_word_to_buffer(&buf, NETMD_RIFF_FORMAT_TAG_ATRAC3, 1);
    netmd_copy_word_to_buffer(&buf, 2, 1);
    netmd_copy_doubleword_to_buffer(&buf, 44100, 1);
    netmd_copy_doubleword_to_buffer(&buf, bytespersecond, 1);
    netmd_copy_word_to_buffer(&buf, (2U*bytesperframe) & 0xffff, 1);
    netmd_copy_word_to_buffer(&buf, 0, 1);

    /* fmt chunk - ATRAC extension */
    netmd_copy_word_to_buffer(&buf, 14, 1);
    netmd_copy_word_to_buffer(&buf, 1, 1);
    netmd_copy_doubleword_to_buffer(&buf, bytesperframe, 1);
    netmd_copy_word_to_buffer(&buf, jointstereo, 1);
    netmd_copy_word_to_buffer(&buf, jointstereo, 1);
    netmd_copy_word_to_buffer(&buf, 1, 1);
    netmd_copy_word_to_buffer(&buf, 0, 1);

    /* data */
    memcpy(buf, "data", 4);
    buf += 4;
    netmd_copy_doubleword_to_buffer(&buf, bytes, 1);

    netmd_log_hex(NETMD_LOG_DEBUG, header, sizeof(header));
    fwrite(header, sizeof(header), 1, f);
}

netmd_error netmd_secure_recv_track(netmd_dev_handle *dev, uint16_t track,
                                    FILE* file)
{
    unsigned char cmdhdr[] = {0x00, 0x10, 0x01};
    unsigned char cmd[sizeof(cmdhdr) + sizeof(track)] = { 0 };
    unsigned char *buf;
    unsigned char encoding;
    unsigned char channel;
    char name[257] = { 0 };
    unsigned char codec;
    uint32_t length;
    uint16_t track_id;

    netmd_response response;
    netmd_error error;

    buf = cmd;
    memcpy(buf, cmdhdr, sizeof(cmdhdr));
    buf += sizeof(cmdhdr);
    netmd_copy_word_to_buffer(&buf, track, 0);

    track_id = (track - 1U) & 0xffff;;
    netmd_request_track_bitrate(dev, track_id, &encoding, &channel);

    if (encoding == NETMD_ENCODING_SP) {
        netmd_request_title(dev, track_id, name, sizeof(name) - 1);
    }
    else {
    }

    netmd_send_secure_msg(dev, 0x30, cmd, sizeof(cmd));
    error = netmd_recv_secure_msg(dev, 0x30, &response, NETMD_STATUS_INTERIM);
    netmd_check_response_bulk(&response, cmdhdr, sizeof(cmdhdr), &error);
    netmd_check_response_word(&response, track, &error);
    codec = netmd_read(&response);
    length = netmd_read_doubleword(&response);

    if (encoding == NETMD_ENCODING_SP) {
        netmd_write_aea_header(name, codec, channel, file);
    }
    else {
        netmd_write_wav_header(codec, length, file);
    }

    if (error == NETMD_NO_ERROR) {
        error = netmd_secure_real_recv_track(dev, length, file, 0x10000);
    }

    if (error == NETMD_NO_ERROR) {
        error = netmd_recv_secure_msg(dev, 0x30, &response, NETMD_STATUS_ACCEPTED);
        netmd_check_response_bulk(&response, cmdhdr, sizeof(cmdhdr), &error);
        netmd_read_response_bulk(&response, NULL, 2, &error);
        netmd_check_response_word(&response, 0, &error);
    }

    return error;
}

netmd_error netmd_secure_commit_track(netmd_dev_handle *dev, uint16_t track,
                                      unsigned char* sessionkey)
{
    unsigned char cmdhdr[] = {0x00, 0x10, 0x01};
    unsigned char cmd[sizeof(cmdhdr) + sizeof(track) + 8];
    unsigned char *buf;

    gcry_cipher_hd_t handle;
    unsigned char hash[8] = { 0 };

    netmd_response response;
    netmd_error error;

    buf = cmd;
    memcpy(buf, cmdhdr, sizeof(cmdhdr));
    buf += sizeof(cmdhdr);
    netmd_copy_word_to_buffer(&buf, track, 0);

    gcry_cipher_open(&handle, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_setkey(handle, sessionkey, 8);
    gcry_cipher_encrypt(handle, buf, sizeof(hash), hash, sizeof(hash));
    buf += sizeof(hash);
    gcry_cipher_close(handle);

    error = netmd_exch_secure_msg(dev, 0x48, cmd, sizeof(cmd), &response);
    netmd_check_response_bulk(&response, cmdhdr, sizeof(cmdhdr), &error);
    netmd_check_response_word(&response, track, &error);

    return error;
}

netmd_error netmd_secure_get_track_uuid(netmd_dev_handle *dev, uint16_t track,
                                        unsigned char *uuid)
{
    unsigned char cmdhdr[] = {0x10, 0x01};
    unsigned char cmd[sizeof(cmdhdr) + sizeof(track)];

    netmd_response response;
    netmd_error error;

    memcpy(cmd, cmdhdr, sizeof(cmdhdr));

    uint16_t tmp = track >> 8;
    cmd[sizeof(cmdhdr)] = tmp & 0xffU;
    cmd[sizeof(cmdhdr) + 1] = track & 0xffU;

    error = netmd_exch_secure_msg(dev, 0x23, cmd, sizeof(cmd), &response);
    netmd_check_response_bulk(&response, cmd, sizeof(cmd), &error);
    netmd_read_response_bulk(&response, uuid, 8, &error);

    return error;
}

netmd_error netmd_secure_delete_track(netmd_dev_handle *dev, uint16_t track,
                                      unsigned char *signature)
{
    unsigned char cmdhdr[] = {0x00, 0x10, 0x01};
    unsigned char cmd[sizeof(cmdhdr) + sizeof(track)];

    netmd_response response;
    netmd_error error;

    memcpy(cmd, cmdhdr, sizeof(cmdhdr));

    uint16_t tmp = track >> 8;
    cmd[sizeof(cmdhdr)] = tmp & 0xffU;
    cmd[sizeof(cmdhdr) + 1] = track & 0xffU;

    error = netmd_exch_secure_msg(dev, 0x40, cmd, sizeof(cmd), &response);
    netmd_check_response_bulk(&response, cmd, sizeof(cmd), &error);
    netmd_read_response_bulk(&response, signature, 8, &error);

    return error;
}

netmd_error netmd_secure_set_track_protection(netmd_dev_handle *dev,
                                              unsigned char mode)
{
    unsigned char cmdhdr[] = {0x00, 0x01, 0x00, 0x00};
    unsigned char cmd[sizeof(cmdhdr) + sizeof(mode)];

    netmd_response response;
    netmd_error error;

    memcpy(cmd, cmdhdr, sizeof(cmdhdr));
    cmd[sizeof(cmdhdr)] = mode;

    error = netmd_exch_secure_msg(dev, 0x2b, cmd, sizeof(cmd), &response);
    netmd_check_response_bulk(&response, cmd, sizeof(cmd), &error);

    return error;
}

/* music-md-manager patch: the host side of the session key exchange, from netmdcli
   (netmdcli.c, GPL-2.0-or-later, (C) 2002, 2003 Marc Britten, (C) 2017 René Rebe), so callers
   don't need libgcrypt themselves. */
static void init_gcrypt(void)
{
    static int done = 0;
    if (!done) {
        gcry_check_version(NULL);
        gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
        gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
        done = 1;
    }
}

void netmd_create_nonce(unsigned char *buffer, size_t length)
{
    init_gcrypt();
    gcry_create_nonce(buffer, length);
}

void netmd_retailmac(const unsigned char *rootkey, const unsigned char *hostnonce,
                     const unsigned char *devnonce, unsigned char *sessionkey)
{
    gcry_cipher_hd_t handle1;
    gcry_cipher_hd_t handle2;
    unsigned char des3_key[24] = { 0 };
    unsigned char iv[8] = { 0 };

    init_gcrypt();
    gcry_cipher_open(&handle1, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_setkey(handle1, rootkey, 8);
    gcry_cipher_encrypt(handle1, iv, 8, hostnonce, 8);

    memcpy(des3_key, rootkey, 16);
    memcpy(des3_key + 16, rootkey, 8);
    gcry_cipher_open(&handle2, GCRY_CIPHER_3DES, GCRY_CIPHER_MODE_CBC, 0);
    gcry_cipher_setkey(handle2, des3_key, 24);
    gcry_cipher_setiv(handle2, iv, 8);
    gcry_cipher_encrypt(handle2, sessionkey, 8, devnonce, 8);

    gcry_cipher_close(handle1);
    gcry_cipher_close(handle2);
}
