#include "packetracker.h"
#include "networksort.h"
#include "kismet_server.h"

Packetracker::Packetracker() {
    gps = NULL;

    num_networks = num_packets = num_dropped = num_noise =
        num_crypt = num_interesting = num_cisco = 0;

    errstr[0] = '\0';

}

void Packetracker::AddGPS(GPSD *in_gps) {
    gps = in_gps;
}

vector<wireless_network *> Packetracker::FetchNetworks() {
    vector<wireless_network *> ret_vec = network_list;

    return ret_vec;
}

// Return the string literal
// This is a really inefficient way of doing this, but I'm tired right now.
string Packetracker::Mac2String(uint8_t *mac, char seperator) {
    char tempstr[MAC_STR_LEN];

    // There must be a better way to do this...
    if (seperator != '\0')
        snprintf(tempstr, MAC_STR_LEN, "%02X%c%02X%c%02X%c%02X%c%02X%c%02X",
                 mac[0], seperator, mac[1], seperator, mac[2], seperator,
                 mac[3], seperator, mac[4], seperator, mac[5]);
    else
        snprintf(tempstr, MAC_STR_LEN, "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);

    string temp = tempstr;
    return temp;
}

// Convert a net to string
string Packetracker::Net2String(wireless_network *in_net) {
    string ret;
    char output[2048];

    snprintf(output, 2048, "%s %d \001%s\001 \001%s\001 %d %d %d %d %d %d %d %d %d "
             "%d.%d.%d.%d %d.%d.%d.%d %d.%d.%d.%d %d %f %f %f %f %f %f %f %f %d %d %d %2.1f "
             "%d %d %d %d %d %A %A %A %ld",
             in_net->bssid.size() > 0 ? in_net->bssid.c_str() : "\002",
             (int) in_net->type,
             in_net->ssid.size() > 0 ? in_net->ssid.c_str() : "\002",
             in_net->beacon_info.size() > 0 ? in_net->beacon_info.c_str() : "\002",
             in_net->llc_packets, in_net->data_packets,
             in_net->crypt_packets, in_net->interesting_packets,
             in_net->channel, in_net->wep,
             (int) in_net->first_time, (int) in_net->last_time,
             (int) in_net->ipdata.atype,
             in_net->ipdata.range_ip[0], in_net->ipdata.range_ip[1],
             in_net->ipdata.range_ip[2], in_net->ipdata.range_ip[3],
             in_net->ipdata.mask[0], in_net->ipdata.mask[1],
             in_net->ipdata.mask[2], in_net->ipdata.mask[3],
             in_net->ipdata.gate_ip[0], in_net->ipdata.gate_ip[1],
             in_net->ipdata.gate_ip[2], in_net->ipdata.gate_ip[3],

             in_net->gps_fixed,
             in_net->min_lat, in_net->min_lon, in_net->min_alt, in_net->min_spd,
             in_net->max_lat, in_net->max_lon, in_net->max_alt, in_net->max_spd,

             in_net->ipdata.octets,
             in_net->cloaked, in_net->beacon, in_net->maxrate,

             in_net->manuf_id, in_net->manuf_score,
	     
             in_net->quality, in_net->signal, in_net->noise,

             in_net->aggregate_lat, in_net->aggregate_lon, in_net->aggregate_alt,
             in_net->aggregate_points);

    ret = output;

    return ret;
}

string Packetracker::CDP2String(cdp_packet *in_cdp) {
    string ret;
    char output[2048];

    // Transform the data fields \n's into \003's for easy transfer
    for (unsigned int i = 0; i < strlen(in_cdp->dev_id); i++)
        if (in_cdp->dev_id[i] == '\n') in_cdp->dev_id[i] = '\003';
    for (unsigned int i = 0; i < strlen(in_cdp->interface); i++)
        if (in_cdp->interface[i] == '\n') in_cdp->interface[i] = '\003';
    for (unsigned int i = 0; i < strlen(in_cdp->software); i++)
        if (in_cdp->software[i] == '\n') in_cdp->software[i] = '\003';
    for (unsigned int i = 0; i < strlen(in_cdp->platform); i++)
        if (in_cdp->platform[i] == '\n') in_cdp->platform[i] = '\003';

    snprintf(output, 2048, "\001%s\001 %d.%d.%d.%d \001%s\001 "
             "%d:%d:%d:%d:%d:%d:%d \001%s\001 \001%s\001",
             in_cdp->dev_id[0] != '\0' ? in_cdp->dev_id : "\002",
             in_cdp->ip[0], in_cdp->ip[1], in_cdp->ip[2], in_cdp->ip[3],
             in_cdp->interface[0] != '\0' ? in_cdp->interface : "\002",
             in_cdp->cap.level1, in_cdp->cap.igmp_forward, in_cdp->cap.nlp, in_cdp->cap.level2_switching,
             in_cdp->cap.level2_sourceroute, in_cdp->cap.level2_transparent, in_cdp->cap.level3,
             in_cdp->software[0] != '\0' ? in_cdp->software : "\002",
             in_cdp->platform[0] != '\0' ? in_cdp->platform : "\002");

    ret = output;
    return ret;
}
// Is a string blank?
bool Packetracker::IsBlank(const char *s) {
    int len, i;
    if (NULL == s) { return true; }
    if (0 == (len = strlen(s))) { return true; }
    for (i = 0; i < len; ++i) {
        if (' ' != s[i]) { return false; }
    }
    return true;
}

int Packetracker::ProcessPacket(packet_info info, char *in_status) {
    wireless_network *net;
    int ret = 0;
    string bssid_mac;

    num_packets++;

    // Junk unknown and noise packets
    if (info.type == packet_noise) {
        num_dropped++;
        num_noise++;
        return(0);
    } else if (info.type == packet_unknown) {
        // If we can't figure out what it is
        // or if FromDS and ToDS are set, we can't make much sense of it so don't
        // try to make a network out of it -- toss it.
        num_dropped++;
        return(0);
    }

    bssid_mac = Mac2String(info.bssid_mac, ':');

    // If it's a broadcast (From and To DS == 1) try to match it to an existing
    // network
    if (info.type == packet_ap_broadcast && bssid_map.find(bssid_mac) == bssid_map.end()) {
        string ts_mac, fs_mac;
        ts_mac = Mac2String(info.source_mac, ':');
        fs_mac = Mac2String(info.dest_mac, ':');
        if (bssid_map.find(ts_mac) != bssid_map.end()) {
            memcpy(info.bssid_mac, info.source_mac, MAC_LEN);
        } else if (bssid_map.find(fs_mac) != bssid_map.end()) {
            memcpy(info.bssid_mac, info.dest_mac, MAC_LEN);
        } else {
            num_dropped++;
            return(0);
        }

        bssid_mac = Mac2String(info.bssid_mac, ':');
    }

    if (bssid_mac == "00:00:00:00:00:00") {
        num_dropped++;
        return(0);
    }

    // If it's a probe request, see if we already know who it should belong to
    if (info.type == packet_probe_req) {
        if (probe_map.find(bssid_mac) != probe_map.end())
            bssid_mac = probe_map[bssid_mac];
    }


    // Find out if we have this network -- Every network that actually
    // gets added has a bssid, so we'll use that to search.  We've filtered
    // everything else out by this point so we're safe to just work off bssid
    if (bssid_map.find(bssid_mac) == bssid_map.end()) {

        // Make a network for them
        net = new wireless_network;

        memcpy(net->bssid_raw, info.bssid_mac, MAC_LEN);

        if (bssid_ip_map.find(bssid_mac) != bssid_ip_map.end())
            memcpy(&net->ipdata, &bssid_ip_map[bssid_mac], sizeof(net_ip_data));

        if (strlen(info.ssid) == 0 || IsBlank(info.ssid)) {
            if (bssid_cloak_map.find(bssid_mac) != bssid_cloak_map.end()) {
                net->ssid = bssid_cloak_map[bssid_mac];

                // If it's a beacon and empty then we're cloaked and we found our
                // ssid so fill it in
                if (info.type == packet_beacon) {
                    net->cloaked = 1;
                } else {
                    net->cloaked = 0;
                }
            } else {
                net->ssid = NOSSID;
                net->cloaked = 0;
            }
        } else {
            net->ssid = info.ssid;
            net->cloaked = 0;
            bssid_cloak_map[bssid_mac] = info.ssid;
        }

        net->channel = info.channel;

        if (info.ap == 1)
            net->type = network_ap;

        if (info.type == packet_probe_req)
            net->type = network_probe;

        net->wep = info.wep;

        net->beacon = info.beacon;

        net->bssid = bssid_mac;

        // Put us in the master list
        network_list.push_back(net);
        net->listed = 1;

        net->first_time = time(0);

        net->maxrate = info.maxrate;

        if (strlen(info.beacon_info) != 0)
            net->beacon_info = info.beacon_info;

        if (net->type == network_probe) {
            snprintf(in_status, STATUS_MAX, "Found new probed network bssid %s",
                     net->bssid.c_str());
        } else {
            snprintf(in_status, STATUS_MAX, "Found new network \"%s\" bssid %s WEP %c Ch %d @ %.2f mbit",
                     net->ssid.c_str(), net->bssid.c_str(), net->wep ? 'Y' : 'N',
                     net->channel, net->maxrate);
        }

        MatchBestManuf(net, 1);

        if (gps != NULL) {
            float lat, lon, alt, spd;
            int fix;

            gps->FetchLoc(&lat, &lon, &alt, &spd, &fix);

            if (fix >= 2) {
                net->gps_fixed = fix;
                net->min_lat = net->max_lat = lat;
                net->min_lon = net->max_lon = lon;
                net->min_alt = net->max_alt = alt;
                net->min_spd = net->max_spd = spd;

                net->aggregate_lat = lat;
                net->aggregate_lon = lon;
                net->aggregate_alt = alt;
                net->aggregate_points = 1;
            }

        }

        num_networks++;

        // And add us to all the maps
        bssid_map[net->bssid] = net;
        ssid_map[net->ssid] = net;

        // Return 1 if we make a new network entry
        ret = 1;
    } else {
        net = bssid_map[bssid_mac];
        if (net->listed == 0) {
            network_list.push_back(net);
            net->listed = 1;
        }
        ret = 0;
    }

    net->last_time = time(0);

    net->quality = info.quality;
    net->signal = info.signal;
    net->noise = info.noise;

    if (gps != NULL) {
        float lat, lon, alt, spd;
        int fix;

        gps->FetchLoc(&lat, &lon, &alt, &spd, &fix);

        if (fix > 1) {
            net->aggregate_lat += lat;
            net->aggregate_lon += lon;
            net->aggregate_alt += alt;
            net->aggregate_points += 1;

            net->gps_fixed = fix;

            if (lat < net->min_lat || net->min_lat == 0)
                net->min_lat = lat;
            else if (lat > net->max_lat)
                net->max_lat = lat;

            if (lon < net->min_lon || net->min_lon == 0)
                net->min_lon = lon;
            else if (lon > net->max_lon)
                net->max_lon = lon;

            if (alt < net->min_alt || net->min_alt == 0)
                net->min_alt = alt;
            else if (alt > net->max_alt)
                net->max_alt = alt;

            if (spd < net->min_spd || net->min_spd == 0)
                net->min_spd = spd;
            else if (spd > net->max_spd)
                net->max_spd = spd;

        } else {
            net->gps_fixed = 0;
        }

    }

    if (info.type == packet_beacon && strlen(info.beacon_info) != 0 &&
        IsBlank(net->beacon_info.c_str())) {
        net->beacon_info = info.beacon_info;
    }

    if (info.type != packet_data && info.type != packet_ap_broadcast &&
        info.type != packet_adhoc_data) {
        // Update the ssid record if we got a beacon for a data network
        if (info.type == packet_beacon) {
            // Update if they changed their SSID
            if (net->ssid != NOSSID && net->ssid != info.ssid && !IsBlank(info.ssid)) {
                net->ssid = info.ssid;
                bssid_cloak_map[net->bssid] = info.ssid;
                MatchBestManuf(net, 1);
            } else if (net->ssid == NOSSID && strlen(info.ssid) > 0 && !IsBlank(info.ssid)) {
                net->ssid = info.ssid;

                bssid_cloak_map[net->bssid] = info.ssid;

                if (net->type == network_ap) {
                    net->cloaked = 1;
                    snprintf(in_status, STATUS_MAX, "Found SSID \"%s\" for cloaked network BSSID %s",
                             net->ssid.c_str(), net->bssid.c_str());
                } else {
                    snprintf(in_status, STATUS_MAX, "Found SSID \"%s\" for network BSSID %s",
                             net->ssid.c_str(), net->bssid.c_str());
                }

                MatchBestManuf(net, 1);
                ret = 2;
            }

            net->channel = info.channel;
            net->wep = info.wep;
            net->type = network_ap;
        }


        // If this is a probe response and the ssid we have is blank, update it.
        // With "closed" networks, this is our chance to see the real ssid.
        // (Thanks to Jason Luther <jason@ixid.net> for this "closed network" detection)
        if (info.type == packet_probe_response || info.type == packet_reassociation &&
            (strlen(info.ssid) > 0) &&
            !IsBlank(info.ssid)) {
            if (net->ssid == NOSSID) {
                net->cloaked = 1;
                net->ssid = info.ssid;
                net->channel = info.channel;
                net->wep = info.wep;

                bssid_cloak_map[net->bssid] = info.ssid;

                snprintf(in_status, STATUS_MAX, "Found SSID \"%s\" for cloaked network BSSID %s",
                         net->ssid.c_str(), net->bssid.c_str());

                MatchBestManuf(net, 1);

                ret = 2;
            } else if (info.ssid != bssid_cloak_map[net->bssid]) {
                bssid_cloak_map[net->bssid] = info.ssid;
                net->ssid = info.ssid;
                net->wep = info.wep;

                MatchBestManuf(net, 1);
            }

            // If we have a probe request network, absorb it into the main network
            string resp_mac = Mac2String(info.dest_mac, ':');

            probe_map[resp_mac] = net->bssid;

            // If we have any networks that match the response already in existance,
            // we should add them to the main network and kill them off
            if (bssid_map.find(resp_mac) != bssid_map.end()) {
                wireless_network *pnet = bssid_map[resp_mac];
                if (pnet->type == network_probe) {

                    net->llc_packets += pnet->llc_packets;
                    net->data_packets += pnet->data_packets;
                    net->crypt_packets += pnet->crypt_packets;
                    net->interesting_packets += pnet->interesting_packets;
                    pnet->type = network_remove;
                    pnet->last_time = time(0);

                    snprintf(in_status, STATUS_MAX, "Associated probe network \"%s\".",
                             pnet->bssid.c_str());

                    num_networks--;

                    ret = 3;
                }
            }

        }

        if (net->type != network_ap && info.type == packet_adhoc) {
            net->type = network_adhoc;
        }

        net->llc_packets++;

    } else {
        if (info.encrypted) {
            net->crypt_packets++;
            num_crypt++;
        }

        if (info.interesting) {
            num_interesting++;
            net->interesting_packets++;
        }

        net->data_packets++;

        // Record a cisco device
        if (info.proto.type == proto_cdp) {
            net->cisco_equip[info.proto.cdp.dev_id] = info.proto.cdp;
            num_cisco++;
        }

        // If we're not aware of a dhcp server already, try to find one.
        if (info.proto.type == proto_dhcp_server && (net->ipdata.atype < address_dhcp ||
                                                     net->ipdata.load_from_store == 1)) {
            // Jackpot, this tells us everything we need to know
            net->ipdata.atype = address_dhcp;

            net->ipdata.range_ip[0] = info.proto.misc_ip[0] & info.proto.mask[0];
            net->ipdata.range_ip[1] = info.proto.misc_ip[1] & info.proto.mask[1];
            net->ipdata.range_ip[2] = info.proto.misc_ip[2] & info.proto.mask[2];
            net->ipdata.range_ip[3] = info.proto.misc_ip[3] & info.proto.mask[3];

            // memcpy(net->range_ip, info.proto.misc_ip, 4);
            memcpy(net->ipdata.mask, info.proto.mask, 4);
            memcpy(net->ipdata.gate_ip, info.proto.gate_ip, 4);

            snprintf(in_status, STATUS_MAX, "Found IP range for \"%s\" via DHCP %d.%d.%d.%d mask %d.%d.%d.%d",
                     net->ssid.c_str(), net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                     net->ipdata.range_ip[2], net->ipdata.range_ip[3],
                     net->ipdata.mask[0], net->ipdata.mask[1],
                     net->ipdata.mask[2], net->ipdata.mask[3]);

            net->ipdata.octets = 0;

            //memcpy(&bssid_ip_map[net->bssid], &net->ipdata, sizeof(net_ip_data));
            bssid_ip_map[net->bssid] = net->ipdata;
            net->ipdata.load_from_store = 0;

            ret = 2;

        } else if (info.proto.type == proto_arp && (net->ipdata.atype < address_arp ||
                                                    net->ipdata.load_from_store == 1)) {
            uint8_t new_range[4];

            memset(new_range, 0, 4);

            if (info.proto.source_ip[0] != 0x00 &&
                info.proto.misc_ip[0] != 0x00) {

                int oct;
                for (oct = 0; oct < 4; oct++) {
                    if (info.proto.source_ip[oct] != info.proto.misc_ip[oct])
                        break;

                    new_range[oct] = info.proto.source_ip[oct];
                }

                if (oct < net->ipdata.octets || net->ipdata.octets == 0) {
                    net->ipdata.octets = oct;
                    memcpy(net->ipdata.range_ip, new_range, 4);
                    snprintf(in_status, STATUS_MAX, "Found IP range for \"%s\" via ARP %d.%d.%d.%d",
                             net->ssid.c_str(), net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                             net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
                    //gui->WriteStatus(status);

                    net->ipdata.atype = address_arp;

                    bssid_ip_map[net->bssid] = net->ipdata;
                    net->ipdata.load_from_store = 0;

                    ret = 2;
                }
            } // valid arp
        } else if (info.proto.type == proto_udp && (net->ipdata.atype <= address_udp ||
                                                    net->ipdata.load_from_store == 1)) {
            uint8_t new_range[4];

            memset(new_range, 0, 4);

            // Not 0.x.x.x.  Not 255.x.x.x.  At least first octet must
            // match.
            if (info.proto.source_ip[0] != 0x00 &&
                info.proto.dest_ip[0] != 0x00 &&
                info.proto.dest_ip[0] != 0xFF &&
                info.proto.source_ip[0] == info.proto.dest_ip[0]) {

                int oct;
                for (oct = 0; oct < 4; oct++) {
                    if (info.proto.source_ip[oct] != info.proto.dest_ip[oct])
                        break;

                    new_range[oct] = info.proto.source_ip[oct];
                }

                if (oct < net->ipdata.octets || net->ipdata.octets == 0) {
                    net->ipdata.octets = oct;
                    memcpy(net->ipdata.range_ip, new_range, 4);
                    snprintf(in_status, STATUS_MAX, "Found IP range for \"%s\" via UDP %d.%d.%d.%d",
                             net->ssid.c_str(), net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                             net->ipdata.range_ip[2], net->ipdata.range_ip[3]);

                    net->ipdata.atype = address_udp;

                    bssid_ip_map[net->bssid] = net->ipdata;
                    net->ipdata.load_from_store = 0;

                    ret = 2;
                }
            }
        }  else if (info.proto.type == proto_misc_tcp && (net->ipdata.atype <= address_tcp ||
                                                          net->ipdata.load_from_store == 1)) {
            uint8_t new_range[4];

            memset(new_range, 0, 4);

            // Not 0.x.x.x.  Not 255.x.x.x.  At least first octet must
            // match.
            if (info.proto.source_ip[0] != 0x00 &&
                info.proto.dest_ip[0] != 0x00 &&
                info.proto.dest_ip[0] != 0xFF &&
                info.proto.source_ip[0] == info.proto.dest_ip[0]) {

                int oct;
                for (oct = 0; oct < 4; oct++) {
                    if (info.proto.source_ip[oct] != info.proto.dest_ip[oct])
                        break;

                    new_range[oct] = info.proto.source_ip[oct];
                }

                if (oct < net->ipdata.octets || net->ipdata.octets == 0) {
                    net->ipdata.octets = oct;
                    memcpy(net->ipdata.range_ip, new_range, 4);
                    snprintf(in_status, STATUS_MAX, "Found IP range for \"%s\" via TCP %d.%d.%d.%d",
                             net->ssid.c_str(), net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                             net->ipdata.range_ip[2], net->ipdata.range_ip[3]);

                    net->ipdata.atype = address_tcp;

                    bssid_ip_map[net->bssid] = net->ipdata;
                    net->ipdata.load_from_store = 0;

                    ret = 2;
                }
            }
        }

    } // data packet

    return ret;
}

int Packetracker::WriteNetworks(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    int netnum = 1;
    vector<wireless_network *> bssid_vec;

    // Convert the map to a vector and sort it
    for (map<string, wireless_network *>::const_iterator i = bssid_map.begin();
         i != bssid_map.end(); ++i)
        bssid_vec.push_back(i->second);

    sort(bssid_vec.begin(), bssid_vec.end(), SortFirstTimeLT());

    for (unsigned int i = 0; i < bssid_vec.size(); i++) {
        wireless_network *net = bssid_vec[i];

        char lt[25];
        char ft[25];

        snprintf(lt, 25, "%s", ctime(&net->last_time));
        snprintf(ft, 25, "%s", ctime(&net->first_time));

        char type[15];

        if (net->type == network_ap)
            snprintf(type, 15, "infrastructure");
        else if (net->type == network_adhoc)
            snprintf(type, 15, "ad-hoc");
        else if (net->type == network_probe)
            snprintf(type, 15, "probe");
        else if (net->type == network_data)
            snprintf(type, 15, "data");


        fprintf(in_file, "Network %d: \"%s\" BSSID: \"%s\"\n"
                "    Type     : %s\n"
                "    Info     : \"%s\"\n"
                "    Channel  : %02d\n"
                "    WEP      : \"%s\"\n"
                "    Maxrate  : %2.1f\n"
                "    LLC      : %d\n"
                "    Data     : %d\n"
                "    Crypt    : %d\n"
                "    Weak     : %d\n"
                "    Total    : %d\n"
                "    First    : \"%s\"\n"
                "    Last     : \"%s\"\n",
                netnum,
                net->ssid.c_str(), net->bssid.c_str(), type,
                net->beacon_info == "" ? "None" : net->beacon_info.c_str(),
                net->channel, net->wep ? "Yes" : "No",
                net->maxrate,
                net->llc_packets, net->data_packets,
                net->crypt_packets, net->interesting_packets,
                (net->llc_packets + net->data_packets),
                ft, lt);

        //if (net->first_mode > 1) {

        if (net->gps_fixed != -1)
            fprintf(in_file,
                    "    Min Loc: Lat %f Lon %f Alt %f Spd %f\n"
                    "    Max Loc: Lat %f Lon %f Alt %f Spd %f\n",
                    net->min_lat, net->min_lon,
                    metric ? net->min_alt / 3.3 : net->min_alt,
                    metric ? net->min_spd * 1.6093 : net->min_spd,
                    net->max_lat, net->max_lon,
                    metric ? net->max_alt / 3.3 : net->max_alt,
                    metric ? net->max_spd * 1.6093 : net->max_spd);

        if (net->ipdata.atype == address_dhcp)
            fprintf(in_file, "    Address found via DHCP %d.%d.%d.%d \n"
                    "      netmask %d.%d.%d.%d gw %d.%d.%d.%d\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3],
                    net->ipdata.mask[0], net->ipdata.mask[1],
                    net->ipdata.mask[2], net->ipdata.mask[3],
                    net->ipdata.gate_ip[0], net->ipdata.gate_ip[1],
                    net->ipdata.gate_ip[2], net->ipdata.gate_ip[3]);
        else if (net->ipdata.atype == address_arp)
            fprintf(in_file, "    Address found via ARP %d.%d.%d.%d\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
        else if (net->ipdata.atype == address_udp)
            fprintf(in_file, "    Address found via UDP %d.%d.%d.%d\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
        else if (net->ipdata.atype == address_tcp)
            fprintf(in_file, "    Address found via TCP %d.%d.%d.%d\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
        fprintf(in_file, "\n");
        netnum++;
    }

    return 1;
}

// Write out the cisco information
int Packetracker::WriteCisco(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    vector<wireless_network *> bssid_vec;

    // Convert the map to a vector and sort it
    for (map<string, wireless_network *>::const_iterator i = bssid_map.begin();
         i != bssid_map.end(); ++i)
        bssid_vec.push_back(i->second);

    sort(bssid_vec.begin(), bssid_vec.end(), SortFirstTimeLT());

    for (unsigned int i = 0; i < bssid_vec.size(); i++) {
        wireless_network *net = bssid_vec[i];

        if (net->cisco_equip.size() == 0)
            continue;


        fprintf(in_file, "Network: \"%s\" BSSID: \"%s\"\n",
                net->ssid.c_str(), net->bssid.c_str());

        int devnum = 1;
        for (map<string, cdp_packet>::const_iterator x = net->cisco_equip.begin();
             x != net->cisco_equip.end(); ++x) {
            cdp_packet cdp = x->second;

            fprintf(in_file, "CDP Broadcast Device %d\n", devnum);
            fprintf(in_file, "    Device ID : %s\n", cdp.dev_id);
            fprintf(in_file, "    Capability: %s%s%s%s%s%s%s\n",
                    cdp.cap.level1 ? "Level 1 " : "" ,
                    cdp.cap.igmp_forward ? "IGMP forwarding " : "",
                    cdp.cap.nlp ? "Network-layer protocols " : "",
                    cdp.cap.level2_switching ? "Level 2 switching " : "",
                    cdp.cap.level2_sourceroute ? "Level 2 source-route bridging " : "",
                    cdp.cap.level2_transparent ? "Level 2 transparent bridging " : "",
                    cdp.cap.level3 ? "Level 3 routing " : "");
            fprintf(in_file, "    Interface : %s\n", cdp.interface);
            fprintf(in_file, "    IP        : %d.%d.%d.%d\n",
                    cdp.ip[0], cdp.ip[1], cdp.ip[2], cdp.ip[3]);
            fprintf(in_file, "    Platform  : %s\n", cdp.platform);
            fprintf(in_file, "    Software  : %s\n", cdp.software);
            fprintf(in_file, "\n");
            devnum++;
        } // cdp
    } // net

    return 1;
}

// Sanitize data not to contain ';'.
string Packetracker::SanitizeCSV(string in_data) {
    string ret;

    for (unsigned int x = 0; x < in_data.length(); x++) {
        if (in_data[x] == ';')
            ret += ' ';
        else
            ret += in_data[x];
    }

    return ret;
}

/* CSV support 
 * Author: Reyk Floeter <reyk@synack.de>
 * Date:   2002/03/13
 */
int Packetracker::WriteCSVNetworks(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    int netnum = 1;
    vector<wireless_network *> bssid_vec;

    // Convert the map to a vector and sort it
    for (map<string, wireless_network *>::const_iterator i = bssid_map.begin();
         i != bssid_map.end(); ++i)
        bssid_vec.push_back(i->second);

    sort(bssid_vec.begin(), bssid_vec.end(), SortFirstTimeLT());

    fprintf(in_file, "Network;NetType;ESSID;BSSID;Info;Channel;Maxrate;WEP;LLC;Data;Crypt;Weak;Total;"
			"First;Last;"
            "GPSMinLat;GPSMinLon;GPSMinAlt;GPSMinSpd;"
            "GPSMaxLat;GPSMaxLon;GPSMaxAlt;GPSMaxSpd;"
            "DHCP;DHCPNetmask;DHCPGateway;ARP;UDP;TCP;\r\n");

    for (unsigned int i = 0; i < bssid_vec.size(); i++) {
        wireless_network *net = bssid_vec[i];

        char lt[25];
        char ft[25];

        snprintf(lt, 25, "%s", ctime(&net->last_time));
        snprintf(ft, 25, "%s", ctime(&net->first_time));

        char type[15];
        if (net->type == network_ap)
            snprintf(type, 15, "infrastructure");
        else if (net->type == network_adhoc)
            snprintf(type, 15, "ad-hoc");
        else if (net->type == network_probe)
            snprintf(type, 15, "probe");
        else if (net->type == network_data)
            snprintf(type, 15, "data");


        fprintf(in_file, "%d;%s;%s;%s;%s;%02d;%2.1f;%s;%d;%d;%d;%d;%d;%s;%s;",
                netnum, type,
                SanitizeCSV(net->ssid).c_str(), net->bssid.c_str(),
                net->beacon_info == "" ? "None" : SanitizeCSV(net->beacon_info).c_str(),
                net->channel, 
                net->maxrate,
                net->wep ? "Yes" : "No",
                net->llc_packets, net->data_packets,
                net->crypt_packets, net->interesting_packets,
                (net->llc_packets + net->data_packets),
                ft, lt);

        if (net->gps_fixed != -1) {
            fprintf(in_file,
                    "%f;%f;%f;%f;"
                    "%f;%f;%f;%f;",
                    net->min_lat, net->min_lon,
                    metric ? net->min_alt / 3.3 : net->min_alt,
                    metric ? net->min_spd * 1.6093 : net->min_spd,
                    net->max_lat, net->max_lon,
                    metric ? net->max_alt / 3.3 : net->max_alt,
                    metric ? net->max_spd * 1.6093 : net->max_spd);
        } else {
            fprintf(in_file, ";;;;;;;;");
        }

        if (net->ipdata.atype == address_dhcp)
            fprintf(in_file, "%d.%d.%d.%d;%d.%d.%d.%d;%d.%d.%d.%d;;;;\r\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3],
                    net->ipdata.mask[0], net->ipdata.mask[1],
                    net->ipdata.mask[2], net->ipdata.mask[3],
                    net->ipdata.gate_ip[0], net->ipdata.gate_ip[1],
                    net->ipdata.gate_ip[2], net->ipdata.gate_ip[3]);
        else if (net->ipdata.atype == address_arp)
            fprintf(in_file, ";;;%d.%d.%d.%d;;;\r\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
        else if (net->ipdata.atype == address_udp)
            fprintf(in_file, ";;;;%d.%d.%d.%d;;\r\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
        else if (net->ipdata.atype == address_tcp)
            fprintf(in_file, ";;;;;%d.%d.%d.%d;\r\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
        else
            fprintf(in_file, ";;;;;;\r\n");
        netnum++;
    }

    return 1;
}

string Packetracker::SanitizeXML(string in_data) {
    string ret;

    for (unsigned int x = 0; x < in_data.length(); x++) {
        if (in_data[x] == '&')
            ret += "&amp;";
        else if (in_data[x] == '<')
            ret += "&lt;";
        else if (in_data[x] == '>')
            ret += "&gt;";
        else
            ret += in_data[x];
    }

    return ret;
}

// Write an XML-formatted output conforming to our DTD at
// http://kismetwireless.net/kismet-1.0.dtd
int Packetracker::WriteXMLNetworks(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    int netnum = 1;
    vector<wireless_network *> bssid_vec;

    fprintf(in_file, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
    fprintf(in_file, "<!DOCTYPE detection-run SYSTEM \"http://kismetwireless.net/kismet-1.4.dtd\">\n");

    fprintf(in_file, "\n\n");

    char lt[25];
    char ft[25];

    snprintf(ft, 25, "%s", ctime(&start_time));
    time_t cur_time = time(0);
    snprintf(lt, 25, "%s", ctime(&cur_time));

    fprintf(in_file, "<detection-run kismet-version=\"%d.%d\" start-time=\"%s\" end-time=\"%s\">\n",
            MAJOR, MINOR, ft, lt);

    // Convert the map to a vector and sort it
    for (map<string, wireless_network *>::const_iterator i = bssid_map.begin();
         i != bssid_map.end(); ++i)
        bssid_vec.push_back(i->second);

    sort(bssid_vec.begin(), bssid_vec.end(), SortFirstTimeLT());

    for (unsigned int i = 0; i < bssid_vec.size(); i++) {
        wireless_network *net = bssid_vec[i];

        snprintf(lt, 25, "%s", ctime(&net->last_time));
        snprintf(ft, 25, "%s", ctime(&net->first_time));
        char type[15];

        if (net->type == network_ap)
            snprintf(type, 15, "infrastructure");
        else if (net->type == network_adhoc)
            snprintf(type, 15, "ad-hoc");
        else if (net->type == network_probe)
            snprintf(type, 15, "probe");
        else if (net->type == network_data)
            snprintf(type, 15, "data");

        fprintf(in_file, "  <wireless-network number=\"%d\" type=\"%s\" wep=\"%s\" cloaked=\"%s\" first-time=\"%s\" last-time=\"%s\">\n",
                netnum, type, net->wep ? "true" : "false", net->cloaked ? "true" : "false",
                ft, lt);

        if (net->ssid != NOSSID)
            fprintf(in_file, "    <SSID>%s</SSID>\n", SanitizeXML(net->ssid).c_str());

        fprintf(in_file, "    <BSSID>%s</BSSID>\n", net->bssid.c_str());
        if (net->beacon_info != "")
            fprintf(in_file, "    <info>%s</info>\n", SanitizeXML(net->beacon_info).c_str());
        fprintf(in_file, "    <channel>%d</channel>\n", net->channel);
        fprintf(in_file, "    <maxrate>%2.1f</maxrate>\n", net->maxrate);
        fprintf(in_file, "    <packets>\n");
        fprintf(in_file, "      <LLC>%d</LLC>\n", net->llc_packets);
        fprintf(in_file, "      <data>%d</data>\n", net->data_packets);
        fprintf(in_file, "      <crypt>%d</crypt>\n", net->crypt_packets);
        fprintf(in_file, "      <weak>%d</weak>\n", net->interesting_packets);
        fprintf(in_file, "      <total>%d</total>\n",
                (net->llc_packets + net->data_packets));
        fprintf(in_file, "    </packets>\n");

        if (net->gps_fixed != -1) {
            fprintf(in_file, "    <gps-info unit=\"%s\">\n", metric ? "metric" : "english");
            fprintf(in_file, "      <min-lat>%f</min-lat>\n", net->min_lat);
            fprintf(in_file, "      <min-lon>%f</min-lon>\n", net->min_lon);
            fprintf(in_file, "      <min-alt>%f</min-alt>\n",
                    metric ? net->min_alt / 3.3 : net->min_alt);
            fprintf(in_file, "      <min-spd>%f</min-spd>\n",
                    metric ? net->min_alt * 1.6093 : net->min_spd);
            fprintf(in_file, "      <max-lat>%f</max-lat>\n", net->max_lat);
            fprintf(in_file, "      <max-lon>%f</max-lon>\n", net->max_lon);
            fprintf(in_file, "      <max-alt>%f</max-alt>\n",
                    metric ? net->max_alt / 3.3 : net->max_alt);
            fprintf(in_file, "      <max-spd>%f</max-spd>\n",
                    metric ? net->max_spd * 1.6093 : net->max_spd);
            fprintf(in_file, "    </gps-info>\n");
        }

        if (net->ipdata.atype == address_dhcp) {
            fprintf(in_file, "    <ip-address type=\"dhcp\">\n");
            fprintf(in_file, "      <ip-range>%d.%d.%d.%d</ip-range>\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
            fprintf(in_file, "      <ip-mask>%d.%d.%d.%d</ip-mask>\n",
                    net->ipdata.mask[0], net->ipdata.mask[1],
                    net->ipdata.mask[2], net->ipdata.mask[3]);
            fprintf(in_file, "      <ip-gateway>%d.%d.%d.%d</ip-gateway>\n",
                    net->ipdata.gate_ip[0], net->ipdata.gate_ip[1],
                    net->ipdata.gate_ip[2], net->ipdata.gate_ip[3]);
            fprintf(in_file, "    </ip-address>\n");
        } else if (net->ipdata.atype == address_arp) {
            fprintf(in_file, "    <ip-address type=\"arp\">\n");
            fprintf(in_file, "      <ip-range>%d.%d.%d.%d</ip-range>\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
            fprintf(in_file, "    </ip-address>\n");
        } else if (net->ipdata.atype == address_udp) {
            fprintf(in_file, "    <ip-address type=\"udp\">\n");
            fprintf(in_file, "      <ip-range>%d.%d.%d.%d</ip-range>\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
            fprintf(in_file, "    </ip-address>\n");
        } else if (net->ipdata.atype == address_tcp) {
            fprintf(in_file, "    <ip-address type=\"tcp\">\n");
            fprintf(in_file, "      <ip-range>%d.%d.%d.%d</ip-range>\n",
                    net->ipdata.range_ip[0], net->ipdata.range_ip[1],
                    net->ipdata.range_ip[2], net->ipdata.range_ip[3]);
            fprintf(in_file, "    </ip-address>\n");
        }

        netnum++;

        if (net->cisco_equip.size() == 0) {
        	fprintf(in_file, "  </wireless-network>\n");
            continue;
		}

        int devnum = 1;
        for (map<string, cdp_packet>::const_iterator x = net->cisco_equip.begin();
             x != net->cisco_equip.end(); ++x) {
            cdp_packet cdp = x->second;

            fprintf(in_file, "    <cisco number=\"%d\">\n", devnum);
            fprintf(in_file, "      <cdp-device-id>%s</cdp-device-id>\n",
                    cdp.dev_id);
            fprintf(in_file, "      <cdp-capability level1=\"%s\" igmp-forward=\"%s\" netlayer=\"%s\" "
                    "level2-switching=\"%s\" level2-sourceroute=\"%s\" level2-transparent=\"%s\" "
                    "level3-routing=\"%s\"/>\n",
                    cdp.cap.level1 ? "true" : "false",
                    cdp.cap.igmp_forward ? "true" : "false",
                    cdp.cap.nlp ? "true" : "false",
                    cdp.cap.level2_switching ? "true" : "false",
                    cdp.cap.level2_sourceroute ? "true" : "false",
                    cdp.cap.level2_transparent ? "true" : "false",
                    cdp.cap.level3 ? "true" : "false");
            fprintf(in_file, "      <cdp-interface>%s</cdp-interface>\n", cdp.interface);
            fprintf(in_file, "      <cdp-ip>%d.%d.%d.%d</cdp-ip>\n",
                    cdp.ip[0], cdp.ip[1], cdp.ip[2], cdp.ip[3]);
            fprintf(in_file, "      <cdp-platform>%s</cdp-platform>\n", cdp.platform);
            fprintf(in_file, "      <cdp-software>%s</cdp-software>\n", cdp.software);
            fprintf(in_file, "    </cisco>\n");
            devnum++;
        } // cdp

        fprintf(in_file, "  </wireless-network>\n");

    } // net

    fprintf(in_file, "</detection-run>\n");

    return 1;
}

void Packetracker::ReadSSIDMap(FILE *in_file) {
    char dline[8192];
    char bssid[MAC_STR_LEN];
    char name[SSID_SIZE+1];

    char format[64];
    // stupid sscanf not taking dynamic sizes
    snprintf(format, 64, "%%%d[^ ] %%%d[^\n]\n",
             MAC_STR_LEN, SSID_SIZE);

    while (!feof(in_file)) {
        fgets(dline, 8192, in_file);

        if (feof(in_file)) break;

        // Fetch the line and continue if we're invalid...
        if (sscanf(dline, format, bssid, name) < 2)
            continue;

        bssid_cloak_map[bssid] = name;

    }

    return;
}

void Packetracker::WriteSSIDMap(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    char format[64];
    snprintf(format, 64, "%%.%ds %%.%ds\n", MAC_STR_LEN, SSID_SIZE);

    for (map<string, string>::iterator x = bssid_cloak_map.begin();
         x != bssid_cloak_map.end(); ++x)
        fprintf(in_file, format, x->first.c_str(), x->second.c_str());

    return;
}

void Packetracker::ReadIPMap(FILE *in_file) {
    char dline[8192];
    char bssid[MAC_STR_LEN];

    char format[64];
    // stupid sscanf not taking dynamic sizes
    snprintf(format, 64, "%%%d[^ ] %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d\n",
             MAC_STR_LEN);

    net_ip_data dat;

    while (!feof(in_file)) {
        fgets(dline, 8192, in_file);

        if (feof(in_file)) break;

        memset(&dat, 0, sizeof(net_ip_data));

        // Fetch the line and continue if we're invalid...
        if (sscanf(dline, format,
                   bssid, &dat.atype, &dat.octets,
                   &dat.range_ip[0], &dat.range_ip[1], &dat.range_ip[2], &dat.range_ip[3],
                   &dat.mask[0], &dat.mask[1], &dat.mask[2], &dat.mask[3],
                   &dat.gate_ip[0], &dat.gate_ip[1], &dat.gate_ip[2], &dat.gate_ip[3]) < 15)
            continue;

        dat.load_from_store = 1;

        memcpy(&bssid_ip_map[bssid], &dat, sizeof(net_ip_data));
    }

    return;

}

void Packetracker::WriteIPMap(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    char format[64];
    snprintf(format, 64, "%%.%ds %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d %%d\n",
            MAC_STR_LEN);

    for (map<string, net_ip_data>::iterator x = bssid_ip_map.begin();
         x != bssid_ip_map.end(); ++x)
        fprintf(in_file, format,
                x->first.c_str(),
                x->second.atype, x->second.octets,
                x->second.range_ip[0], x->second.range_ip[1],
                x->second.range_ip[2], x->second.range_ip[3],
                x->second.mask[0], x->second.mask[1],
                x->second.mask[2], x->second.mask[3],
                x->second.gate_ip[0], x->second.gate_ip[1],
                x->second.gate_ip[2], x->second.gate_ip[3]);

    return;
}

void Packetracker::RemoveNetwork(string in_bssid) {
    for (unsigned int x = 0; x < network_list.size(); x++) {
        if (network_list[x]->bssid == in_bssid) {
            network_list.erase(network_list.begin() + x);
            break;
        }
    }

}

// Write a gpsdrive compatable waypoint file
int Packetracker::WriteGpsdriveWaypt(FILE *in_file) {
    fseek(in_file, 0L, SEEK_SET);
    ftruncate(fileno(in_file), 0);

    // Convert the map to a vector and sort it
    for (map<string, wireless_network *>::const_iterator i = bssid_map.begin();
         i != bssid_map.end(); ++i) {
        wireless_network *net = i->second;

        float lat, lon;
        lat = (net->min_lat + net->max_lat) / 2;
        lon = (net->min_lon + net->max_lon) / 2;
        fprintf(in_file, "%s\t%f  %f\n", net->bssid.c_str(), lat, lon);
    }

    return 1;
}

string Packetracker::Packet2String(const packet_info *in_info) {
    char ret[2048];
    string rets;

    // type, encrypted, weak, beacon, source, dest, bssid
    snprintf(ret, 2048, "%d %d %d %d %d %s %s %s \001%s\001 ",
             in_info->type, (int) in_info->time, in_info->encrypted,
             in_info->interesting, in_info->beacon,
             Mac2String((uint8_t *) in_info->source_mac, ':').c_str(),
             Mac2String((uint8_t *) in_info->dest_mac, ':').c_str(),
             Mac2String((uint8_t *) in_info->bssid_mac, ':').c_str(),
             strlen(in_info->ssid) == 0 ? " " : in_info->ssid);
    rets += ret;

    if (in_info->proto.type != proto_unknown) {
        // type source dest sport dport
        uint8_t dip[4];
        if (in_info->proto.type == proto_arp)
            memcpy(dip, in_info->proto.misc_ip, 4);
        else
            memcpy(dip, in_info->proto.dest_ip, 4);

        snprintf(ret, 2048, "%d %d.%d.%d.%d %d.%d.%d.%d %d %d %d \001%s\001\n",
                 in_info->proto.type,
                 in_info->proto.source_ip[0], in_info->proto.source_ip[1],
                 in_info->proto.source_ip[2], in_info->proto.source_ip[3],
                 dip[0], dip[1], dip[2], dip[3],
                 in_info->proto.sport, in_info->proto.dport,
                 in_info->proto.nbtype,
                 strlen(in_info->proto.netbios_source) == 0 ? " " : in_info->proto.netbios_source);
    } else {
        snprintf(ret, 2048, "0 0.0.0.0 0.0.0.0 0 0 0 \001 \001\n");
    }

    rets += ret;

    return rets;
}
