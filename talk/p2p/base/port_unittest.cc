/*
 * libjingle
 * Copyright 2004 Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "talk/base/basicpacketsocketfactory.h"
#include "talk/base/gunit.h"
#include "talk/base/helpers.h"
#include "talk/base/host.h"
#include "talk/base/logging.h"
#include "talk/base/natserver.h"
#include "talk/base/natsocketfactory.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/socketaddress.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/p2p/base/relayport.h"
#include "talk/p2p/base/stunport.h"
#include "talk/p2p/base/tcpport.h"
#include "talk/p2p/base/udpport.h"
#include "talk/p2p/base/teststunserver.h"
#include "talk/p2p/base/testrelayserver.h"

using talk_base::AsyncPacketSocket;
using talk_base::NATType;
using talk_base::NAT_OPEN_CONE;
using talk_base::NAT_ADDR_RESTRICTED;
using talk_base::NAT_PORT_RESTRICTED;
using talk_base::NAT_SYMMETRIC;
using talk_base::PacketSocketFactory;
using talk_base::scoped_ptr;
using talk_base::Socket;
using talk_base::SocketAddress;
using namespace cricket;

static const int kTimeout = 1000;
static const SocketAddress kLocalAddr1 = SocketAddress("192.168.1.2", 0);
static const SocketAddress kLocalAddr2 = SocketAddress("192.168.1.3", 0);
static const SocketAddress kNatAddr1 = SocketAddress("77.77.77.77",
                                                    talk_base::NAT_SERVER_PORT);
static const SocketAddress kNatAddr2 = SocketAddress("88.88.88.88",
                                                    talk_base::NAT_SERVER_PORT);
static const SocketAddress kStunAddr = SocketAddress("99.99.99.1",
                                                     STUN_SERVER_PORT);
static const SocketAddress kRelayUdpIntAddr("99.99.99.2", 5000);
static const SocketAddress kRelayUdpExtAddr("99.99.99.3", 5001);
static const SocketAddress kRelayTcpIntAddr("99.99.99.2", 5002);
static const SocketAddress kRelayTcpExtAddr("99.99.99.3", 5003);
static const SocketAddress kRelaySslTcpIntAddr("99.99.99.2", 5004);
static const SocketAddress kRelaySslTcpExtAddr("99.99.99.3", 5005);

// This test message is copied from stun_unittest.
static const unsigned char kRfc5769SampleResponse[] = {
  0x01, 0x01, 0x00, 0x3c,  //     Response type and message length
  0x21, 0x12, 0xa4, 0x42,  //     Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,  // }
  0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,  // }
  0x80, 0x22, 0x00, 0x0b,  //    SOFTWARE attribute header
  0x74, 0x65, 0x73, 0x74,  // }
  0x20, 0x76, 0x65, 0x63,  // }  UTF-8 server name
  0x74, 0x6f, 0x72, 0x20,  // }
  0x00, 0x20, 0x00, 0x08,  //    XOR-MAPPED-ADDRESS attribute header
  0x00, 0x01, 0xa1, 0x47,  //    Address family (IPv4) and xor'd mapped port
  0xe1, 0x12, 0xa6, 0x43,  //    Xor'd mapped IPv4 address
  0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
  0x2b, 0x91, 0xf5, 0x99,  // }
  0xfd, 0x9e, 0x90, 0xc3,  // }
  0x8c, 0x74, 0x89, 0xf9,  // }  HMAC-SHA1 fingerprint
  0x2a, 0xf9, 0xba, 0x53,  // }
  0xf0, 0x6b, 0xe7, 0xd7,  // }
  0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
  0xc0, 0x7d, 0x4c, 0x96   //    CRC32 fingerprint
};


static Candidate GetCandidate(Port* port) {
  assert(port->candidates().size() == 1);
  return port->candidates()[0];
}

static SocketAddress GetAddress(Port* port) {
  return GetCandidate(port).address();
}

class TestPort : public Port {
 public:
  TestPort(talk_base::Thread* thread, const std::string& type,
           talk_base::PacketSocketFactory* factory, talk_base::Network* network,
           const talk_base::IPAddress& ip, int min_port, int max_port,
           const std::string& username_fragment, const std::string& password)
      : Port(thread, type, factory, network, ip, min_port, max_port,
             username_fragment, password)  {}
  ~TestPort() {}

  virtual void PrepareAddress() {}
  virtual Connection* CreateConnection(const Candidate& remote_candidate,
                                       CandidateOrigin origin) { return NULL; }
  virtual int SendTo(
      const void* data, size_t size, const talk_base::SocketAddress& addr,
      bool payload) {return -1;}
  virtual int SetOption(talk_base::Socket::Option opt, int value) { return -1; }
  virtual int GetError() {return -1;}

  using cricket::Port::GetStunMessage;
};

class TestChannel : public sigslot::has_slots<> {
 public:
  TestChannel(Port* p1, Port* p2)
      : src_(p1), dst_(p2), address_count_(0), conn_(NULL),
        remote_request_(NULL) {
    src_->SignalAddressReady.connect(this, &TestChannel::OnAddressReady);
    src_->SignalUnknownAddress.connect(this, &TestChannel::OnUnknownAddress);
  }

  int address_count() { return address_count_; }
  Connection* conn() { return conn_; }
  const SocketAddress& remote_address() { return remote_address_; }
  const std::string remote_fragment() { return remote_frag_; }

  void Start() {
    src_->PrepareAddress();
  }
  void CreateConnection() {
    conn_ = src_->CreateConnection(GetCandidate(dst_), Port::ORIGIN_MESSAGE);
  }
  void AcceptConnection() {
    ASSERT_TRUE(remote_request_.get() != NULL);
    Candidate c = GetCandidate(dst_);
    c.set_address(remote_address_);
    conn_ = src_->CreateConnection(c, Port::ORIGIN_MESSAGE);
    src_->SendBindingResponse(remote_request_.get(), remote_address_);
    remote_request_.reset();
  }
  void Ping() {
    conn_->Ping(0);
  }
  void Stop() {
    conn_->SignalDestroyed.connect(this, &TestChannel::OnDestroyed);
    conn_->Destroy();
  }

  void OnAddressReady(Port* port) {
    address_count_++;
  }

  void OnUnknownAddress(Port* port, const SocketAddress& addr,
                        StunMessage* msg, const std::string& rf,
                        bool /*port_muxed*/) {
    ASSERT_EQ(src_.get(), port);
    if (!remote_address_.IsNil()) {
      ASSERT_EQ(remote_address_, addr);
    }
    // MI attribute shouldn't be present in ping requests.
    const cricket::StunByteStringAttribute* mi_attr =
        msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
    if (src_->ice_protocol() == cricket::ICEPROTO_RFC5245) {
      ASSERT_TRUE(mi_attr != NULL);
    } else {
      ASSERT_TRUE(mi_attr == NULL);
    }
    remote_address_ = addr;
    CopyStunMessage(msg, remote_request_.accept());
    remote_frag_ = rf;
  }

  void CopyStunMessage(const StunMessage* src, StunMessage** dst) {
    talk_base::ByteBuffer buf;
    src->Write(&buf);
    *dst = new StunMessage();
    (*dst)->Read(&buf);
  }

  void OnDestroyed(Connection* conn) {
    ASSERT_EQ(conn_, conn);
    conn_ = NULL;
  }

 private:
  talk_base::Thread* thread_;
  talk_base::scoped_ptr<Port> src_;
  Port* dst_;

  int address_count_;
  Connection* conn_;
  SocketAddress remote_address_;
  talk_base::scoped_ptr<StunMessage> remote_request_;
  std::string remote_frag_;
};

