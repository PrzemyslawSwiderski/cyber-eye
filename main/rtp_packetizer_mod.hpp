#pragma once

#include <cstring>
#include <cstdint>
#include <vector>

#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE_H264 96
#define RTP_MTU 1400 // Standard MTU for Ethernet

// RTP header structure (12 bytes)
struct __attribute__((packed)) RTPHeader
{
  uint8_t version_padding_cc;  // V=2, P=0, CC=0
  uint8_t marker_payload_type; // M=0 for first packet, PT=96
  uint16_t sequence_number;
  uint32_t timestamp;
  uint32_t ssrc;
};

// FU-A (Fragmentation Unit A) header for H264
struct __attribute__((packed)) FUHeader
{
  uint8_t start_bit; // 1 for first fragment, 0 for middle, last
  uint8_t end_bit;   // 1 for last fragment
  uint8_t nal_type;  // Original NAL unit type
};

class RTPPacketizer
{
public:
  RTPPacketizer(uint32_t ssrc = 0x12345678)
      : ssrc_(ssrc), sequence_number_(0), timestamp_(0)
  {
    srand(ssrc_);
  }

  // Packetize H264 frame into RTP packets
  std::vector<std::vector<uint8_t>> packetize(const uint8_t *h264_data, size_t size, uint32_t pts)
  {
    std::vector<std::vector<uint8_t>> packets;

    if (!h264_data || size == 0)
    {
      return packets;
    }

    // Set timestamp (in 90kHz clock for video)
    timestamp_ = pts * 90; // Convert to 90kHz clock

    // Find H264 NAL units (start codes: 0x00 0x00 0x01 or 0x00 0x00 0x00 0x01)
    size_t pos = 0;
    while (pos < size)
    {
      // Find next NAL unit
      uint8_t nal_header = 0;
      size_t nal_start = pos;

      // Skip start code
      if (pos + 3 < size && h264_data[pos] == 0x00 && h264_data[pos + 1] == 0x00 && h264_data[pos + 2] == 0x01)
      {
        pos += 3;
      }
      else if (pos + 4 < size && h264_data[pos] == 0x00 && h264_data[pos + 1] == 0x00 &&
               h264_data[pos + 2] == 0x00 && h264_data[pos + 3] == 0x01)
      {
        pos += 4;
      }
      else
      {
        // No start code found, break
        break;
      }

      // Get NAL unit header (first byte after start code)
      if (pos >= size)
        break;
      nal_header = h264_data[pos];

      // Get NAL unit type (lower 5 bits)
      uint8_t nal_type = nal_header & 0x1F;
      size_t nal_end = pos;

      // Find next start code
      bool found = false;
      for (size_t i = nal_end + 1; i < size - 3; i++)
      {
        if (h264_data[i] == 0x00 && h264_data[i + 1] == 0x00 &&
            (h264_data[i + 2] == 0x01 || h264_data[i + 2] == 0x00))
        {
          nal_end = i;
          found = true;
          break;
        }
      }
      if (!found)
      {
        nal_end = size;
      }

      // Process NAL unit
      size_t nal_size = nal_end - pos;
      size_t pos_in_nal = 0;

      // Single NAL unit packet (if it fits in MTU)
      if (nal_size + 12 <= RTP_MTU)
      {
        packets.push_back(createSingleNALPacket(h264_data + pos, nal_size, nal_type));
      }
      else
      {
        // Fragment the NAL unit using FU-A
        std::vector<std::vector<uint8_t>> fu_packets = createFragmentedPackets(
            h264_data + pos, nal_size, nal_type, nal_header);
        packets.insert(packets.end(), fu_packets.begin(), fu_packets.end());
      }

      pos = nal_end;
    }

    return packets;
  }

  void resetSequence()
  {
    sequence_number_ = 0;
  }

private:
  std::vector<uint8_t> createSingleNALPacket(const uint8_t *nal_data, size_t size, uint8_t nal_type)
  {
    std::vector<uint8_t> packet(RTP_HEADER_SIZE + size);

    // Set RTP header (without marker bit)
    setRTPHeader(packet.data(), false, RTP_PAYLOAD_TYPE_H264);

    // Copy NAL data (including NAL header)
    memcpy(packet.data() + RTP_HEADER_SIZE, nal_data, size);

    return packet;
  }

  std::vector<std::vector<uint8_t>> createFragmentedPackets(const uint8_t *nal_data, size_t size,
                                                            uint8_t nal_type, uint8_t nal_header)
  {
    std::vector<std::vector<uint8_t>> packets;

    // FU indicator (first byte of FU-A packet)
    uint8_t fu_indicator = (nal_header & 0xE0) | 28; // 28 = FU-A type

    // Data without NAL header
    const uint8_t *data = nal_data + 1;
    size_t data_size = size - 1;

    size_t offset = 0;
    bool first_packet = true;
    bool last_packet = false;
    int packet_count = 0;

    while (offset < data_size)
    {
      size_t fragment_size = RTP_MTU - RTP_HEADER_SIZE - 2; // 2 bytes for FU header
      if (offset + fragment_size >= data_size)
      {
        fragment_size = data_size - offset;
        last_packet = true;
      }

      // Create packet
      std::vector<uint8_t> packet(RTP_HEADER_SIZE + 2 + fragment_size);

      // Set RTP header with marker bit on last packet
      setRTPHeader(packet.data(), last_packet, RTP_PAYLOAD_TYPE_H264);

      // Set FU indicator
      packet[RTP_HEADER_SIZE] = fu_indicator;

      // Set FU header (S=start, E=end, nal_type)
      uint8_t fu_header = (first_packet ? 0x80 : 0x00) | (last_packet ? 0x40 : 0x00) | nal_type;
      packet[RTP_HEADER_SIZE + 1] = fu_header;

      // Copy fragment data
      memcpy(packet.data() + RTP_HEADER_SIZE + 2, data + offset, fragment_size);

      packets.push_back(std::move(packet));

      offset += fragment_size;
      first_packet = false;
      packet_count++;
    }

    return packets;
  }

  void setRTPHeader(uint8_t *buffer, bool marker, uint8_t payload_type)
  {
    RTPHeader *header = (RTPHeader *)buffer;

    // Version = 2, Padding = 0, Extension = 0, CSRC count = 0
    header->version_padding_cc = (RTP_VERSION << 6) | 0x00;

    // Marker bit (1 for last packet of frame), Payload type
    header->marker_payload_type = (marker ? 0x80 : 0x00) | payload_type;

    // Sequence number (network byte order)
    header->sequence_number = htons(sequence_number_++);

    // Timestamp (network byte order)
    header->timestamp = htonl(timestamp_);

    // SSRC (network byte order)
    header->ssrc = htonl(ssrc_);
  }

  static constexpr size_t RTP_HEADER_SIZE = 12;

  uint32_t ssrc_;
  uint16_t sequence_number_;
  uint32_t timestamp_;
};
