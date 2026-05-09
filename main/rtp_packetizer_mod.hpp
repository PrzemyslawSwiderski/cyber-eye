#pragma once

#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <arpa/inet.h>

static constexpr uint8_t RTP_VERSION = 2;
static constexpr uint8_t RTP_PAYLOAD_H264 = 96;
static constexpr uint16_t RTP_DEFAULT_MTU = 1400;
static constexpr uint32_t RTP_CLOCK_RATE = 90000;
static constexpr size_t RTP_HEADER_SIZE = 12;

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
      : ssrc_(ssrc), sequence_number_(0), timestamp_(0),
        max_payload_size_(mtu > RTP_HEADER_SIZE ? mtu - RTP_HEADER_SIZE : 100)
  {
  }

  // timestamp_us: monotonic capture timestamp in microseconds (e.g. from esp_timer_get_time())
  std::vector<std::vector<uint8_t>> packetize(const uint8_t *data, size_t size, uint64_t timestamp_us)
  {
    std::vector<std::vector<uint8_t>> packets;
    if (!data || size == 0 || max_payload_size_ < 2)
      return packets;

    uint32_t new_ts = static_cast<uint32_t>((timestamp_us * RTP_CLOCK_RATE) / 1000000ULL);
    timestamp_ = (new_ts != timestamp_) ? new_ts : timestamp_ + 1;

    packets.reserve(size / max_payload_size_ + 2);
    processNALUnits(data, size, packets);
    return packets;
  }

  void resetSequence()
  {
    sequence_number_ = 0;
    timestamp_ = 0;
  }

private:
  // Returns pointer to the first byte of the next start code and sets sc_len,
  // or nullptr if none found in [p, end).
  static const uint8_t *findStartCode(const uint8_t *p, const uint8_t *end, uint8_t &sc_len)
  {
    while (p + 3 <= end)
    {
      p = static_cast<const uint8_t *>(memchr(p, 0x00, end - p));
      if (!p || p + 3 > end)
        return nullptr;

      if (p[1] == 0x00 && p[2] == 0x01)
      {
        sc_len = 3;
        return p;
      }
      if (p[1] == 0x00 && p[2] == 0x00 && p + 4 <= end && p[3] == 0x01)
      {
        sc_len = 4;
        return p;
      }
      ++p;
    }
    return nullptr;
  }

  void processNALUnits(const uint8_t *data, size_t size, std::vector<std::vector<uint8_t>> &packets)
  {
    const uint8_t *end = data + size;
    uint8_t sc_len = 0;

    const uint8_t *sc = findStartCode(data, end, sc_len);
    if (!sc)
      return;

    const uint8_t *nal = sc + sc_len;

    while (nal < end)
    {
      const uint8_t *next_sc = findStartCode(nal + 1, end, sc_len);
      const uint8_t *nal_end = next_sc ? next_sc : end; // last NAL reaches buffer end

      size_t nal_size = nal_end - nal;
      uint8_t nal_header = *nal;
      uint8_t nal_type = nal_header & 0x1F;
      bool is_last = (next_sc == nullptr);

      if (nal_type >= 1 && nal_type <= 23 && nal_size > 0)
      {
        if (nal_size <= max_payload_size_)
          packetizeSingle(nal, nal_size, is_last, packets);
        else
          packetizeFragmented(nal, nal_size, nal_header, is_last, packets);
      }

      if (!next_sc)
        break;
      nal = next_sc + sc_len;
    }
  }

  void packetizeSingle(const uint8_t *data, size_t size, bool marker,
                       std::vector<std::vector<uint8_t>> &packets)
  {
    std::vector<uint8_t> packet(RTP_HEADER_SIZE + size);
    writeRTPHeader(packet.data(), marker);
    memcpy(packet.data() + RTP_HEADER_SIZE, data, size);
    packets.push_back(std::move(packet));
  }

  void packetizeFragmented(const uint8_t *data, size_t size, uint8_t nal_header,
                           bool is_last_nal, std::vector<std::vector<uint8_t>> &packets)
  {
    static constexpr size_t FU_OVERHEAD = 2; // FU indicator + FU header

    const size_t fu_payload = max_payload_size_ - FU_OVERHEAD;
    const uint8_t *payload = data + 1; // skip NAL header byte
    size_t remaining = size - 1;
    bool first = true;

    while (remaining > 0)
    {
      size_t chunk = std::min(fu_payload, remaining);
      bool last_frag = (chunk >= remaining);

      std::vector<uint8_t> packet(RTP_HEADER_SIZE + FU_OVERHEAD + chunk);
      writeRTPHeader(packet.data(), last_frag && is_last_nal);

      packet[RTP_HEADER_SIZE] = (nal_header & 0xE0) | 28;
      packet[RTP_HEADER_SIZE + 1] = (first ? 0x80 : 0x00) | (last_frag ? 0x40 : 0x00) | (nal_header & 0x1F);

      memcpy(packet.data() + RTP_HEADER_SIZE + FU_OVERHEAD, payload, chunk);
      packets.push_back(std::move(packet));

      payload += chunk;
      remaining -= chunk;
      first = false;
    }
  }

  void writeRTPHeader(uint8_t *buf, bool marker)
  {
    auto *h = reinterpret_cast<RTPHeader *>(buf);
    h->version_padding_cc = RTP_VERSION << 6;
    h->marker_payload_type = (marker ? 0x80 : 0x00) | RTP_PAYLOAD_H264;
    h->sequence_number = htons(sequence_number_++);
    h->timestamp = htonl(timestamp_);
    h->ssrc = htonl(ssrc_);
  }

  uint32_t ssrc_;
  uint16_t sequence_number_;
  uint32_t timestamp_;
  size_t max_payload_size_;
};
