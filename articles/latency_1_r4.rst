===========================================
Network Latency with TSN on Virtual Machine
===========================================

This blog series will describe our experiments in evaluating network latency with
IEEE802.1 TSN (Time Sensitive Networking)[1] technology, especially under virtual
machine (hypervisor) environments.

The 1st article will explain our assumptions, test system setup and different
configurations of virtual network interfaces that we are going to evaluate.

Background
==========
Nowaday, Software Defined Vehicle (SDV) has been gaining momentum in the automotive
industry.
A modern vehicle has to deal with information from a number of sources. As well
as data from the powertrain and chassis there are now increasingly Advanced Driver
Assistance Systems (ADAS). The infotainment system has expanded from managing
a radio and maybe a CD player to sourcing data from the outside world via the cloud.
As such, more and more
software components have been integrated in building and creating a modern vehicle.

Linaro's white paper[2] surveys this trend, summarises architectural transformation
along with standardized technologies being deployed in this segment and illustrates
challenges that we are facing now and in the near future.

In the traditional systems, fixed-function Electric Control Unit(ECU)s are connected
directly to the sensors, relays and actuators, creating purpose-specific domains.
Even today, as mentioned in [2], a number of ECUs in a premium car has reached
a massive number of 150 ECUs.

As this number of ECUs grows into the hundreds, there is a push to integrate
multiple services onto a smaller number of ECU servers. The logical next step is
to virtualise their functions onto more powerful centralised ECUs by taking advantage
of hypervisor isolation.

The wiring harness has also been a big problem in terms of space and weight;
According to the article[3], an average vehicle has 100-120 lbs of wire harness
and a luxury car contains 1,500-2000 copper wires or over 1 mile in total length.

In the zonal architecture, electric devices including sensors/actuators will be
connected to ECUs on the servers via zonal gateways.
Networks between them will then be converged and replaced from traditional CANbus
technology to standard ethernet which will bring in a single unified bus as
system-wide networking, simplifying the complexity of network management and
reducing a total cost of physical wiring.

.. figure:: fig_zonal2.png
  :alt: zonal architecture
  :width: 600
  :align: center

  fig. zonal architecture

TSN and Performance
===================

As many of automotive applications require some level of realtimeness,
the default best effort approach in the ethernet protocol isn't suitable
for reliable data exchanges over the network.
Here we have a solution, Time Sensitive Networking (TSN).

TSN is a collection of standards defined by IEEE801.2 and seen as extensions to
IEEE801.Q (VLAN), addressing issues like network latency and robustness.
TSN provides the predictable nature of real-time communications where particular
packets are expected to be delivered within a specific time with minimum jitters
whereas other low-priority traffic are also allowed at best-effort basis.

TSN includes several features like:

- clock synchronization (Precision Time Protocol, or PTP):

  all the clocks of the devices connected to the network, including NICs and routers,
  will be synchronized to behave based on shared time reference.

- various network schedulers/shapers:

  Credit-based scheduler (cbs or Qav) provides fair scheduling, allowing managed
  bandwidth for each traffic, while time-aware shaper (tas or Qbv) enforces
  deterministic delivery under strict packet gate controls.
  There are more schedulers and shapers defined in the standards.

- resource management protocol (SRP):

- multiple paths and frame replication:

With those technologies combined and properly configured, some level of determinism
in packet delivery is guaranteed. This determinism is, however, limited at hardware
or mac layer. As more software components, along with kernel or hypervisor
interventions, get stacked both on sender side and receiver side, software
plays dominant roles in increasing end-to-end latency in specific use cases.

Linaro has been getting involved in TSN related development works and contributed
to bringing them into mainstream for broader user experiences. In the past
Linaro Connect, we also presented our experiments [4],[5] and demonstrated a fast
data path of network utilizing AF_XDP technology for minimizing the software overhead
in kernel network stack.

We are now going to move forward [6]; the ultimate goal would be to optimise some
software overhead and strive to guarantee the worst-case latency under a virtual
machine environment.
As a first step, we will measure and evaluate network performance, especially latency,
for different implementations of virtual network interfaces and then look into
some of potential factors that might impose non-negligible overheads in packet delivery.

Latency Evaluation
==================

Simple scenario
---------------
In a real-world car,
there are plenty of different types of data transmitted between devices/gateways
and ECU servers over the network.
For instance,
side or rear-view cameras should deliver view images in constant intervals to
the center console (cluster). In powertrain and chassis control systems,
more variety of data from sensors must be guaranteed to be transmitted over
the ethernet with strict criteria and ECU application is also expected to be able
to manage devices (like actuators) by sending control messages in a reliable manner.
This type of traffic must be much more deterministic with strict behaviors than
data streaming for infotainment and absolutely critical for the safety system.

While we will have to learn more about system criteria, in particular,
network traffic characters and related requirements, we cannot at this stage
simulate the whole system with realistic applications, instead we would like to
consider a simple use case with two different types of network traffic in this study.

 - latency-critical messages

   Those may mimic periodic data transmission from device as well as
   some control messages from ECU

 - bulk data streams at best-effort base

