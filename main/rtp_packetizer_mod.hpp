/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include <cstring>
#include <cstdint>
#include <vector>
#include <arpa/inet.h>

#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE_H264 96
#define RTP_DEFAULT_MTU 1200

struct __attribute__((packed)) RTPHeader
{
  uint8_t version_padding_cc;
  uint8_t marker_payload_type;
  uint16_t sequence_number;
  uint32_t timestamp;
  uint32_t ssrc;
};

class RTPPacketizer
{
public:
  RTPPacketizer(uint32_t ssrc = 0x12345678, uint16_t mtu = RTP_DEFAULT_MTU)
      : ssrc_(ssrc), sequence_number_(0), timestamp_(0), timestamp_increment_(3000) // 90000/30 = 3000 for 30fps
        ,
        mtu_(mtu)
  {
  }

  void setFrameRate(int fps)
  {
    if (fps > 0)
    {
      timestamp_increment_ = 90000 / fps;
    }
  }

  void resetSequence()
  {
    sequence_number_ = 0;
    timestamp_ = 0;
  }

  std::vector<std::vector<uint8_t>> packetize(const uint8_t *h264_data, size_t size, uint32_t frame_number)
  {
    std::vector<std::vector<uint8_t>> packets;

    if (!h264_data || size == 0)
    {
      return packets;
    }

    // Increment timestamp for each frame
    timestamp_ += timestamp_increment_;

    // Process NAL units
    size_t pos = 0;
    while (pos < size)
    {
      // Find NAL start code
      while (pos < size && !(pos + 3 < size &&
                             h264_data[pos] == 0x00 && h264_data[pos + 1] == 0x00 &&
                             (h264_data[pos + 2] == 0x01 || h264_data[pos + 2] == 0x00)))
      {
        pos++;
      }

      if (pos >= size)
        break;

      // Find start code length
      size_t start_code_len = 3;
      if (pos + 3 < size && h264_data[pos + 2] == 0x00 && h264_data[pos + 3] == 0x01)
      {
        start_code_len = 4;
      }

      size_t nal_start = pos + start_code_len;
      if (nal_start >= size)
        break;

      // Find end of NAL
      size_t nal_end = nal_start;
      while (nal_end + 3 < size && !(h264_data[nal_end] == 0x00 &&
                                     h264_data[nal_end + 1] == 0x00 &&
                                     (h264_data[nal_end + 2] == 0x01 || h264_data[nal_end + 2] == 0x00)))
      {
        nal_end++;
      }

      size_t nal_size = nal_end - nal_start;
      uint8_t nal_header = h264_data[nal_start];
      uint8_t nal_type = nal_header & 0x1F;

      // Calculate max payload size
      size_t max_payload_size = mtu_ - 12; // RTP header is 12 bytes

      if (nal_size + 1 <= max_payload_size)
      {
        // Single NAL packet
        std::vector<uint8_t> packet(12 + nal_size);
        // Don't set marker for every packet, only last of frame
        bool marker = (nal_end >= size - 1); // Last NAL of frame
        setRTPHeader(packet.data(), marker, RTP_PAYLOAD_TYPE_H264);
        memcpy(packet.data() + 12, h264_data + nal_start, nal_size);
        packets.push_back(std::move(packet));
      }
      else
      {
        // FU-A fragmentation
        const uint8_t *data = h264_data + nal_start + 1;
        size_t data_size = nal_size - 1;
        size_t fragment_payload_size = max_payload_size - 2; // FU header is 2 bytes

        size_t offset = 0;
        bool first = true;
        bool last = false;

        while (offset < data_size)
        {
          size_t fragment_size = fragment_payload_size;
          if (offset + fragment_size >= data_size)
          {
            fragment_size = data_size - offset;
            last = true;
          }

          std::vector<uint8_t> packet(12 + 2 + fragment_size);
          // Only set marker on last fragment of last NAL of frame
          bool marker = last && (nal_end >= size - 1);
          setRTPHeader(packet.data(), marker, RTP_PAYLOAD_TYPE_H264);

          // FU indicator
          packet[12] = (nal_header & 0xE0) | 28;
          // FU header
          packet[13] = (first ? 0x80 : 0x00) | (last ? 0x40 : 0x00) | nal_type;

          memcpy(packet.data() + 14, data + offset, fragment_size);
          packets.push_back(std::move(packet));

          offset += fragment_size;
          first = false;
        }
      }

      pos = nal_end;
    }

    return packets;
  }

private:
  void setRTPHeader(uint8_t *buffer, bool marker, uint8_t payload_type)
  {
    RTPHeader *header = (RTPHeader *)buffer;
    header->version_padding_cc = (RTP_VERSION << 6);
    header->marker_payload_type = (marker ? 0x80 : 0x00) | (payload_type & 0x7F);
    header->sequence_number = htons(sequence_number_++);
    header->timestamp = htonl(timestamp_);
    header->ssrc = htonl(ssrc_);
  }

  uint32_t ssrc_;
  uint16_t sequence_number_;
  uint32_t timestamp_;
  uint32_t timestamp_increment_;
  uint16_t mtu_;
};
