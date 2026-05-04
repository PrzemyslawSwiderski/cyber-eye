#pragma once

#include <cstring>
#include <cstdint>
#include <vector>
#include <chrono>
#include <arpa/inet.h>

#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE_H264 96
#define RTP_DEFAULT_MTU 1400
#define RTP_H264_CLOCK_RATE 90000 // H.264 fixed clock rate

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
      : ssrc_(ssrc), sequence_number_(0), mtu_(mtu), max_payload_size_((mtu_ > 12) ? (mtu_ - 12) : 100), start_time_(std::chrono::steady_clock::now())
  {
  }

  // Main packetize method - timestamp automatically from system time
  std::vector<std::vector<uint8_t>> packetize(const uint8_t *h264_data, size_t size)
  {
    std::vector<std::vector<uint8_t>> packets;

    if (!h264_data || size == 0 || max_payload_size_ < 2)
    {
      return packets;
    }

    // Update timestamp based on elapsed time
    updateTimestamp();

    // Extract and packetize NAL units
    std::vector<NALUnit> nal_units;
    if (extractNALUnits(h264_data, size, nal_units))
    {
      for (size_t i = 0; i < nal_units.size(); i++)
      {
        bool is_last_nal = (i == nal_units.size() - 1);

        if (nal_units[i].data_size + 1 <= max_payload_size_)
        {
          createSingleNALPacket(nal_units[i], is_last_nal, packets);
        }
        else if (nal_units[i].data_size > 1)
        {
          createFragmentedPacket(nal_units[i], is_last_nal, packets);
        }
      }
    }

    return packets;
  }

  void resetSequence()
  {
    sequence_number_ = 0;
    start_time_ = std::chrono::steady_clock::now();
    timestamp_ = 0;
  }


private:
  struct NALUnit
  {
    const uint8_t *data;
    size_t data_size;
    uint8_t nal_header;
    uint8_t nal_type;
  };

  void updateTimestamp()
  {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();

    // Convert microseconds to 90kHz clock
    uint32_t new_timestamp = (elapsed_us * RTP_H264_CLOCK_RATE) / 1000000;

    // Ensure timestamp always increases
    if (new_timestamp > timestamp_)
    {
      timestamp_ = new_timestamp;
    }
    else if (new_timestamp == timestamp_)
    {
      timestamp_++; // Prevent duplicate timestamps
    }
  }

  bool extractNALUnits(const uint8_t *data, size_t size, std::vector<NALUnit> &nal_units)
  {
    size_t pos = 0;

    while (pos < size)
    {
      // Find start code (0x000001 or 0x00000001)
      size_t start_code_pos = pos;
      uint8_t start_code_len = 0;

      while (start_code_pos < size - 2)
      {
        if (data[start_code_pos] == 0 && data[start_code_pos + 1] == 0)
        {
          if (start_code_pos + 2 < size && data[start_code_pos + 2] == 1)
          {
            start_code_len = 3;
            break;
          }
          if (start_code_pos + 3 < size && data[start_code_pos + 2] == 0 &&
              data[start_code_pos + 3] == 1)
          {
            start_code_len = 4;
            break;
          }
        }
        start_code_pos++;
      }

      if (start_code_len == 0)
        break;

      size_t nal_start = start_code_pos + start_code_len;
      if (nal_start >= size)
        break;

      // Find next start code
      size_t nal_end = nal_start;
      while (nal_end < size - 2)
      {
        if (data[nal_end] == 0 && data[nal_end + 1] == 0)
        {
          if (nal_end + 2 < size && data[nal_end + 2] == 1)
            break;
          if (nal_end + 3 < size && data[nal_end + 2] == 0 && data[nal_end + 3] == 1)
            break;
        }
        nal_end++;
      }

      size_t nal_size = nal_end - nal_start;
      if (nal_size > 0)
      {
        NALUnit nal;
        nal.data = data + nal_start;
        nal.data_size = nal_size;
        nal.nal_header = data[nal_start];
        nal.nal_type = nal.nal_header & 0x1F;

        // Only include valid NAL types (1-23 are video data)
        if (nal.nal_type >= 1 && nal.nal_type <= 23)
        {
          nal_units.push_back(nal);
        }
      }

      pos = nal_end;
    }

    return !nal_units.empty();
  }

  void createSingleNALPacket(const NALUnit &nal, bool is_last_nal,
                             std::vector<std::vector<uint8_t>> &packets)
  {
    std::vector<uint8_t> packet(12 + nal.data_size);
    setRTPHeader(packet.data(), is_last_nal);
    memcpy(packet.data() + 12, nal.data, nal.data_size);
    packets.push_back(std::move(packet));
  }

  void createFragmentedPacket(const NALUnit &nal, bool is_last_nal,
                              std::vector<std::vector<uint8_t>> &packets)
  {
    size_t fu_payload_size = max_payload_size_ - 2;
    if (fu_payload_size == 0)
    {
      createSingleNALPacket(nal, is_last_nal, packets);
      return;
    }

    const uint8_t *data = nal.data + 1;
    size_t data_size = nal.data_size - 1;
    size_t offset = 0;
    bool first = true;

    while (offset < data_size)
    {
      size_t fragment_size = std::min(fu_payload_size, data_size - offset);
      bool last_fragment = (offset + fragment_size >= data_size);

      std::vector<uint8_t> packet(12 + 2 + fragment_size);
      bool marker = last_fragment && is_last_nal;
      setRTPHeader(packet.data(), marker);

      // FU indicator + FU header
      packet[12] = (nal.nal_header & 0xE0) | 28;
      packet[13] = (first ? 0x80 : 0x00) | (last_fragment ? 0x40 : 0x00) | nal.nal_type;

      memcpy(packet.data() + 14, data + offset, fragment_size);
      packets.push_back(std::move(packet));

      offset += fragment_size;
      first = false;
    }
  }

  void setRTPHeader(uint8_t *buffer, bool marker)
  {
    RTPHeader *header = (RTPHeader *)buffer;
    header->version_padding_cc = (RTP_VERSION << 6);
    header->marker_payload_type = (marker ? 0x80 : 0x00) | RTP_PAYLOAD_TYPE_H264;
    header->sequence_number = htons(sequence_number_++);
    header->timestamp = htonl(timestamp_);
    header->ssrc = htonl(ssrc_);
  }

  uint32_t ssrc_;
  uint16_t sequence_number_;
  uint32_t timestamp_ = 0;
  uint16_t mtu_;
  size_t max_payload_size_;
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
};
