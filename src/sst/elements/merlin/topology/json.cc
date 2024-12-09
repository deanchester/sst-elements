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


#include <sst_config.h>
#include "json.h"

using namespace SST::Merlin;
using namespace std;

topo_json::topo_json(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int num_vns) :
  Topology(cid),
  id(rtr_id),
  num_ports(num_ports),
  num_vns(num_vns),
  num_vcs(-1)
{
  params.find_array("json:table", dst_to_port_);

  if (params.contains("json:rtr_ports")){
    std::vector<int> rtr_ports;
    params.find_array("json:rtr_ports", rtr_ports);
    for (auto port : rtr_ports) rtr_ports_.insert(port);
  }

  if (params.contains("json:node_ports")){
    std::vector<int> node_ports;
    params.find_array("json:node_ports", node_ports);
    for (auto port : node_ports) node_ports_.insert(port);
  }


  std::map<int,int> inj_ports;
  int max_inj_port = 0;
  for (int dst=0; dst < dst_to_port_.size(); ++dst){
    int port = dst_to_port_[dst];
    bool is_inj_port = node_ports_.find(port) != node_ports_.end();
    if (is_inj_port){
      max_inj_port = std::max(max_inj_port, port);
      inj_ports[port] = dst;
    }
  }
  port_to_dst_.resize(max_inj_port+1);
  for (auto& pair : inj_ports){
    port_to_dst_[pair.first] = pair.second;
  }
}

topo_json::~topo_json(){}

/**
 * @brief route
 * @param port input port
 * @param vc
 * @param ev
 */
void topo_json::route_packet(int port, int vc, internal_router_event* ev) {
  int dest = ev->getDest();
  ev->setNextPort(dst_to_port_[dest]);
}

internal_router_event* process_input(RtrEvent* ev) {
  internal_router_event* ire = new internal_router_event(ev);
  ire->setVC(ire->getVN());
  return ire;
}

void topo_json::routeInitData(int port, internal_router_event* ev, std::vector<int> &outPorts) {
  if (ev->getDest() == UNTIMED_BROADCAST_ADDR){
    //SST::Output::getDefaultObject().fatal(CALL_INFO, 1, "INIT_BROADCAST_ADDR unimplemented");
  } else {
    route_packet(port, 0, ev);
    outPorts.push_back(ev->getNextPort());
  }
}

internal_router_event* topo_json::process_InitData_input(RtrEvent* ev) {
  auto* ret = new internal_router_event(ev);
  return ret;
}

int topo_json::getEndpointID(int port) {
  return port_to_dst_[port];
}

Topology::PortState topo_json::getPortState(int port) const{
  if (rtr_ports_.find(port) != rtr_ports_.end()){
    return R2R;
  } else if (node_ports_.find(port) != node_ports_.end()){
    return R2N;
  } else {
    return UNCONNECTED;
  }
}

int topo_json::computeNumVCs(int vns) {return vns;}

void topo_json::getVCsPerVN(std::vector<int>& vns_per_vn){
    return; 
}