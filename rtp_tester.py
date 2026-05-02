#!/usr/bin/env python3
"""
RTP H264 Stream Validator
Verifies if UDP stream contains valid RTP-encapsulated H264 video
"""

import socket
import struct
import time
import sys
from collections import defaultdict
from datetime import datetime

class RTPH264Validator:
    def __init__(self):
        self.packets_received = 0
        self.valid_rtp_packets = 0
        self.h264_found = False
        self.nal_types_found = set()
        self.sequence_numbers = []
        self.timestamps = []
        self.ssrcs = set()
        self.payload_types = set()
        self.packet_loss = 0
        self.out_of_order = 0
        self.fu_a_packets = 0  # Fragmentation Unit A (RTP fragmentation)
        self.stap_a_packets = 0  # Aggregation packets
        
        # NAL type meanings
        self.nal_meanings = {
            0: "Unspecified",
            1: "Coded slice (non-IDR)",
            2: "Coded slice data partition A",
            3: "Coded slice data partition B",
            4: "Coded slice data partition C",
            5: "Coded slice (IDR - keyframe)",
            6: "SEI (Supplemental Enhancement Info)",
            7: "SPS (Sequence Parameter Set)",
            8: "PPS (Picture Parameter Set)",
            9: "Access Unit Delimiter",
            10: "End of Sequence",
            11: "End of Stream",
            12: "Filler Data",
            13: "SPS Extension",
            14: "Prefix NAL",
            15: "Subset SPS",
            16: "Depth Parameter Set",
            24: "STAP-A (Single-Time Aggregation Packet)",
            25: "STAP-B",
            26: "MTAP16",
            27: "MTAP24",
            28: "FU-A (Fragmentation Unit A)",
            29: "FU-B"
        }
    
    def parse_rtp_header(self, data):
        """Parse RTP header and return header info and payload"""
        if len(data) < 12:
            return None, None
        
        # Parse RTP header
        first_byte = data[0]
        version = (first_byte >> 6) & 0x03
        padding = (first_byte >> 5) & 0x01
        extension = (first_byte >> 4) & 0x01
        csrc_count = first_byte & 0x0F
        
        second_byte = data[1]
        marker = (second_byte >> 7) & 0x01
        payload_type = second_byte & 0x7F
        
        sequence = struct.unpack('>H', data[2:4])[0]
        timestamp = struct.unpack('>I', data[4:8])[0]
        ssrc = struct.unpack('>I', data[8:12])[0]
        
        # Calculate header size
        header_size = 12 + (csrc_count * 4)
        if extension:
            if len(data) >= header_size + 4:
                extension_length = struct.unpack('>H', data[header_size+2:header_size+4])[0]
                header_size += 4 + (extension_length * 4)
        
        if len(data) <= header_size:
            return None, None
        
        payload = data[header_size:]
        
        return {
            'version': version,
            'padding': padding,
            'extension': extension,
            'csrc_count': csrc_count,
            'marker': marker,
            'payload_type': payload_type,
            'sequence': sequence,
            'timestamp': timestamp,
            'ssrc': ssrc
        }, payload
    
    def parse_h264_nal(self, data):
        """Parse H264 NAL units from RTP payload"""
        if len(data) < 1:
            return []
        
        nals = []
        pos = 0
        
        while pos < len(data):
            if pos + 1 > len(data):
                break
            
            nal_type = data[pos] & 0x1F
            
            # Check for aggregation packet (STAP-A)
            if nal_type == 24:  # STAP-A
                self.stap_a_packets += 1
                pos += 1
                while pos < len(data):
                    if pos + 2 > len(data):
                        break
                    nal_size = struct.unpack('>H', data[pos:pos+2])[0]
                    pos += 2
                    if pos + nal_size <= len(data):
                        nal = data[pos:pos+nal_size]
                        if len(nal) > 0:
                            inner_type = nal[0] & 0x1F
                            nals.append({
                                'type': inner_type,
                                'size': len(nal),
                                'data': nal,
                                'presentation': self.nal_meanings.get(inner_type, f"Unknown {inner_type}")
                            })
                        pos += nal_size
                    else:
                        break
            
            # Check for fragmentation unit (FU-A)
            elif nal_type == 28:  # FU-A
                self.fu_a_packets += 1
                if pos + 2 <= len(data):
                    fu_indicator = data[pos]
                    fu_header = data[pos + 1]
                    start_bit = (fu_header >> 7) & 0x01
                    end_bit = (fu_header >> 6) & 0x01
                    fu_type = fu_header & 0x1F
                    
                    if start_bit:
                        # This is the start of a fragmented NAL
                        # Reconstruct NAL header
                        nal_data = bytes([(fu_indicator & 0xE0) | fu_type]) + data[pos+2:]
                        nals.append({
                            'type': fu_type,
                            'size': len(nal_data),
                            'data': nal_data,
                            'presentation': self.nal_meanings.get(fu_type, f"Unknown {fu_type}"),
                            'fragmented': True,
                            'start_bit': start_bit,
                            'end_bit': end_bit
                        })
                    else:
                        # Continuation fragment - we'll just note it
                        nals.append({
                            'type': fu_type,
                            'size': len(data[pos+2:]),
                            'fragmented': True,
                            'start_bit': start_bit,
                            'end_bit': end_bit,
                            'presentation': f"FU-A fragment (type {fu_type})"
                        })
                    pos += 2  # Skip FU indicator and header
                else:
                    break
            
            # Single NAL unit packet
            else:
                nals.append({
                    'type': nal_type,
                    'size': len(data[pos:]),
                    'data': data[pos:],
                    'presentation': self.nal_meanings.get(nal_type, f"Unknown {nal_type}")
                })
                break  # Single NAL consumes rest of packet
        
        return nals
    
    def analyze_stream(self, host='0.0.0.0', port=3333, duration=10, buffer_size=65536):
        """Main method to analyze RTP H264 stream"""
        
        print(f"🔍 RTP H264 Stream Validator")
        print(f"📡 Listening on {host}:{port} for {duration} seconds...")
        print("=" * 70)
        
        # Create UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)  # 2MB buffer
        sock.settimeout(0.5)
        
        try:
            sock.bind((host, port))
        except Exception as e:
            print(f"❌ Failed to bind to {host}:{port}: {e}")
            return False
        
        start_time = time.time()
        last_sequence = defaultdict(int)
        first_packet_time = None
        
        print(f"⏳ Capturing packets...\n")
        
        try:
            while time.time() - start_time < duration:
                try:
                    data, addr = sock.recvfrom(buffer_size)
                    self.packets_received += 1
                    
                    if first_packet_time is None:
                        first_packet_time = time.time()
                    
                    # Parse RTP header
                    rtp_info, payload = self.parse_rtp_header(data)
                    
                    if rtp_info is None:
                        print(f"⚠️  Packet {self.packets_received}: Invalid RTP header")
                        continue
                    
                    if rtp_info['version'] != 2:
                        print(f"⚠️  Packet {self.packets_received}: Invalid RTP version {rtp_info['version']}")
                        continue
                    
                    self.valid_rtp_packets += 1
                    self.ssrcs.add(rtp_info['ssrc'])
                    self.payload_types.add(rtp_info['payload_type'])
                    self.sequence_numbers.append(rtp_info['sequence'])
                    self.timestamps.append(rtp_info['timestamp'])
                    
                    # Check for packet loss and reordering
                    ssrc = rtp_info['ssrc']
                    current_seq = rtp_info['sequence']
                    
                    if ssrc in last_sequence:
                        expected = (last_sequence[ssrc] + 1) & 0xFFFF
                        if current_seq != expected:
                            if current_seq > last_sequence[ssrc]:
                                gap = current_seq - expected
                                if gap > 0 and gap < 1000:  # Likely packet loss, not wrap
                                    self.packet_loss += gap
                                    print(f"📉 Packet loss detected: expected {expected}, got {current_seq} (lost {gap} packets)")
                                else:
                                    self.out_of_order += 1
                                    print(f"🔄 Out-of-order packet: {current_seq} after {last_sequence[ssrc]}")
                            else:
                                self.out_of_order += 1
                    
                    last_sequence[ssrc] = current_seq
                    
                    # Parse H264 NAL units
                    nals = self.parse_h264_nal(payload)
                    
                    if nals:
                        self.h264_found = True
                        for nal in nals:
                            if 'type' in nal:
                                self.nal_types_found.add(nal['type'])
                                
                                # Print special NALs
                                if nal['type'] in [7, 8, 5]:  # SPS, PPS, IDR
                                    print(f"🎬 Packet {self.packets_received}: {nal['presentation']} (type {nal['type']}) - {nal['size']} bytes")
                                    
                                # Print progress periodically
                                if self.packets_received % 100 == 0:
                                    print(f"📊 Progress: {self.packets_received} packets, {self.valid_rtp_packets} valid RTP, "
                                          f"H264 NALs: {sorted(self.nal_types_found)}")
                
                except socket.timeout:
                    continue
                except Exception as e:
                    print(f"❌ Error processing packet {self.packets_received}: {e}")
        
        except KeyboardInterrupt:
            print("\n⚠️  Stopped by user")
        finally:
            sock.close()
        
        # Print results
        self.print_results(duration)
        return self.is_valid()
    
    def print_results(self, duration):
        """Print analysis results"""
        print("\n" + "=" * 70)
        print("📊 ANALYSIS RESULTS")
        print("=" * 70)
        
        # Network statistics
        print(f"\n📦 Network Statistics:")
        print(f"   Total packets received: {self.packets_received}")
        print(f"   Valid RTP packets: {self.valid_rtp_packets}")
        
        if self.packets_received > 0:
            percentage = (self.valid_rtp_packets / self.packets_received) * 100
            print(f"   RTP validity: {percentage:.1f}%")
            
            # Calculate packet rate
            rate = self.packets_received / duration
            print(f"   Packet rate: {rate:.1f} pps")
        
        # RTP stream information
        print(f"\n🎯 RTP Stream Information:")
        print(f"   SSRCs found: {', '.join([hex(x) for x in self.ssrcs]) if self.ssrcs else 'None'}")
        print(f"   Payload types: {', '.join([str(x) for x in self.payload_types])}")
        
        # RTP payload type 96 is typical for dynamic H264
        if 96 in self.payload_types:
            print(f"   ✓ Payload type 96 (typical for H264) detected")
        
        if self.sequence_numbers:
            print(f"   Sequence range: {min(self.sequence_numbers)} - {max(self.sequence_numbers)}")
            print(f"   Packet loss: {self.packet_loss} packets")
            print(f"   Out-of-order: {self.out_of_order} packets")
        
        if self.timestamps:
            unique_ts = len(set(self.timestamps))
            print(f"   Unique timestamps: {unique_ts}")
            
            if len(self.timestamps) > 1:
                # Calculate timestamp frequency (90kHz for H264)
                ts_diffs = []
                for i in range(1, min(len(self.timestamps), 100)):
                    diff = (self.timestamps[i] - self.timestamps[i-1]) & 0xFFFFFFFF
                    if diff < 0x7FFFFFFF:  # Positive difference (not wrap)
                        ts_diffs.append(diff)
                
                if ts_diffs:
                    avg_diff = sum(ts_diffs) / len(ts_diffs)
                    # 90kHz clock for H264
                    frame_rate = 90000 / avg_diff if avg_diff > 0 else 0
                    print(f"   Estimated frame rate: {frame_rate:.1f} fps (using 90kHz clock)")
        
        # H264 information
        print(f"\n🎬 H264 Stream Information:")
        print(f"   H264 data found: {'✅ Yes' if self.h264_found else '❌ No'}")
        print(f"   FU-A (fragmented) packets: {self.fu_a_packets}")
        print(f"   STAP-A (aggregated) packets: {self.stap_a_packets}")
        
        if self.nal_types_found:
            print(f"\n   NAL unit types found: {sorted(self.nal_types_found)}")
            print(f"   NAL type details:")
            for nal_type in sorted(self.nal_types_found):
                meaning = self.nal_meanings.get(nal_type, f"Unknown")
                print(f"     • Type {nal_type}: {meaning}")
        else:
            print(f"   ⚠️  No H264 NAL units found")
        
        # Determine stream type
        print(f"\n📋 Stream Analysis:")
        
        # Check if this is likely H264 over RTP
        if 96 in self.payload_types and self.h264_found:
            print(f"   ✓ Dynamic payload type 96 with H264 content")
        
        # Check for fragmentation
        if self.fu_a_packets > 0:
            print(f"   ✓ RTP-level fragmentation (FU-A) detected - this handles packets > MTU correctly")
            print(f"     (Good: RTP fragmentation is better than IP fragmentation)")
        elif self.packets_received > 0 and max([len(str(seq)) for seq in self.sequence_numbers]) > 0:
            print(f"   ⚠️  No RTP fragmentation detected - could lead to IP fragmentation")
        
        print("\n" + "=" * 70)
    
    def is_valid(self):
        """Determine if the stream is valid RTP H264"""
        print("\n🔍 VALIDATION RESULT:")
        print("=" * 70)
        
        # Basic requirements
        has_rtp = self.valid_rtp_packets > 0
        has_h264 = self.h264_found
        has_sps = 7 in self.nal_types_found
        has_pps = 8 in self.nal_types_found
        has_idr = 5 in self.nal_types_found
        
        if not has_rtp:
            print("❌ INVALID: No valid RTP packets detected")
            print("   Make sure the stream uses RTP encapsulation")
            return False
        
        if not has_h264:
            print("❌ INVALID: No H264 data found in RTP payload")
            print("   Payload type might not be H264 or data is corrupted")
            return False
        
        # Check for completeness
        issues = []
        if not has_sps:
            issues.append("Missing SPS (Sequence Parameter Set) - required for decoding")
        if not has_pps:
            issues.append("Missing PPS (Picture Parameter Set) - required for decoding")
        if not has_idr:
            issues.append("No IDR frames found - playback may start with corruption")
        
        if not issues:
            print("✅ VALID: Complete RTP H264 stream detected")
            print(f"   ✓ Contains SPS, PPS, and IDR frames")
            if self.fu_a_packets > 0:
                print(f"   ✓ Uses RTP fragmentation (FU-A) for large frames")
            print(f"   ✓ Packet loss: {self.packet_loss} lost packets over {self.packets_received} total")
            return True
        else:
            print("⚠️  PARTIAL: RTP H264 stream detected but incomplete")
            for issue in issues:
                print(f"   • {issue}")
            
            if not has_sps and not has_pps and self.packets_received > 0:
                print(f"\n   💡 Suggestion: You might have joined mid-stream")
                print(f"      Wait longer (SPS/PPS are often sent periodically)")
                print(f"      Or capture from the beginning of the stream")
            
            return False


def main():
    # Parse command line arguments
    host = '0.0.0.0'
    port = 3333
    duration = 30
    
    if len(sys.argv) > 1:
        host = sys.argv[1]
    if len(sys.argv) > 2:
        port = int(sys.argv[2])
    if len(sys.argv) > 3:
        duration = int(sys.argv[3])
    
    validator = RTPH264Validator()
    valid = validator.analyze_stream(host, port, duration)
    
    # Exit with appropriate code
    sys.exit(0 if valid else 1)


if __name__ == "__main__":
    main()