class PortTest : public testing::Test {
 public:
  PortTest()
      : main_(talk_base::Thread::Current()),
        pss_(new talk_base::PhysicalSocketServer),
        ss_(new talk_base::VirtualSocketServer(pss_.get())),
        ss_scope_(ss_.get()),
        network_("unittest", "unittest", talk_base::IPAddress(INADDR_ANY), 32),
        socket_factory_(talk_base::Thread::Current()),
        nat_factory1_(ss_.get(), kNatAddr1),
        nat_factory2_(ss_.get(), kNatAddr2),
        nat_socket_factory1_(&nat_factory1_),
        nat_socket_factory2_(&nat_factory2_),
        stun_server_(main_, kStunAddr),
        relay_server_(main_, kRelayUdpIntAddr, kRelayUdpExtAddr,
                      kRelayTcpIntAddr, kRelayTcpExtAddr,
                      kRelaySslTcpIntAddr, kRelaySslTcpExtAddr),
        username_(talk_base::CreateRandomString(ICE_UFRAG_LENGTH)),
        password_(talk_base::CreateRandomString(ICE_PWD_LENGTH)),
        ice_protocol_(cricket::ICEPROTO_GOOGLE) {
    network_.AddIP(talk_base::IPAddress(INADDR_ANY));
  }

 protected:
  static void SetUpTestCase() {
    // Ensure the RNG is inited.
    talk_base::InitRandom(NULL, 0);
  }

