#!/usr/bin/env python
#
# Copyright 2009-2023 NTESS. Under the terms
# of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
#
# Copyright (c) 2009-2023, NTESS
# All rights reserved.
#
# Portions are copyright of other developers:
# See the file CONTRIBUTORS.TXT in the top level directory
# of the distribution for more information.
#
# This file is part of the SST software package. For license
# information, see the LICENSE file in the top level directory of the
# distribution.

import sst
from sst.merlin.base import *
import json

class RtrOutputJSON:
    def __init__(self,id,name,inport,is_node,is_rtr):
        self.is_node = is_node
        self.is_rtr = is_rtr
        self.name = name
        self.id = id
        self.inport = inport

class RtrConfigJSON:
    def __init__(self, id, name, num_nodes, num_ports):
        self.id = id
        self.name = name
        self.table = [-1] * num_nodes
        self.outputs = [None] * num_ports

class topoJson(Topology):

    def __init__(self):
        Topology.__init__(self)
        self._declareClassVariables(["link_latency","host_link_latency","bundleEndpoints","json:table","json:rtr_ports","json:node_ports"])
        self._declareParams("main",["topo_path", "rtr_path", "node_numbering", "switch_numbering"])
        self._subscribeToPlatformParamSet("topology")
        # self.topo_doc = json.load(open(topo_path))

        if not self.rtr_path:
            self.rtr_doc = self.topo_doc
        else:
            # self.rtr_doc = json.load(open(rtr_path))
            pass
            
        print("In Python constructor")

        self._setCallbackOnWrite("topo_path",self._topo_callback)
    
    def _topo_callback(self, variable_name, value):
        print("In Python callback")
        #Wire up JSON file
        switches = self.topo_doc["switches"]
        swIdx = 0
        for name in switches:
          self.switch_numbering[name] = swIdx
          swIdx += 1
        self.num_routers = swIdx

        self.routers = [None]*self.num_routers

        nodes = self.topo_doc["nodes"]
        nodeIdx = 0
        for name in nodes:
          self.node_numbering[name] = nodeIdx
          nodeIdx += 1
        self.num_nodes = nodeIdx
        _params["num_peers"] = self.num_nodes

        for srcName in switches:
          srcId = self.switch_numbering[srcName]
          switch_json = switches[srcName]
          ports_json = switch_json["outports"]
          max_port = 0
          for portName in ports_json:
            max_port = max(max_port, int(portName))
          num_ports = max_port + 1

          rtr = RtrConfigJSON(srcId, srcName, self.num_nodes, num_ports)
          for portName in ports_json:
            portId = int(portName)
            link_json = ports_json[portName]
            destName = link_json["destination"]
            destInport = int(link_json["inport"])
            if destName in self.node_numbering:
              rtr.outputs[portId] = RtrOutputJSON(self.node_numbering[destName], destName, destInport, True, False)
            else:
              rtr.outputs[portId] = RtrOutputJSON(self.switch_numbering[destName], destName, destInport, False, True)
          self.routers[srcId] = rtr
          
          # Read Routing Tables for each component.
          switches_json = self.topo_doc["switches"]
          for i in iter(range(self.num_routers)):
            rtr = self.routers[i]
            rtr_json = switches_json[rtr.name]
            routes_json = rtr_json["routes"]
            for destName in routes_json:
              destId = self.node_numbering[destName]
              portId = int(routes_json[destName])
              rtr.table[destId] = portId
          
    def getName(self):
        return "JSON Topology"

    def getNumNodes(self):
        return len(self.topo_doc["nodes"])

    def getLink(self, left, leftPort, right, rightPort):
      name = ""
      if left < right:
        name = "link.%s:%d.%s:%d" % (left,leftPort,right,rightPort)
      else:
        name = "link.%s:%d.%s:%d" % (right,rightPort,left,leftPort)
      if not name in self.links:
        self.links[name] = sst.Link(name)
      return self.links[name]
      
    def getRouterNameForId(self,rtr_id):
        return self.getRouterNameForLocation(rtr_id)
        
    def getRouterNameForLocation(self,location):
        return "rtr_%s"%(list(self.topo_doc["switches"].keys())[location])
    
    def findRouterByLocation(self,location):
        return sst.findComponentByName(self.getRouterNameForLocation(location))
    
    def build(self, endpoint):
        print("In Python build")
        if not self.host_link_latency:
            self.host_link_latency = self.link_latency
        node_id = 0
        for i in iter(range(self.num_routers)):
          rtrCfg = self.routers[i]
          num_ports = len(rtrCfg.outputs)
          
          rtr = self._instanceRouter(num_ports, i)
          topology = rtr.setSubComponent(self.router.getTopologySlotName(),"merlin.json")
          self._applyStatisticsSettings(topology)
          topology.addParams(self._getGroupParams("main"))
          
          node_ports = []
          rtr_ports = []
          
          for port in range(num_ports):
            dest = rtrCfg.outputs[port]
            if not dest:
              continue

            if dest.is_rtr:
              rtr_ports.append(port)
              rtr.addLink(self.getLink(rtrCfg.name,port,dest.name,dest.inport), "port%d" % port, _params["link_lat"])
            elif dest.is_node:
              node_ports.append(port)
              (ep, port_name) = endpoint.build(node_id, {})
              if ep:
                  hlink = sst.Link("hostlink_%d"%node_id)
                  if self.bundleEndpoints:
                      hlink.setNoCut()
                  ep.addLink(hlink, port_name, self.host_link_latency)
                  node_id = node_id + 1
          
          if rtr_ports:
            rtr.addParam("json:rtr_ports", rtr_ports)
          if node_ports:
            rtr.addParam("json:node_ports", node_ports)
          
          rtr.addParam("json:table", rtrCfg.table)