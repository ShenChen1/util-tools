#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import argparse
import binascii
import fcntl
import re
import socket
import struct
import time

class EthFrame():
    ETH_HDR_FMT = '!6s6sH'
    ETH_HDR_LEN = 6 + 6 + 2
    '''
    Simple Python model for an Ethernet Frame
    '''
    def __init__(self,
                 dst_mac='ff:ff:ff:ff:ff:ff',
                 src_mac='ff:ff:ff:ff:ff:ff',
                 proto='0x8951', payload='Peter'):
        self.eth_dst_addr = dst_mac
        self.eth_src_addr = src_mac
        self.eth_proto = proto
        self.eth_payload = payload

    def pack(self):
        dst = binascii.unhexlify(self.eth_dst_addr.replace(":",""))
        src = binascii.unhexlify(self.eth_src_addr.replace(":",""))
        eth_header = struct.pack(EthFrame.ETH_HDR_FMT,
                                 dst, src, int(self.eth_proto, 0))
        eth_frame = eth_header + bytes(self.eth_payload, encoding='ascii')
        return eth_frame

class Interface(object):
    ETH_P_ALL = 3
    SIOCGIFHWADDR = 0x8927
    SIOCGIFMTU = 0x8921

    def __init__(self, ifname):
        self.name = ifname

        # Open RAW socket to send on
        self.s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(Interface.ETH_P_ALL))
        self.s.bind((self.name, 0))

        # Get the MAC address of the interface to send on
        ifr = struct.pack('256s', bytes(self.name[:15], encoding='ascii'))
        ifs = fcntl.ioctl(self.s.fileno(), Interface.SIOCGIFHWADDR, ifr)
        self.mac = ':'.join(['%02x' % i for i in ifs[18:24]])

        # Get MTU size of the interface
        ifr = struct.pack('256s', bytes(self.name[:15], encoding='ascii'))
        ifs = fcntl.ioctl(self.s.fileno(), Interface.SIOCGIFMTU, ifr)
        self.mtu = struct.unpack('<H',ifs[16:18])[0]

    def __del__(self):
        self.s.close()

    def get_mac(self):
        return self.mac

    def get_mtu(self):
        return self.mtu

    def send_packet(self, data):
        self.s.send(data)

def verify_hex(x):
    return hex(int(x, 0))

def verify_mac(mac):
    if re.match("[0-9a-f]{2}([-:]?)[0-9a-f]{2}(\\1[0-9a-f]{2}){4}$", mac.lower()):
        return mac
    raise RuntimeError("Invalid mac format:%s" % mac)

def main():
    parser = argparse.ArgumentParser(prog='l2_packet_sender')
    parser.add_argument('-I', '--interface', default="eth0")
    parser.add_argument('-i', '--interval', type=int, default=100)
    parser.add_argument('-s', '--packetsize', type=int, default=0)
    parser.add_argument('-c', '--count', type=int, default=-1)
    parser.add_argument('-m', '--dst_mac', type=verify_mac, default='12:23:34:45:56:67')
    parser.add_argument('-p', '--ether_proto', type=verify_hex, default='0x8951')
    parser.add_argument('-d', '--data', default="hello")
    args = parser.parse_args()

    # Get info of interface
    iface = Interface(args.interface)

    # Use MTU size if packetsize is 0
    if args.packetsize == 0:
        args.packetsize = iface.get_mtu()

    # Packet data
    payload = ''
    for i in range(args.packetsize):
        payload += args.data[i % len(args.data)]

    # Construct the Ethernet frame
    src_mac = iface.get_mac()
    frame = EthFrame(args.dst_mac, src_mac, args.ether_proto, payload).pack()

    i = args.count
    while i:
        # Send packet
        iface.send_packet(frame)

        # Sleep for a while
        time.sleep(args.interval / 1000)
        i -= 1

if __name__ == '__main__':
    main()