  void TestLocalToLocal() {
    UDPPort* port1 = CreateUdpPort(kLocalAddr1);
    UDPPort* port2 = CreateUdpPort(kLocalAddr2);
    TestConnectivity("udp", port1, "udp", port2, true, true, true, true);
  }
  void TestLocalToStun(NATType type) {
    UDPPort* port1 = CreateUdpPort(kLocalAddr1);
    nat_server2_.reset(CreateNatServer(kNatAddr2, type));
    StunPort* port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    TestConnectivity("udp", port1, StunName(type), port2,
                     type == NAT_OPEN_CONE, true, type != NAT_SYMMETRIC, true);
  }
  void TestLocalToRelay(ProtocolType proto) {
    UDPPort* port1 = CreateUdpPort(kLocalAddr1);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    TestConnectivity("udp", port1, RelayName(proto), port2,
                     true, true, true, true);
  }
  void TestStunToLocal(NATType type) {
    nat_server1_.reset(CreateNatServer(kNatAddr1, type));
    StunPort* port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    UDPPort* port2 = CreateUdpPort(kLocalAddr2);
    TestConnectivity(StunName(type), port1, "udp", port2,
                     true, type != NAT_SYMMETRIC, true, true);
  }
  void TestStunToStun(NATType type1, NATType type2) {
    nat_server1_.reset(CreateNatServer(kNatAddr1, type1));
    StunPort* port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    nat_server2_.reset(CreateNatServer(kNatAddr2, type2));
    StunPort* port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    TestConnectivity(StunName(type1), port1, StunName(type2), port2,
                     type2 == NAT_OPEN_CONE,
                     type1 != NAT_SYMMETRIC, type2 != NAT_SYMMETRIC,
                     type1 + type2 < (NAT_PORT_RESTRICTED + NAT_SYMMETRIC));
  }
  void TestStunToRelay(NATType type, ProtocolType proto) {
    nat_server1_.reset(CreateNatServer(kNatAddr1, type));
    StunPort* port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    TestConnectivity(StunName(type), port1, RelayName(proto), port2,
                     true, type != NAT_SYMMETRIC, true, true);
  }
  void TestTcpToTcp() {
    TCPPort* port1 = CreateTcpPort(kLocalAddr1);
    TCPPort* port2 = CreateTcpPort(kLocalAddr2);
    TestConnectivity("tcp", port1, "tcp", port2, true, false, true, true);
  }
  void TestTcpToRelay(ProtocolType proto) {
    TCPPort* port1 = CreateTcpPort(kLocalAddr1);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_TCP);
    TestConnectivity("tcp", port1, RelayName(proto), port2,
                     true, false, true, true);
  }
  void TestSslTcpToRelay(ProtocolType proto) {
    TCPPort* port1 = CreateTcpPort(kLocalAddr1);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_SSLTCP);
    TestConnectivity("ssltcp", port1, RelayName(proto), port2,
                     true, false, true, true);
  }

  // helpers for above functions
  UDPPort* CreateUdpPort(const SocketAddress& addr) {
    return CreateUdpPort(addr, &socket_factory_);
  }
  UDPPort* CreateUdpPort(const SocketAddress& addr,
                         PacketSocketFactory* socket_factory) {
    UDPPort* port =  UDPPort::Create(main_, socket_factory, &network_,
                                     addr.ipaddr(), 0, 0, username_, password_);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  TCPPort* CreateTcpPort(const SocketAddress& addr) {
    TCPPort* port = CreateTcpPort(addr, &socket_factory_);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  TCPPort* CreateTcpPort(const SocketAddress& addr,
                         PacketSocketFactory* socket_factory) {
    TCPPort* port =  TCPPort::Create(main_, socket_factory, &network_,
                                     addr.ipaddr(), 0, 0, username_, password_,
                                     true);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  StunPort* CreateStunPort(const SocketAddress& addr,
                           talk_base::PacketSocketFactory* factory) {
    StunPort* port =  StunPort::Create(main_, factory, &network_,
                                       addr.ipaddr(), 0, 0,
                                       username_, password_, kStunAddr);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  RelayPort* CreateRelayPort(const SocketAddress& addr,
                             ProtocolType int_proto, ProtocolType ext_proto) {
    RelayPort* port = RelayPort::Create(main_, &socket_factory_, &network_,
                                        addr.ipaddr(), 0, 0,
                                        username_, password_);
    SocketAddress addrs[] =
        { kRelayUdpIntAddr, kRelayTcpIntAddr, kRelaySslTcpIntAddr };
    port->AddServerAddress(ProtocolAddress(addrs[int_proto], int_proto));
    // TODO: Add an external address for ext_proto, so that the
    // other side can connect to this port using a non-UDP protocol.
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  talk_base::NATServer* CreateNatServer(const SocketAddress& addr,
                                        talk_base::NATType type) {
    return new talk_base::NATServer(type, ss_.get(), addr, ss_.get(), addr);
  }
  static const char* StunName(NATType type) {
    switch (type) {
      case NAT_OPEN_CONE:       return "stun(open cone)";
      case NAT_ADDR_RESTRICTED: return "stun(addr restricted)";
      case NAT_PORT_RESTRICTED: return "stun(port restricted)";
      case NAT_SYMMETRIC:       return "stun(symmetric)";
      default:                  return "stun(?)";
    }
  }
  static const char* RelayName(ProtocolType type) {
    switch (type) {
      case PROTO_UDP:           return "relay(udp)";
      case PROTO_TCP:           return "relay(tcp)";
      case PROTO_SSLTCP:        return "relay(ssltcp)";
      default:                  return "relay(?)";
    }
  }

  void TestCrossFamilyPorts(int type);

  // this does all the work
  void TestConnectivity(const char* name1, Port* port1,
                        const char* name2, Port* port2,
                        bool accept, bool same_addr1,
                        bool same_addr2, bool possible);

  void set_ice_protocol(cricket::IceProtocolType protocol) {
    ice_protocol_ = protocol;
  }

  StunMessage* CreateStunMessage(int type) {
    StunMessage* msg = new StunMessage();
    msg->SetType(type);
    msg->SetTransactionID(
        talk_base::CreateRandomString(kStunTransactionIdLength));
    return msg;
  }
  TestPort* CreateTestPort(const std::string& username,
                           const std::string& password) {
    return new TestPort(main_, "test", &socket_factory_, &network_,
                        talk_base::SocketAddress().ipaddr(), 0, 0,
                        username, password);
  }



 private:
  talk_base::Thread* main_;
  talk_base::scoped_ptr<talk_base::PhysicalSocketServer> pss_;
  talk_base::scoped_ptr<talk_base::VirtualSocketServer> ss_;
  talk_base::SocketServerScope ss_scope_;
  talk_base::Network network_;
  talk_base::BasicPacketSocketFactory socket_factory_;
  talk_base::scoped_ptr<talk_base::NATServer> nat_server1_;
  talk_base::scoped_ptr<talk_base::NATServer> nat_server2_;
  talk_base::NATSocketFactory nat_factory1_;
  talk_base::NATSocketFactory nat_factory2_;
  talk_base::BasicPacketSocketFactory nat_socket_factory1_;
  talk_base::BasicPacketSocketFactory nat_socket_factory2_;
  TestStunServer stun_server_;
  TestRelayServer relay_server_;
  std::string username_;
  std::string password_;
  cricket::IceProtocolType ice_protocol_;
};

void PortTest::TestConnectivity(const char* name1, Port* port1,
                                const char* name2, Port* port2,
                                bool accept, bool same_addr1,
                                bool same_addr2, bool possible) {
  LOG(LS_INFO) << "Test: " << name1 << " to " << name2 << ": ";
  port1->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  port2->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up channels.
  TestChannel ch1(port1, port2);
  TestChannel ch2(port2, port1);
  EXPECT_EQ(0, ch1.address_count());
  EXPECT_EQ(0, ch2.address_count());

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_WAIT(1, ch1.address_count(), kTimeout);
  ASSERT_EQ_WAIT(1, ch2.address_count(), kTimeout);

  // Send a ping from src to dst. This may or may not make it.
  ch1.CreateConnection();
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_TRUE_WAIT(ch1.conn()->connected(), kTimeout);  // for TCP connect
  ch1.Ping();
  WAIT(!ch2.remote_address().IsNil(), kTimeout);

  if (accept) {
    // We are able to send a ping from src to dst. This is the case when
    // sending to UDP ports and cone NATs.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_EQ(ch2.remote_fragment(), port1->username_fragment());

    // Ensure the ping came from the same address used for src.
    // This is the case unless the source NAT was symmetric.
    if (same_addr1) EXPECT_EQ(ch2.remote_address(), GetAddress(port1));
    EXPECT_TRUE(same_addr2);

    // Send a ping from dst to src.
    ch2.AcceptConnection();
    ASSERT_TRUE(ch2.conn() != NULL);
    ch2.Ping();
    EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch2.conn()->write_state(),
                   kTimeout);
  } else {
    // We can't send a ping from src to dst, so flip it around. This will happen
    // when the destination NAT is addr/port restricted or symmetric.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_TRUE(ch2.remote_address().IsNil());

    // Send a ping from dst to src. Again, this may or may not make it.
    ch2.CreateConnection();
    ASSERT_TRUE(ch2.conn() != NULL);
    ch2.Ping();
    WAIT(ch2.conn()->write_state() == Connection::STATE_WRITABLE, kTimeout);

    if (same_addr1 && same_addr2) {
      // The new ping got back to the source.
      EXPECT_EQ(Connection::STATE_READABLE, ch1.conn()->read_state());
      EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());

      // First connection may not be writable if the first ping did not get
      // through.  So we will have to do another.
      if (ch1.conn()->write_state() == Connection::STATE_WRITE_CONNECT) {
        ch1.Ping();
        EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch1.conn()->write_state(),
                       kTimeout);
      }
    } else if (!same_addr1 && possible) {
      // The new ping went to the candidate address, but that address was bad.
      // This will happen when the source NAT is symmetric.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());

      // However, since we have now sent a ping to the source IP, we should be
      // able to get a ping from it. This gives us the real source address.
      ch1.Ping();
      EXPECT_TRUE_WAIT(!ch2.remote_address().IsNil(), kTimeout);
      EXPECT_EQ(Connection::STATE_READ_TIMEOUT, ch2.conn()->read_state());
      EXPECT_TRUE(ch1.remote_address().IsNil());

      // Pick up the actual address and establish the connection.
      ch2.AcceptConnection();
      ASSERT_TRUE(ch2.conn() != NULL);
      ch2.Ping();
      EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch2.conn()->write_state(),
                     kTimeout);
    } else if (!same_addr2 && possible) {
      // The new ping came in, but from an unexpected address. This will happen
      // when the destination NAT is symmetric.
      EXPECT_FALSE(ch1.remote_address().IsNil());
      EXPECT_EQ(Connection::STATE_READ_TIMEOUT, ch1.conn()->read_state());

      // Update our address and complete the connection.
      ch1.AcceptConnection();
      ch1.Ping();
      EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch1.conn()->write_state(),
                     kTimeout);
    } else {  // (!possible)
      // There should be s no way for the pings to reach each other. Check it.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
      ch1.Ping();
      WAIT(!ch2.remote_address().IsNil(), kTimeout);
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
    }
  }

  // Everything should be good, unless we know the situation is impossible.
  ASSERT_TRUE(ch1.conn() != NULL);
  ASSERT_TRUE(ch2.conn() != NULL);
  if (possible) {
    EXPECT_EQ(Connection::STATE_READABLE, ch1.conn()->read_state());
    EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_EQ(Connection::STATE_READABLE, ch2.conn()->read_state());
    EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());
  } else {
    EXPECT_NE(Connection::STATE_READABLE, ch1.conn()->read_state());
    EXPECT_NE(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_NE(Connection::STATE_READABLE, ch2.conn()->read_state());
    EXPECT_NE(Connection::STATE_WRITABLE, ch2.conn()->write_state());
  }

  // Tear down and ensure that goes smoothly.
  ch1.Stop();
  ch2.Stop();
  EXPECT_TRUE_WAIT(ch1.conn() == NULL, kTimeout);
  EXPECT_TRUE_WAIT(ch2.conn() == NULL, kTimeout);
}