As far as the network configuration, including TSN, is properly set up and
we assume that all the traffic is arbitrated at some manageable level,
we can expect that this model will tell us enough fundamentals to understand
basic behaviors of system (OS and hypervisor) under mixed and congested network
traffic load.

.. figure:: fig_model.png
  :alt: evaluation model
  :width: 600
  :align: center

  fig. evaluation model

Virtual Network Interface
-------------------------
We use kvm as a hypervisor in this study since there are several choices
available for virtual network configurations.
One of advantages of using kvm is that, as it is integrated in Linux kernel,
using built-in instrument tools makes it easier to examine and analyze
behaviors around networking in the system later on.

a) bridge in host
b) macvtap
c) offload to userspace (vhost-user)

In addition, we will examine PCI pass through for comparison purpose.

d) NIC pass through (of physical device or SR-IOV virtual function)

On the other hand,
we always use virtio-net as a virtual network device on guest VM side
since it is a common hypervisor-agnostic interface and can perform much better
than a user-mode device fully-emulated by qemu.


bridge within host
~~~~~~~~~~~~~~~~~~
A tap device is an in-kernel network interface which corresponds to a virtual
network exposed to a guest VM. Adding a tap device and a physical device on host
OS to the same bridge allows a guest VM to communicate with devices and gateways
participating in the external network.

We assume that vhost-net is on so that we can always save additional cost
in virtqueue handling due to context switches and data copy.

We think of following bridge implementations::

a-1) kernel bridge:

This is a default configuration when a network device is set up with a tap interface.

.. figure:: fig_bridge.png
  :width: 600
  :alt: kernel bridge
  :align: center

  fig. kernel bridge

a-2) Open vSwitch:

The kernel bridge in (a-1) case will be replaced with in-kernel openvswitch module.
Since packets are handled per-interrupt base and vhost-net and virtio components still
remain in the data path, unlike "userspace offload" case below, we will see little
improvement thanks to this simple replacement.

.. figure:: fig_ovs.png
  :width: 600
  :alt: Open vSwitch
  :align: center

  fig. Open vSwitch

a-3) XDP-based bridge:

Two XDP programs are to be installed for ingress and egress paths.
When a new packet arrives at a port, its destination will be looked up in
an internal table by XDP program. If it is destined to guest VM (in ingress case),
it will be redirected to a corresponding tap interface as a fast data path.
Other packets are passed on to the normal kernel network stack (in this case,
kernel bridge) as a slow data path.

.. figure:: fig_xdp.png
  :alt: XDP bridge
  :width: 600
  :align: center

  fig. XDP-based bridge

macvtap
~~~~~~~
Macvtap is a combination of macvlan and tap interface. Each macvtap interface has
its own mac address on an associated physical network interface and all the packets
destined to this address are directly redirected to a guest VM at the low layer
of kernel network stack.

.. figure:: fig_macvtap.png
  :alt: macvtap
  :width: 600
  :align: center

  fig. macvtap

Userspace offload
~~~~~~~~~~~~~~~~~
This is an advanced configuration to skip most of kernel software stack
on host OS.
A userspace application may fetch all the packets directly from a network device
driver and re-routes them to guest VMs via vhost-user in userspace.

Open vSwitch is one of such technologies and can utilize either DPDK (Data Path
Development Kit) or AF_XDP socket to bypass kernel overhead.
AF_XDP support seems to be experimental as of now, though.

.. figure:: fig_offload.png
  :alt: userspace offload
  :width: 600
  :align: center

  fig. userspace offloading

PCI pass through
~~~~~~~~~~~~~~~~
Some NICs have virtual functions of network interfaces (SR-IOV), which are directly
exposed to guest VMs with assistance from VFIO framework and work as isolated network
devices on VMs.
As we will skip most of the overhead imposed by the host or hypervisor, we can expect
the optimal performance in a virtual machine environment.

This solution, however, comes with limitations. First, available virtual functions
are subject to the hardware. Second, managing packets going out of different virtual
machines is complicated and the traffic may not be well arbitrated to satisfy
latency requirements.

.. figure:: fig_passthrough.png
  :alt: PCI pass through
  :width: 600
  :align: center

  fig. PCI pass through

Trailer
=======
In this article, we explained our planned methodology for latency evaluation.
If you have any comments or suggestions on our approach or about assumption on
system configuration, please let us know promptly so that we will perform
more meaningful analysis and develop a more practical approach.

Our next blog in this series will show the initial result of latency measurement
with various virtual network interfaces.


[1] https://1.ieee802.org/tsn/

[2] https://static.linaro.org/assets/automotive_white_paper_0921.pdf

[3] https://semiengineering.com/shedding-pounds-in-automotive-electronics/

[4] https://resources.linaro.org/en/resource/F6xXMMdkS6BjHdqri8Yy76

[5] https://resources.linaro.org/en/resource/TG82UyhpmJafAmEfiaWXP4

[6] https://linaro.atlassian.net/browse/STR-68
