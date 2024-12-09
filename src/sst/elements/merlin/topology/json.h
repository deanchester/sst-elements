// -*- mode: c++ -*-

// Copyright 2009-2024 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2024, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef COMPONENTS_MERLIN_TOPOLOGY_JSON_H
#define COMPONENTS_MERLIN_TOPOLOGY_JSON_H
#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include "sst/elements/merlin/router.h"

namespace SST {
namespace Merlin {
class topo_json : public Topology {

public:

    SST_ELI_REGISTER_SUBCOMPONENT(
        topo_json,
        "merlin",
        "json",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "JSON topology object",
        SST::Merlin::Topology
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"json:table",  "List of ports where table[i] is the port to follow for node i"},
        {"json:rtr_ports", "Shape of the fattree"},
        {"json:node_ports", "Name here"},
    )

    topo_json(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int num_vns); 
    ~topo_json();
    virtual void route_packet(int port, int vc, internal_router_event* ev);
    virtual internal_router_event* process_input(RtrEvent* ev); 
    virtual void routeInitData(int port, internal_router_event* ev, std::vector<int> &outPorts); 
    virtual internal_router_event* process_InitData_input(RtrEvent* ev);
    virtual int getEndpointID(int port);
    virtual Topology::PortState getPortState(int port) const;
    virtual int computeNumVCs(int vns); 
    virtual void getVCsPerVN(std::vector<int>& vns_per_vn);
    
private:
  std::vector<int> dst_to_port_;
  std::vector<int> port_to_dst_;
  std::set<int> rtr_ports_;
  std::set<int> node_ports_;
  int id;
  int num_ports;
  int num_vns;
  int num_vcs;
    
};

}
}

#endif // COMPONENTS_MERLIN_TOPOLOGY_JSON_H