class FakePacketSocketFactory : public talk_base::PacketSocketFactory {
 public:
  FakePacketSocketFactory()
      : next_udp_socket_(NULL),
        next_server_tcp_socket_(NULL),
        next_client_tcp_socket_(NULL) {
  }
  virtual ~FakePacketSocketFactory() { }

  virtual AsyncPacketSocket* CreateUdpSocket(
      const SocketAddress& address, int min_port, int max_port) {
    EXPECT_TRUE(next_udp_socket_ != NULL);
    AsyncPacketSocket* result = next_udp_socket_;
    next_udp_socket_ = NULL;
    return result;
  }

  virtual AsyncPacketSocket* CreateServerTcpSocket(
      const SocketAddress& local_address, int min_port, int max_port,
      bool ssl) {
    EXPECT_TRUE(next_server_tcp_socket_ != NULL);
    AsyncPacketSocket* result = next_server_tcp_socket_;
    next_server_tcp_socket_ = NULL;
    return result;
  }

  // TODO: |proxy_info| and |user_agent| should be set
  // per-factory and not when socket is created.
  virtual AsyncPacketSocket* CreateClientTcpSocket(
      const SocketAddress& local_address, const SocketAddress& remote_address,
      const talk_base::ProxyInfo& proxy_info,
      const std::string& user_agent, bool ssl) {
    EXPECT_TRUE(next_client_tcp_socket_ != NULL);
    AsyncPacketSocket* result = next_client_tcp_socket_;
    next_client_tcp_socket_ = NULL;
    return result;
  }

  void set_next_udp_socket(AsyncPacketSocket* next_udp_socket) {
    next_udp_socket_ = next_udp_socket;
  }
  void set_next_server_tcp_socket(AsyncPacketSocket* next_server_tcp_socket) {
    next_server_tcp_socket_ = next_server_tcp_socket;
  }
  void set_next_client_tcp_socket(AsyncPacketSocket* next_client_tcp_socket) {
    next_client_tcp_socket_ = next_client_tcp_socket;
  }

 private:
  AsyncPacketSocket* next_udp_socket_;
  AsyncPacketSocket* next_server_tcp_socket_;
  AsyncPacketSocket* next_client_tcp_socket_;
};

class FakeAsyncPacketSocket : public AsyncPacketSocket {
 public:
  // Returns current local address. Address may be set to NULL if the
  // socket is not bound yet (GetState() returns STATE_BINDING).
  virtual SocketAddress GetLocalAddress() const {
    return SocketAddress();
  }

  // Returns remote address. Returns zeroes if this is not a client TCP socket.
  virtual SocketAddress GetRemoteAddress() const {
    return SocketAddress();
  }

  // Send a packet.
  virtual int Send(const void *pv, size_t cb) {
    return cb;
  }
  virtual int SendTo(const void *pv, size_t cb, const SocketAddress& addr) {
    return cb;
  }
  virtual int Close() {
    return 0;
  }

  virtual State GetState() const { return state_; }
  virtual int GetOption(Socket::Option opt, int* value) { return 0; }
  virtual int SetOption(Socket::Option opt, int value) { return 0; }
  virtual int GetError() const { return 0; }
  virtual void SetError(int error) { }

  void set_state(State state) { state_ = state; }

 private:
  State state_;
};

// Local -> XXXX
TEST_F(PortTest, TestLocalToLocal) {
  TestLocalToLocal();
}

TEST_F(PortTest, TestLocalToConeNat) {
  TestLocalToStun(NAT_OPEN_CONE);
}

TEST_F(PortTest, TestLocalToARNat) {
  TestLocalToStun(NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestLocalToPRNat) {
  TestLocalToStun(NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestLocalToSymNat) {
  TestLocalToStun(NAT_SYMMETRIC);
}

TEST_F(PortTest, TestLocalToRelay) {
  TestLocalToRelay(PROTO_UDP);
}

TEST_F(PortTest, TestLocalToTcpRelay) {
  TestLocalToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestLocalToSslTcpRelay) {
  TestLocalToRelay(PROTO_SSLTCP);
}

// Cone NAT -> XXXX
TEST_F(PortTest, TestConeNatToLocal) {
  TestStunToLocal(NAT_OPEN_CONE);
}

TEST_F(PortTest, TestConeNatToConeNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestConeNatToARNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestConeNatToPRNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestConeNatToSymNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestConeNatToRelay) {
  TestStunToRelay(NAT_OPEN_CONE, PROTO_UDP);
}

TEST_F(PortTest, TestConeNatToTcpRelay) {
  TestStunToRelay(NAT_OPEN_CONE, PROTO_TCP);
}

// Address-restricted NAT -> XXXX
TEST_F(PortTest, TestARNatToLocal) {
  TestStunToLocal(NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestARNatToConeNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestARNatToARNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestARNatToPRNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestARNatToSymNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestARNatToRelay) {
  TestStunToRelay(NAT_ADDR_RESTRICTED, PROTO_UDP);
}

TEST_F(PortTest, TestARNATNatToTcpRelay) {
  TestStunToRelay(NAT_ADDR_RESTRICTED, PROTO_TCP);
}

// Port-restricted NAT -> XXXX
TEST_F(PortTest, TestPRNatToLocal) {
  TestStunToLocal(NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToConeNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestPRNatToARNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToPRNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToSymNat) {
  // Will "fail"
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestPRNatToRelay) {
  TestStunToRelay(NAT_PORT_RESTRICTED, PROTO_UDP);
}

TEST_F(PortTest, TestPRNatToTcpRelay) {
  TestStunToRelay(NAT_PORT_RESTRICTED, PROTO_TCP);
}

// Symmetric NAT -> XXXX
TEST_F(PortTest, TestSymNatToLocal) {
  TestStunToLocal(NAT_SYMMETRIC);
}

TEST_F(PortTest, TestSymNatToConeNat) {
  TestStunToStun(NAT_SYMMETRIC, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestSymNatToARNat) {
  TestStunToStun(NAT_SYMMETRIC, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestSymNatToPRNat) {
  // Will "fail"
  TestStunToStun(NAT_SYMMETRIC, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestSymNatToSymNat) {
  // Will "fail"
  TestStunToStun(NAT_SYMMETRIC, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestSymNatToRelay) {
  TestStunToRelay(NAT_SYMMETRIC, PROTO_UDP);
}

TEST_F(PortTest, TestSymNatToTcpRelay) {
  TestStunToRelay(NAT_SYMMETRIC, PROTO_TCP);
}

// Outbound TCP -> XXXX
TEST_F(PortTest, TestTcpToTcp) {
  TestTcpToTcp();
}

/* TODO: Enable these once testrelayserver can accept external TCP.
TEST_F(PortTest, TestTcpToTcpRelay) {
  TestTcpToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestTcpToSslTcpRelay) {
  TestTcpToRelay(PROTO_SSLTCP);
}
*/

// Outbound SSLTCP -> XXXX
/* TODO: Enable these once testrelayserver can accept external SSL.
TEST_F(PortTest, TestSslTcpToTcpRelay) {
  TestSslTcpToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestSslTcpToSslTcpRelay) {
  TestSslTcpToRelay(PROTO_SSLTCP);
}
*/

TEST_F(PortTest, TestTcpNoDelay) {
  TCPPort* port1 = CreateTcpPort(kLocalAddr1);
  int option_value = -1;
  int success = port1->GetOption(talk_base::Socket::OPT_NODELAY,
                                 &option_value);
  ASSERT_EQ(0, success);  // GetOption() should complete successfully w/ 0
  ASSERT_EQ(1, option_value);
  delete port1;
}

TEST_F(PortTest, TestDelayedBindingUdp) {
  FakeAsyncPacketSocket *socket = new FakeAsyncPacketSocket();
  FakePacketSocketFactory socket_factory;

  socket_factory.set_next_udp_socket(socket);
  scoped_ptr<UDPPort> port(
      CreateUdpPort(kLocalAddr1, &socket_factory));

  socket->set_state(AsyncPacketSocket::STATE_BINDING);
  port->PrepareAddress();

  EXPECT_EQ(0U, port->candidates().size());
  socket->SignalAddressReady(socket, kLocalAddr2);

  EXPECT_EQ(1U, port->candidates().size());
}

TEST_F(PortTest, TestDelayedBindingTcp) {
  FakeAsyncPacketSocket *socket = new FakeAsyncPacketSocket();
  FakePacketSocketFactory socket_factory;

  socket_factory.set_next_server_tcp_socket(socket);
  scoped_ptr<TCPPort> port(
      CreateTcpPort(kLocalAddr1, &socket_factory));

  socket->set_state(AsyncPacketSocket::STATE_BINDING);
  port->PrepareAddress();

  EXPECT_EQ(0U, port->candidates().size());
  socket->SignalAddressReady(socket, kLocalAddr2);

  EXPECT_EQ(1U, port->candidates().size());
}

// This test case verifies standard ICE features in STUN messages. Currently it
// verifies Message Integrity attribute in STUN messages and username in STUN
// binding request will have colon (":") between remote and local username.
TEST_F(PortTest, TestRfc5245Features) {
  // TestLocalToLocal.
  set_ice_protocol(cricket::ICEPROTO_RFC5245);
  UDPPort* port1 = CreateUdpPort(kLocalAddr1);
  ASSERT_EQ(cricket::ICEPROTO_RFC5245, port1->ice_protocol());
  UDPPort* port2 = CreateUdpPort(kLocalAddr2);
  ASSERT_EQ(cricket::ICEPROTO_RFC5245, port2->ice_protocol());
  TestConnectivity("udp", port1, "udp", port2, true, true, true, true);
}

void PortTest::TestCrossFamilyPorts(int type) {
  FakePacketSocketFactory factory;
  scoped_ptr<Port> ports[4];
  SocketAddress addresses[4] = {SocketAddress("192.168.1.3", 0),
                                SocketAddress("192.168.1.4", 0),
                                SocketAddress("2001:db8::1", 0),
                                SocketAddress("2001:db8::2", 0)};
  for (int i = 0; i < 4; i++) {
    FakeAsyncPacketSocket *socket = new FakeAsyncPacketSocket();
    if (type == SOCK_DGRAM) {
      factory.set_next_udp_socket(socket);
      ports[i].reset(CreateUdpPort(addresses[i], &factory));
    } else if (type == SOCK_STREAM) {
      factory.set_next_server_tcp_socket(socket);
      ports[i].reset(CreateTcpPort(addresses[i], &factory));
    }
    socket->set_state(AsyncPacketSocket::STATE_BINDING);
    socket->SignalAddressReady(socket, addresses[i]);
    ports[i]->PrepareAddress();
  }

  // IPv4 Port, connects to IPv6 candidate and then to IPv4 candidate.
  if (type == SOCK_STREAM) {
    FakeAsyncPacketSocket* clientsocket = new FakeAsyncPacketSocket();
    factory.set_next_client_tcp_socket(clientsocket);
  }
  Connection* c = ports[0]->CreateConnection(GetCandidate(ports[2].get()),
                                             Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(NULL == c);
  EXPECT_EQ(0U, ports[0]->connections().size());
  c = ports[0]->CreateConnection(GetCandidate(ports[1].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(NULL == c);
  EXPECT_EQ(1U, ports[0]->connections().size());

  // IPv6 Port, connects to IPv4 candidate and to IPv6 candidate.
  if (type == SOCK_STREAM) {
    FakeAsyncPacketSocket* clientsocket = new FakeAsyncPacketSocket();
    factory.set_next_client_tcp_socket(clientsocket);
  }
  c = ports[2]->CreateConnection(GetCandidate(ports[0].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(NULL == c);
  EXPECT_EQ(0U, ports[2]->connections().size());
  c = ports[2]->CreateConnection(GetCandidate(ports[3].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(NULL == c);
  EXPECT_EQ(1U, ports[2]->connections().size());
}

TEST_F(PortTest, TestSkipCrossFamilyTcp) {
  TestCrossFamilyPorts(SOCK_STREAM);
}

TEST_F(PortTest, TestSkipCrossFamilyUdp) {
  TestCrossFamilyPorts(SOCK_DGRAM);
}

TEST_F(PortTest, TestGetStunMessageNoUsername) {
  talk_base::SocketAddress addr;
  std::string username;
  talk_base::scoped_ptr<StunMessage> stun_out_message;

  talk_base::scoped_ptr<TestPort> port(CreateTestPort("username", "password"));
  talk_base::scoped_ptr<StunMessage> test_message(
      CreateStunMessage(STUN_BINDING_REQUEST));

  talk_base::scoped_ptr<talk_base::ByteBuffer> buf(new talk_base::ByteBuffer());
  test_message->Write(buf.get());

  // No username attribute in the message. Since this is a request message,
  // stun_out_message should be NULL.
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() == NULL);
  ASSERT_TRUE(username.empty());
  stun_out_message.reset();

  // Testing no username case with response message. stun_out_message will not
  // be null in this case.
  buf.reset(new talk_base::ByteBuffer());
  test_message->SetType(STUN_BINDING_RESPONSE);
  test_message->Write(buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() != NULL);
  ASSERT_TRUE(username.empty());
  stun_out_message.reset();
}

// Verifies GetStunMessage method using incorrect usernames.
TEST_F(PortTest, TestGetStunMessageIncorrectUsername) {
  talk_base::SocketAddress addr;
  std::string username;
  talk_base::scoped_ptr<StunMessage> stun_out_message;
  talk_base::scoped_ptr<talk_base::ByteBuffer> buf(new talk_base::ByteBuffer());

  talk_base::scoped_ptr<TestPort> port(CreateTestPort("username", "password"));
  talk_base::scoped_ptr<StunMessage> test_message(
      CreateStunMessage(STUN_BINDING_REQUEST));

  // ICE protocol is ICEPROTO_GOOGLE.
  // Out username should be empty along with stun_out_message.
  std::string username_attr_str = "localusernameremoteusername";
  StunByteStringAttribute* username_attr =
      StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  username_attr->CopyBytes(username_attr_str.c_str(), username_attr_str.size());
  test_message->AddAttribute(username_attr);
  test_message->Write(buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() == NULL);
  ASSERT_TRUE(username.empty());
  stun_out_message.reset();

  // ICE protocol is ICEPROTO_RFC5245.
  // Out username should be empty along with stun_out_message.
  buf.reset(new talk_base::ByteBuffer());
  port->set_ice_protocol(ICEPROTO_RFC5245);
  test_message->AddMessageIntegrity("password");
  test_message->Write(buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() == NULL);
  ASSERT_TRUE(username.empty());
  stun_out_message.reset();

  // ICE protocol is ICEPROTO_GOOGLE.
  // Input username is shorter than port local username.
  port->set_ice_protocol(ICEPROTO_GOOGLE);
  test_message.reset(CreateStunMessage(STUN_BINDING_REQUEST));
  std::string short_username = "user";
  buf.reset(new talk_base::ByteBuffer());
  username_attr =  StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  username_attr->CopyBytes(short_username.c_str(), short_username.size());
  test_message->AddAttribute(username_attr);
  test_message->AddMessageIntegrity("password");
  test_message->Write(buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() == NULL);
  ASSERT_TRUE(username.empty());
  stun_out_message.reset();
}

TEST_F(PortTest, TestGetStunMessage) {
  talk_base::SocketAddress addr;
  std::string username;
  talk_base::scoped_ptr<StunMessage> stun_out_message;
  talk_base::scoped_ptr<talk_base::ByteBuffer> buf(new talk_base::ByteBuffer());
  talk_base::scoped_ptr<TestPort> port(CreateTestPort("username", "password"));

  // Valid username present in stun request.
  port->set_ice_protocol(ICEPROTO_RFC5245);
  talk_base::scoped_ptr<StunMessage> test_message(
      CreateStunMessage(STUN_BINDING_REQUEST));
  buf.reset(new talk_base::ByteBuffer());
  StunByteStringAttribute* username_attr =
      StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  std::string username_attr_str = "username:remoteusername";
  username_attr->CopyBytes(username_attr_str.c_str(), username_attr_str.size());
  test_message->AddAttribute(username_attr);
  test_message->AddMessageIntegrity("password");
  test_message->Write(buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() != NULL);
  ASSERT_EQ("remoteusername", username);
  stun_out_message.reset();

  // Passing username without colon to port which has ice protocol type
  // set to ICEPROTO_RFC5245.
  test_message.reset(CreateStunMessage(STUN_BINDING_REQUEST));
  buf.reset(new talk_base::ByteBuffer());
  username_attr =  StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  // GICE style username.
  username_attr_str = "usernameremoteusername";
  username_attr->CopyBytes(username_attr_str.c_str(), username_attr_str.size());
  test_message->AddAttribute(username_attr);
  test_message->AddMessageIntegrity("password");
  test_message->Write(buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() == NULL);
  stun_out_message.reset();

  // Passing stun request message with MI to the port which supports
  // ICEPROTO_GOOGLE. MI attribute should be ignored and GetStunMessage
  // should return valid remote username and stun message.
  port->set_ice_protocol(ICEPROTO_GOOGLE);
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   stun_out_message.accept(), &username));
  ASSERT_TRUE(stun_out_message.get() != NULL);
  ASSERT_EQ("remoteusername", username);
  stun_out_message.reset();
}
