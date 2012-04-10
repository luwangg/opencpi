/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2010
 *
 *    Mercury Federal Systems, Incorporated
 *    1901 South Bell Street
 *    Suite 402
 *    Arlington, Virginia 22202
 *    United States of America
 *    Telephone 703-413-0781
 *    FAX 703-413-0784
 *
 *  This file is part of OpenCPI (www.opencpi.org).
 *     ____                   __________   ____
 *    / __ \____  ___  ____  / ____/ __ \ /  _/ ____  _________ _
 *   / / / / __ \/ _ \/ __ \/ /   / /_/ / / /  / __ \/ ___/ __ `/
 *  / /_/ / /_/ /  __/ / / / /___/ ____/_/ / _/ /_/ / /  / /_/ /
 *  \____/ .___/\___/_/ /_/\____/_/    /___/(_)____/_/   \__, /
 *      /_/                                             /____/
 *
 *  OpenCPI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenCPI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * Abstact:
 *   This file contains the Implementation for the OCPI transport.
 *
 * Revision History: 
 * 
 *    Author: John F. Miller
 *    Date: 1/2005
 *    Revision Detail: Created
 *
 */

#include <algorithm>
#include <DtHandshakeControl.h>
#include <DtTransferInternal.h>
#include <OcpiIntTransportExceptions.h>
#include <DtOsDataTypes.h>
#include <OcpiTransport.h>
#include <OcpiCircuit.h>
#include <OcpiList.h>
#include <OcpiUtilHash.h>
#include <OcpiOsMutex.h>
#include <OcpiOsMisc.h>
#include <OcpiOsAssert.h>
#include <OcpiTransportExceptions.h>
#include <OcpiIntParallelDataDistribution.h>
#include "OcpiUtilAutoMutex.h"

using namespace OCPI::DataTransport;
using namespace OCPI::OS;
using namespace DataTransfer;
namespace OU = OCPI::Util;
namespace OS = OCPI::OS;

struct GEndPoint {
  EndPoint*     loc;
  SMBResources* res;
  std::string   ep;
  ~GEndPoint() 
  {
  };
};


static uint32_t         g_nextCircuitId=0;
//OCPI::OS::Mutex     OCPI::DataTransport::Transport::m_mutex(true);
OCPI::Util::VList   OCPI::DataTransport::Transport::m_cached_transfers;
OCPI::Util::VList   OCPI::DataTransport::Transport::active_transfers;

struct TransferDesc_ {
  XferRequest*  xfer;
  EndPoint* loc;
  int offset;
};
typedef struct TransferDesc_ TransferDesc;

#if 0
std::vector<std::string> 
OCPI::DataTransport::Transport::
getListOfSupportedEndpoints()
{
  return XferFactoryManager::getFactoryManager().getListOfSupportedEndpoints();  
}
#endif

// FIXME have recursive mutex with default constructor
// Constructors
OCPI::DataTransport::Transport::
Transport( TransportGlobal* tpg, bool uses_mailboxes, OCPI::Time::Emit * parent  )
  : OCPI::Time::Emit(parent, "Transport"), m_defEndpoint(NULL),
    m_uses_mailboxes(uses_mailboxes), m_mutex(*new OCPI::OS::Mutex(true)),
    m_nextCircuitId(0), m_CSendpoint(NULL), m_transportGlobal(tpg)
{
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 
  init();
}


OCPI::DataTransport::Transport::
Transport( TransportGlobal* tpg, bool uses_mailboxes )
  : OCPI::Time::Emit("Transport"), m_defEndpoint(NULL),
    m_uses_mailboxes(uses_mailboxes), m_mutex(*new OCPI::OS::Mutex(true)),
    m_nextCircuitId(0), m_CSendpoint(NULL), m_transportGlobal(tpg)
{
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 
  init();
}



void 
OCPI::DataTransport::Transport::
init() {
  if ( g_nextCircuitId == 0 ) {
    srand( time(NULL) );
    g_nextCircuitId = getpid() + rand();
  }

  m_nextCircuitId += ++g_nextCircuitId;

  // At this point we need to ask xfer factory manager for all possible endpoint
  m_endpoints = 
    XferFactoryManager::getFactoryManager().getListOfSupportedEndpoints();  

  std::vector<std::string>::const_iterator it;
  for ( it=m_endpoints.begin(); it!=m_endpoints.end(); it++ ) {
    ocpiDebug("initial supported ep = %s", (*it).c_str() );
  }
}


// Find an endpoint that we can use to communicate with the given endpoint (string).
// It might just be a protocol string
// Create one if there isn't one.
DataTransfer::EndPoint &Transport::
getLocalCompatibleEndpoint(const char *endpoint) {
  if (!endpoint || !endpoint[0])
    endpoint = "ocpi-smb-pio";  // FIXME: some global constant
  // This entry point is for mailbox users, so there cannot be any finalized endpoints yet
  for (unsigned n=0; n<m_localEndpoints.getElementCount(); n++ ) {
    GEndPoint *gep = static_cast<GEndPoint*>(m_localEndpoints[n]);
    if (canSupport(*gep->loc, endpoint))
      throw "Unexpected existing endpoint";
  }
  SMBResources *res = 0;
  if (strchr(endpoint, ':')) {
    // If we have more than protocol, we need a compatible local endpoint to it
    // First see if any of our initial endpoints will work
    // FIXME!!!!!!!!!!!!!!! save these as structures, not strings, so we don't need to reparse.
    char *protocol = strdup(endpoint);
    char *cs = strdup(endpoint);
    uint32_t mailBox, maxMb, size;
    EndPoint::getProtocolFromString(endpoint, protocol);
    EndPoint::getResourceValuesFromString(endpoint, cs, &mailBox, &maxMb, &size);
    std::vector<std::string>::iterator i;
    for (i = m_endpoints.begin(); i != m_endpoints.end(); i++) {
      const char *ep = (*i).c_str();
      char *protocol1 = strdup(ep);
      char *cs1 = strdup(ep);
      uint32_t mailBox1, maxMb1, size1;
      DataTransfer::EndPoint::getProtocolFromString(ep, protocol1);
      DataTransfer::EndPoint::getResourceValuesFromString(ep, cs1, &mailBox1, &maxMb1, &size1);
      if (!strcmp(protocol, protocol1) &&
	  maxMb == maxMb1 &&
	  mailBox != mailBox1) {
	res = addLocalEndpoint((*i).c_str());
      }
      free(protocol1);
      free(cs1);
      if (res)
	break;
	   
    }
    free(protocol);
    free(cs);
    if (!res)
      res = addLocalEndpoint(endpoint, true);
  } else
    // Just a protocol
    res = findLocalCompatibleEndpoint(endpoint);
  if (!res)
    throw UnsupportedEndpointEx(endpoint);
  return *res->sMemServices->endpoint();
}

SMBResources* 
OCPI::DataTransport::Transport::
findLocalCompatibleEndpoint( const char* ep )
{
  ocpiDebug("Finding compatible endpoint for %s", ep );

  std::vector<std::string>::iterator it;
  for ( it=m_endpoints.begin(); it!=m_endpoints.end(); it++ ) {
    ocpiDebug("ep = %s", (*it).c_str() );
    int m=0;
    while ( (*it).c_str()[m] || ep[m] ) {

      if (ep[m]) {
	if ((*it).c_str()[m] != ep[m] )
	  break;
      } else if ((*it).c_str()[m] != ':')
	break;
      if ( ( (*it).c_str()[m] == ':') || ep[m] == ':' ) {

        // Make sure that the endpoint is finalized
	SMBResources *res = addLocalEndpoint( (*it).c_str() );
	
        (*it) = res->sMemServices->endpoint()->end_point; 
        ocpiDebug("Found %s for %s", (*it).c_str(), ep );
        return res;
      }
      m++;
    }
  }
  throw UnsupportedEndpointEx(ep);
  return NULL;
}


/**********************************
 * This method gets the Node-wide mutex used to lock our mailbox
 * on the specified endpoint for mailbox communication
 *********************************/
struct MailBoxLock {
  Mutex *mutex;
  OCPI::OS::uint32_t hash_code;
  ~MailBoxLock()
  {
    //    mutex->unlock();
    delete mutex;
  }
};


OCPI::DataTransport::Transport::~Transport()
{
  {
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 
  OCPI::OS::uint32_t m;

  for ( m=0; m<m_cached_transfers.size(); m++ ) {
    TransferDesc* tmp_td = static_cast<TransferDesc*>(m_cached_transfers[m]);
    delete tmp_td;
  }
  m_cached_transfers.destroyList();

  for ( m=0; m<active_transfers.size();  m++ ) {
    XferRequest* xr = static_cast<XferRequest*>(active_transfers[m]);
    delete xr;
  }
  active_transfers.destroyList();

  for ( m=0; m<m_mailbox_locks.size(); m++ ) {
    MailBoxLock* mb = static_cast<MailBoxLock*>(m_mailbox_locks[m]);
    delete mb;
  }
  m_mailbox_locks.destroyList();

  // Remove our children before our reference is removed
  Circuit * c = firstChild();
  while ( c ) {
    Circuit * next_c =  c->nextChild();
    c->release();
    c = next_c;
  }

  unsigned int n;
  for ( n=0; n<m_remoteEndpoints.getElementCount(); n++ ) {
    delete static_cast<GEndPoint*>(m_remoteEndpoints[n]);
  }
  for ( n=0; n<m_localEndpoints.getElementCount(); n++ ) { 
    delete static_cast<GEndPoint*>(m_localEndpoints[n]);
  }
  }
  delete &m_mutex;
}


void 
OCPI::DataTransport::Transport::
setNewCircuitRequestListener( NewCircuitRequestListener* listener )
{
  m_newCircuitListener = listener;
}



Mutex* OCPI::DataTransport::Transport::getMailBoxLock( const char* mbid )
{

  OCPI::OS::uint32_t hash = OU::Misc::hashCode( mbid );
  OCPI::OS::int32_t len = m_mailbox_locks.getElementCount();
  for ( int n=0; n<len; n++ ) {
    MailBoxLock* mb = static_cast<MailBoxLock*>(m_mailbox_locks.getEntry(n));
    if ( mb->hash_code == hash ) {
      return mb->mutex;
    }
  }

  // We didnt find one, so create one
  MailBoxLock *mbl = new MailBoxLock;
  mbl->hash_code = hash;
  mbl->mutex = new Mutex();
  m_mailbox_locks.insert( mbl );
  return mbl->mutex;
}



void 
OCPI::DataTransport::Transport::
requestNewConnection( Circuit* circuit, bool send, const char *protocol, OS::Timer *timer)
{
  std::string& input_loc = circuit->getInputPortSet(0)->getPortFromIndex(0)->getMetaData()->real_location_string;
  std::string& output_loc = circuit->getOutputPortSet()->getPortFromIndex(0)->getMetaData()->real_location_string;

  std::string*  server_loc;
  std::string*  client_loc;
  std::string   nuls;

  if ( !send ) {
    server_loc = &output_loc;
    client_loc = &input_loc;
  }
  else {
    client_loc = &output_loc;
    server_loc = &input_loc;
  }

  if ( ! isLocalEndpoint ( server_loc->c_str() ) ) {
    addRemoteEndpoint( server_loc->c_str() );
  }

  ocpiDebug("requestNewConnection: c: %s s: %s", client_loc->c_str(), server_loc->c_str());
  XferFactory* tfactory = 
    XferFactoryManager::getFactoryManager().find( *client_loc, nuls );
  if ( ! tfactory ) {
    throw UnsupportedEndpointEx(client_loc->c_str());
  }
  EndPoint* client_location = tfactory->getEndPoint( *client_loc );

  Mutex* mutex = getMailBoxLock(server_loc->c_str());
  mutex->lock();

  SMBResources* s_res = 
    XferFactoryManager::getFactoryManager().getSMBResources( *client_loc );
  SMBResources* t_res = 
    XferFactoryManager::getFactoryManager().getSMBResources( *server_loc );

  XferMailBox xmb(client_location->mailbox );
  bool openCircuit = circuit->m_openCircuit;
  circuit->m_openCircuit = true;
  while ( ! xmb.mailBoxAvailable(s_res) ) {
    dispatch();
    OCPI::OS::sleep(0);
    if (timer && timer->expired()) {
      mutex->unlock();
      throw OCPI::Util::EmbeddedException("Server Not Responding");
    }
  }
  circuit->m_openCircuit = openCircuit;

  ocpiDebug("Client is making a request to server to establish new connection.");
   
  DataTransfer::ContainerComms::MailBox* mb = xmb.getMailBox( s_res );
  mb->request.reqNewConnection.type = DataTransfer::ContainerComms::ReqNewConnection;
  mb->request.reqNewConnection.circuitId = circuit->getCircuitId();
  mb->request.reqNewConnection.buffer_size = circuit->getOutputPortSet()->getBufferLength();
  mb->request.reqNewConnection.send = send ? 1 : 0;
  strcpy(mb->request.reqNewConnection.output_end_point, m_CSendpoint->end_point.c_str() );
  if (protocol) {
    // If we have protocol info, we will asking the server for a place to put it.
    // We first must allocate space on our side of the transfer, and copy the protocol data
    // into our local smb buffer.  Then later we can transfer it to the server side when we
    // find out where the server's protocol buffer is.  We will do that when the server asks us
    // for our output flow control offsets.
    uint64_t protocolOffset;
    uint32_t protocolSize = strlen(protocol) + 1;
    mb->request.reqNewConnection.protocol_size = protocolSize;
    if (s_res->sMemResourceMgr->alloc(protocolSize, 0, &protocolOffset))
      throw OCPI::Util::EmbeddedException(NO_MORE_BUFFER_AVAILABLE, "for protocol info exchange");
    void *myProtocolBuffer = s_res->sMemServices->map(protocolOffset, protocolSize);
    memcpy(myProtocolBuffer, protocol, protocolSize);
    s_res->sMemServices->unMap();    
    circuit->setProtocolInfo(protocolSize, protocolOffset);
  } else
    mb->request.reqNewConnection.protocol_size = 0;
   
  // For now, this request does not require a return
  mb->return_offset = -1;
  mb->return_size = 0;
  mb->returnMailboxId = client_location->mailbox;

  ocpiDebug("reqNewConnection: mb %p 0x%x type %d", mb, circuit->getCircuitId(),
	    mb->request.reqNewConnection.type);
 
  xmb.makeRequest( s_res, t_res );

  // We will lock here a prescibed amount of time to see if we are successful
  while ( ! xmb.mailBoxAvailable(s_res) ) {
    if (timer && timer->expired()) {
      mutex->unlock();
      throw OCPI::Util::EmbeddedException("Server Not Responding");
    }
    dispatch();
    OCPI::OS::sleep(1);
  }
  if ( mb->error_code ) {
    mutex->unlock();
    throw OCPI::Util::EmbeddedException("Failed to create client connection");
  }

  mutex->unlock();
}



Circuit *
OCPI::DataTransport::Transport::
createCircuit( 
              CircuitId&   cid,
              ConnectionMetaData* connection,
              PortOrdinal src_ports[],
              PortOrdinal dest_ports[],
              OCPI::OS::uint32_t flags,
	      const char *protocol,
	      OS::Timer *timer
              )                        
{

  // Make sure that this circuit does not already exist
  OCPI::DataTransport::Circuit* circuit = getCircuit( cid );
  if ( circuit ) {
    deleteCircuit( circuit->getCircuitId() );
  }
  if ( supportsMailboxes() ) {
    circuit = new OCPI::DataTransport::Circuit( this, cid, connection, src_ports, dest_ports );
  }
  else {
    // NOTE:: This needs to be specialized for optimization
    circuit = new OCPI::DataTransport::Circuit( this, cid, connection, src_ports, dest_ports );
  }

  m_circuits.push_back( circuit );
  ocpiDebug("New circuit created and registered: id %x flags %x", circuit->getCircuitId(), flags);

  // We may need to make a new connection request
  if ( flags & NewConnectionFlag ) {
    try {
      requestNewConnection( circuit, (flags & SendCircuitFlag) != 0, protocol, timer );
    }
    catch( ... ) {
      deleteCircuit( circuit->getCircuitId() );
      throw;
    }
  }
  return circuit;
}



Circuit *
OCPI::DataTransport::Transport::
createCircuit( 
              const char*   id,         
              ConnectionMetaData* connection,        
              PortOrdinal src_ports[],        
              PortOrdinal dest_ports[],
              uint32_t flags,
	      const char *protocol,
	      OS::Timer *timer
              )
{
  ( void ) id;
  CircuitId cid;
  cid = this->m_nextCircuitId++;
  return createCircuit( cid, connection, src_ports, dest_ports, flags, protocol, timer);
}



// ports in the connection are used.
Circuit * 
OCPI::DataTransport::Transport::
createCircuit( OCPI::RDT::Descriptors& sPortDesc )
{
  Circuit * c;
  // Create the port connection meta-data
  OCPI::DataTransport::ConnectionMetaData* cmd = 
    new OCPI::DataTransport::ConnectionMetaData( sPortDesc );
  c = createCircuit(NULL ,cmd);
  return c;
}

// Create an output given a descriptor from a remote input port
// Also returning a flowcontrol descriptor to give to that remote port
OCPI::DataTransport::Port * 
OCPI::DataTransport::Transport::
createOutputPort(OCPI::RDT::Descriptors& outputDesc,
		 const OCPI::RDT::Descriptors& inputDesc )
{
  // Before creating the output port, we need to 
  // create a local endpoint that is compatible with the remote.
  strcpy(outputDesc.desc.oob.oep, 
	 findLocalCompatibleEndpoint(inputDesc.desc.oob.oep )->
	 sMemServices->endpoint()->end_point.c_str());
  // Ensure that the input port endpoint is registered
  addRemoteEndpoint(inputDesc.desc.oob.oep);
  if (outputDesc.desc.dataBufferSize > inputDesc.desc.dataBufferSize)
    outputDesc.desc.dataBufferSize = inputDesc.desc.dataBufferSize;

  Circuit *c = createCircuit(outputDesc);
  c->addInputPort(inputDesc, outputDesc.desc.oob.oep);

  Port *p = c->getOutputPort();
  p->getPortDescriptor(outputDesc, &inputDesc);
  return p;
}
// Create an output port given an existing input port.
OCPI::DataTransport::Port * 
OCPI::DataTransport::Transport::
createOutputPort(OCPI::RDT::Descriptors& outputDesc,
		 OCPI::DataTransport::Port &inputPort )
{
  // With an inside connection, the endpoints are the same
  strcpy(outputDesc.desc.oob.oep, inputPort.getEndpoint()->end_point.c_str());
  if (outputDesc.desc.dataBufferSize > inputPort.getMetaData()->m_descriptor.desc.dataBufferSize) {
    ocpiDebug("Forcing output buffer size to %u from input size %u on local connection",
	   inputPort.getMetaData()->m_descriptor.desc.dataBufferSize,
	   outputDesc.desc.dataBufferSize);	   
    outputDesc.desc.dataBufferSize = inputPort.getMetaData()->m_descriptor.desc.dataBufferSize;
  }

  inputPort.getCircuit()->finalize(outputDesc.desc.oob.oep);

  Port *p = inputPort.getCircuit()->getOutputPort();
  p->getPortDescriptor(outputDesc, NULL);
  return p;
}

OCPI::DataTransport::Port * 
OCPI::DataTransport::Transport::
createInputPort( Circuit * &circuit,  OCPI::RDT::Descriptors& desc, const OU::PValue *params )
{
  // First, process params to establish the right endpoint
  DataTransfer::SMBResources *res = NULL;
  const char *endpoint = NULL, *protocol = NULL;
  bool found = false;
  if ((found = OU::findString(params, "protocol", protocol)) ||
      (found = OU::findString(params, "transport", protocol)) ||
      (protocol = getenv("OCPI_DEFAULT_PROTOCOL"))) {
    if (!found)
      ocpiDebug("Forcing protocol = %s because OCPI_DEFAULT_PROTOCOL set in environment", protocol);
    res = addLocalEndpointFromProtocol(protocol);
  } else {
    // It is up to the caller to specify the endpoint which implicitly defines
    // the QOS.  If the caller does not provide the endpoint, we will pick one
    // by default.
    OU::findString(params, "endpoint", endpoint);
    res = addLocalEndpoint(endpoint);
  }
  if ( ! res ) {
    throw OCPI::Util::Error("Endpoint not supported: protocol \"%s\" endpoint \"%s\"",
			    protocol ? protocol : "", endpoint ? endpoint : "");
  }
  std::string &eps = res->sMemServices->endpoint()->end_point;
  strcpy(desc.desc.oob.oep, eps.c_str());
  
  int ord=-1;
  OCPI::DataTransport::Port * dtPort=NULL;  
  
  // For sake of efficiency we make sure to re-use the circuits that relate 
  // to the same connecton
  if ( circuit  ) {
    if ( circuit->getInputPortSetCount() ) {
      ord = 1 + circuit->getInputPortSet(0)->getPortCount();
    }
    else {
      ord = 1;
    }      

    // Create the port meta-data
    OCPI::DataTransport::PortSetMetaData* psmd;
    if ( ! circuit->getInputPortSet(0) ) {
      psmd = new OCPI::DataTransport::PortSetMetaData(  false, 1,new OCPI::DataTransport::ParallelDataDistribution(), 
                                                       desc.desc.nBuffers,
                                                       desc.desc.dataBufferSize,
                                                       circuit->getConnectionMetaData() );
    }
    else {
      psmd = circuit->getInputPortSet(0)->getPsMetaData();
    }
    dtPort = circuit->addPort( new OCPI::DataTransport::PortMetaData( ord, desc, psmd) );
    circuit->updatePort( dtPort );
  }
  else {

    // Create the port meta-data
    OCPI::DataTransport::ConnectionMetaData* cmd = 
      new OCPI::DataTransport::ConnectionMetaData( NULL,
                                                     eps.c_str(),
                                                     desc.desc.nBuffers,
                                                     desc.desc.dataBufferSize );
    ord = 1;
    circuit = createCircuit(NULL ,cmd);

    OCPI::DataTransport::PortSet* ps = circuit->getInputPortSet(0);
    dtPort = ps->getPortFromOrdinal(ord);    
  }

  return dtPort;
}

OCPI::DataTransport::Port * 
OCPI::DataTransport::Transport::
createInputPort(OCPI::RDT::Descriptors& desc, const OU::PValue *params )
{
  Circuit *circuit = 0;
  Port *port = createInputPort(circuit, desc, params );
  circuit->attach(); // FIXME: why wouldn't port creation do the attach?
  // Merge port descriptor info between what was passed in and what is determined here.
  port->getPortDescriptor(desc, NULL);
 // Make sure the dtport's descriptor is consistent
  //port->getMetaData()->m_descriptor = desc;
  return port;
}

OCPI::DataTransport::Circuit* 
OCPI::DataTransport::Transport::
getCircuit(  CircuitId& circuit_id )
{
  std::vector<OCPI::DataTransport::Circuit*>::iterator cit;
  for ( cit=m_circuits.begin(); cit!=m_circuits.end(); cit++) {
    if ( (*cit)->getCircuitId() == circuit_id ) {
      return (*cit);
    }
  }
  return NULL;
}


OCPI::OS::uint32_t 
OCPI::DataTransport::Transport::
getCircuitCount()
{
  return m_circuits.size();
}


void 
OCPI::DataTransport::Transport::
deleteCircuit( CircuitId circuit_ord )
{
  OCPI::DataTransport::Circuit* circuit = getCircuit( circuit_ord );
  if (circuit)
    deleteCircuit(circuit);
}

void
OCPI::DataTransport::Transport::
deleteCircuit(Circuit *circuit)
{
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 

  std::vector<OCPI::DataTransport::Circuit*>::iterator it = std::find(m_circuits.begin(), m_circuits.end(), circuit);
  m_circuits.erase(it);
  delete circuit;
  if ( m_circuits.size() == 0 ) {
    for ( OCPI::OS::uint32_t m=0; m<m_cached_transfers.getElementCount(); m++ ) {
      TransferDesc* tmp_td = static_cast<TransferDesc*>(m_cached_transfers[m]);
      m_cached_transfers.remove( tmp_td );
      delete tmp_td;
    }
  }
}



/**********************************
 * General house keeping 
 *********************************/
void OCPI::DataTransport::Transport::dispatch(DataTransfer::EventManager*)
{
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 

  // move data from queue if possible
  std::vector<OCPI::DataTransport::Circuit*>::iterator cit;
  for ( cit=m_circuits.begin(); cit!=m_circuits.end(); cit++) {
    if ( (*cit) == NULL ) continue;
    if ( (*cit)->ready() ) {
      // (*cit)->initializeDataTransfers();
      (*cit)->checkQueuedTransfers();
    }
  }

  // handle mailbox requests
  if ( m_uses_mailboxes )
    checkMailBoxs();
  //  OCPI::OS::sleep(1);

}


void OCPI::DataTransport::Transport::clearRemoteMailbox( OCPI::OS::uint32_t offset, EndPoint* loc )
{
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 
  TransferDesc* td=NULL;

#ifdef DEBUG_L2
  ocpiDebug("Clearing remote mailbox address = %s, offset = 0x%x", loc->end_point, offset );
#endif

  for ( OCPI::OS::uint32_t m=0; m<m_cached_transfers.getElementCount(); m++ ) {
    TransferDesc* tmp_td = static_cast<TransferDesc*>(m_cached_transfers[m]);
      if ( (tmp_td->loc->end_point == loc->end_point) && ((OCPI::OS::uint32_t)tmp_td->offset == offset ) ) {
      td = tmp_td;
      while ( td->xfer->getStatus() ) {
#ifdef DEBUG_L2
        ocpiDebug("Request to clear the remote mailbox has not yet completed");
#endif
      }
    }
  }

  if (  ! td ) {

    /* Attempt to get or make a transfer template */
    XferServices* ptemplate = 
      XferFactoryManager::getFactoryManager().getService( m_CSendpoint, 
                                      loc );
    if ( ! ptemplate ) {
      ocpiAssert(0);
    }

    XferRequest* ptransfer = ptemplate->createXferRequest();

    // Create the copy in the template

    ptransfer->copy (
		     offset + sizeof(ContainerComms::BasicReq),
		     offset + sizeof(ContainerComms::BasicReq),
		     sizeof(ContainerComms::MailBox) - sizeof(ContainerComms::BasicReq),
		     XferRequest::FirstTransfer );
                
    ptransfer->copy (
		     offset,
		     offset,
		     sizeof(ContainerComms::BasicReq),
		     XferRequest::LastTransfer );

    ptransfer->post();

    // Cache it
    TransferDesc *trd = new TransferDesc;
    trd->loc = loc;
    trd->offset = offset;
    trd->xfer = ptransfer;
    m_cached_transfers.push_back( trd );

  }
  else {
    ocpiAssert( td->xfer->getStatus() == 0 );
    td->xfer->post();
  }
}

void OCPI::DataTransport::Transport::
sendOffsets( OCPI::Util::VList& offsets, std::string& remote_ep, 
	     uint32_t extraSize, uint64_t extraFrom, uint64_t extraTo)
{
  OCPI::Util::AutoMutex guard ( m_mutex, true ); 

 FORSTART:

  for ( OCPI::OS::uint32_t m=0; m<active_transfers.getElementCount();  m++ ) {
    XferRequest* xr = static_cast<XferRequest*>(active_transfers[m]);
    if ( xr->getStatus() == 0 ) {
      XferRequest* for_delete = xr;
      active_transfers.remove( xr );
      delete for_delete;
      goto FORSTART;
    }
  }
  /* Attempt to get or make a transfer template */
  XferServices* ptemplate = 
    XferFactoryManager::getFactoryManager().getService( m_CSendpoint->end_point, 
                                    remote_ep );
  if ( ! ptemplate ) {
    ocpiAssert(0);
  }

#ifdef DEBUG_L2
  ocpiDebug("In OCPI::DataTransport::Transport::sendOffsets, sending %d OCPI::OS::int32_ts", offsets.size() );
#endif

  XferRequest* ptransfer = ptemplate->createXferRequest();

  // We do the extra transfer first so that the other side will have the protocol when it
  // sees that the output offsets have been copied.
  if (extraSize)
    ptransfer->copy (extraFrom, extraTo, extraSize, XferRequest::None);

  for ( OCPI::OS::uint32_t y=0; y<offsets.getElementCount(); y++) {

    OCPI::DataTransport::Port::ToFrom* tf = 
      static_cast<OCPI::DataTransport::Port::ToFrom*>(offsets[y]);

#ifdef DEBUG_L3
    ocpiDebug("Adding copy to transfer list, 0x%x to 0x%x", tf->from_offset,tf->to_offset );
#endif
    ptransfer->copy (
		     tf->from_offset,
		     tf->to_offset,
		     sizeof(OCPI::OS::uint32_t),
		     XferRequest::None );

  }
  ptransfer->post();
  active_transfers.push_back( ptransfer );
}



SMBResources* Transport::getEndpointResourcesFromMailbox(OCPI::OS::uint32_t mb )
{
  OCPI::OS::uint32_t n;
  for ( n=0; n<m_remoteEndpoints.getElementCount(); n++ ) {
    if ( mb == ((GEndPoint*)m_remoteEndpoints.getEntry(n))->loc->mailbox ) {
      return ((GEndPoint*)m_remoteEndpoints.getEntry(n))->res;
    }
  }
  for ( n=0; n<m_localEndpoints.getElementCount(); n++ ) {
    if ( mb == ((GEndPoint*)m_localEndpoints.getEntry(n))->loc->mailbox ) {
      return ((GEndPoint*)m_localEndpoints.getEntry(n))->res;
    }
  }
  return NULL;
}

                                                                

/**********************************
 * Our mailbox handler
 *********************************/
static volatile int nc=0;
void OCPI::DataTransport::Transport::checkMailBoxs()
{
  nc++;

  // Ignore our request slot
  DataTransfer::ContainerComms* comms =   getEndpointResources(m_CSendpoint->end_point.c_str())->m_comms;

  // See if we have any comms requests
  unsigned nMailBoxes = m_CSendpoint->maxCount;
  for ( OCPI::OS::uint32_t n=0; n<nMailBoxes; n++ ) {

    if ( (n != m_CSendpoint->mailbox ) && (comms->mailBox[n].request.reqBasic.type != 0) ) {

      ocpiDebug("Got a mailbox request from %d, req = %d", n, 
             comms->mailBox[n].request.reqBasic.type);

      switch ( comms->mailBox[n].request.reqBasic.type ) {

      case DataTransfer::ContainerComms::ReqUpdateCircuit:
        {


          CircuitId circuit_id = comms->mailBox[n].request.reqUpdateCircuit.receiverCircuitId;
          ocpiDebug("Handling case DataTransfer::ContainerComms::ReqUpdateCircuit: 0x%x\n", circuit_id);
          OCPI::DataTransport::Circuit* c = 
            static_cast<OCPI::DataTransport::Circuit*>(getCircuit(circuit_id ));

          c->updateInputs( &comms->mailBox[n].request.reqUpdateCircuit );

          // Clear our mailbox
          comms->mailBox[n].error_code = 0;
          comms->mailBox[n].request.reqBasic.type = DataTransfer::ContainerComms::NoRequest;

          // Clear the remote mailbox
          XferFactory* tfactory = 
            XferFactoryManager::getFactoryManager().find( comms->mailBox[n].request.reqUpdateCircuit.output_end_point, NULL );
          if ( ! tfactory ) {
            throw UnsupportedEndpointEx(comms->mailBox[n].request.reqUpdateCircuit.output_end_point);
          }

          // We will copy our copy of their mailbox back to them
          int offset = sizeof(UpAndRunningMarker)+sizeof(ContainerComms::MailBox)*n;
          std::string s(comms->mailBox[n].request.reqUpdateCircuit.output_end_point);
          clearRemoteMailbox( offset, tfactory->getEndPoint( s ) );
        }
        break;

        // New connection request
      case DataTransfer::ContainerComms::ReqNewConnection:
        {
	  CircuitId circuit_id = comms->mailBox[n].request.reqNewConnection.circuitId;
          ocpiDebug("Handling case DataTransfer::ContainerComms::ReqNewConnection: 0x%x", circuit_id);
          try {

            // If we dont have a new circuit listener installed, ignore the request
            if ( ! m_newCircuitListener ) {
              nc--;
              return;
            }

            ConnectionMetaData* md=NULL;
            Circuit* c=NULL;
            addRemoteEndpoint(comms->mailBox[n].request.reqNewConnection.output_end_point);
            try {

              // send flag indicates that the client is requesting a circuit to send data to me
              if ( comms->mailBox[n].request.reqNewConnection.send ) {

                std::string s(comms->mailBox[n].request.reqNewConnection.output_end_point);
                md = new ConnectionMetaData( s.c_str(), 
                                             m_CSendpoint->end_point.c_str(), 1, 
                                             comms->mailBox[n].request.reqNewConnection.buffer_size  );
              }
              else {
                                                                        
                std::string s(comms->mailBox[n].request.reqNewConnection.output_end_point);
                md = new ConnectionMetaData( m_CSendpoint->end_point.c_str(),
					     s.c_str(),  1, 
					     comms->mailBox[n].request.reqNewConnection.buffer_size );
              }

              // Create the new circuit on request from the other side (client)
	      // If the client has protocol info for us, allocate local smb space for it, so it can
	      // copy it to me when I tell it where to put it in the request for output control offsets;
	      uint64_t protocolOffset = 0;
	      uint32_t protocolSize = comms->mailBox[n].request.reqNewConnection.protocol_size;
	      if (protocolSize) {
		// Allocate local space
		if (m_CSendpoint->resources->sMemResourceMgr->alloc(protocolSize, 0,  &protocolOffset))
		  throw OCPI::Util::EmbeddedException(NO_MORE_BUFFER_AVAILABLE, "for protocol info exchange");
		// map in local space
		
	      }
              c = createCircuit(circuit_id, md);
	      c->setProtocolInfo(protocolSize, protocolOffset);
            }
            catch ( ... ) {

              delete md;
              delete c;

              // Clear our mailbox
              comms->mailBox[n].error_code = -1;
              comms->mailBox[n].request.reqBasic.type = DataTransfer::ContainerComms::NoRequest;

              // Clear the remote mailbox
              XferFactory* tfactory = 
                XferFactoryManager::getFactoryManager().find( comms->mailBox[n].request.reqNewConnection.output_end_point, NULL );

              // We will copy our copy of their mailbox back to them
              int offset = sizeof(UpAndRunningMarker)+sizeof(ContainerComms::MailBox)*n;
              std::string s(comms->mailBox[n].request.reqNewConnection.output_end_point);
              clearRemoteMailbox( offset, tfactory->getEndPoint( s ));

              throw;
            }

            // Clear our mailbox
            comms->mailBox[n].error_code = 0;
            comms->mailBox[n].request.reqBasic.type = DataTransfer::ContainerComms::NoRequest;

            // Clear the remote mailbox
            XferFactory* tfactory = 
              XferFactoryManager::getFactoryManager().find( comms->mailBox[n].request.reqNewConnection.output_end_point, NULL );
            if ( ! tfactory ) {
              throw UnsupportedEndpointEx(comms->mailBox[n].request.reqNewConnection.output_end_point);
            }

            // We will copy our copy of their mailbox back to them
            int offset = sizeof(UpAndRunningMarker)+sizeof(ContainerComms::MailBox)*n;
            std::string s(comms->mailBox[n].request.reqNewConnection.output_end_point);
            clearRemoteMailbox( offset, tfactory->getEndPoint( s ) );

            // Hand it back to the listener
            m_newCircuitListener->newCircuitAvailable( c );

          }
          catch( ... ) {
            throw;
          }
        }
        break;


      case DataTransfer::ContainerComms::ReqOutputControlOffset:
        {
          CircuitId circuit_id = comms->mailBox[n].request.reqOutputContOffset.circuitId;
          ocpiDebug("Handling case DataTransfer::ContainerComms::ReqOutputControlOffset: 0x%x", circuit_id);

          int port_id = comms->mailBox[n].request.reqOutputContOffset.portId;

          addRemoteEndpoint( comms->mailBox[n].request.reqOutputContOffset.shadow_end_point );

          // Get the circuit 
          Circuit* c = getCircuit( circuit_id );
	  if (!c) {
	    ocpiBad("Unknown circuit %x", circuit_id);
	    ocpiAssert(0);
	  }
          OCPI::DataTransport::Port* port = 
            static_cast<OCPI::DataTransport::Port*>(c->getOutputPortSet()->getPortFromOrdinal( port_id ));
          ocpiAssert(port);

          // We will lookup the return addres based upon the mailbox
          SMBResources* res = getEndpointResources(comms->mailBox[n].request.reqOutputContOffset.shadow_end_point) ;
          if ( ! res ) {
            ocpiBad("**** INTERNAL programming error !! output shadow port asked for control offset and we dont know its end point !!\n");
            ocpiAssert(0);
          }

	  uint32_t protocolSize = 0;
	  uint64_t protocolOffset;
	  if (comms->mailBox[n].request.reqOutputContOffset.protocol_offset) {
	    // The server side is telling us where to put the protocol info, based on our telling
	    // it, in the reqnewconnection, how big it is.
	    c->getProtocolInfo(protocolSize, protocolOffset);
	    ocpiAssert(protocolSize != 0);
	  }

          OCPI::Util::VList offsetv;

          port->getOffsets( comms->mailBox[n].return_offset, offsetv);
	  
	  sendOffsets( offsetv, res->sMemServices->endpoint()->end_point,
		       protocolSize, protocolOffset,
		       comms->mailBox[n].request.reqOutputContOffset.protocol_offset);
          port->releaseOffsets( offsetv );
	  if (protocolSize)
	    m_CSendpoint->resources->sMemResourceMgr->free(protocolOffset, protocolSize);

          // Clear our mailbox
          comms->mailBox[n].error_code = 0;
          comms->mailBox[n].request.reqBasic.type = DataTransfer::ContainerComms::NoRequest;

          // We will copy our copy of their mailbox back to them
          int offset = sizeof(UpAndRunningMarker)+sizeof(ContainerComms::MailBox)*n;
          clearRemoteMailbox( offset, res->sMemServices->endpoint() );

        }
        break;



      case DataTransfer::ContainerComms::ReqShadowRstateOffset:
        {

          CircuitId circuit_id = comms->mailBox[n].request.reqShadowOffsets.circuitId;

          ocpiDebug("Handling case DataTransfer::ContainerComms::ReqShadowRstateOffset: 0x%x", circuit_id);
          int port_id = comms->mailBox[n].request.reqShadowOffsets.portId;

          SMBResources* res=    
            getEndpointResources( comms->mailBox[n].request.reqShadowOffsets.url );

          // Get the circuit 
          Circuit* c = getCircuit( circuit_id );

          ocpiDebug("Return address = %s", comms->mailBox[n].request.reqShadowOffsets.url );

          OCPI::DataTransport::Port* port=NULL;
          for ( OCPI::OS::uint32_t y=0; y<c->getInputPortSetCount(); y++ ) {
            PortSet* ps = static_cast<PortSet*>(c->getInputPortSet(y));
            port = static_cast<OCPI::DataTransport::Port*>(ps->getPortFromOrdinal( port_id ));
            if ( port ) {
              break;
            }
          }
          if ( !port ) {
                break;
          }

          OCPI::Util::VList offsetv;
          port->getOffsets( comms->mailBox[n].return_offset, offsetv);
          sendOffsets( offsetv, res->sMemServices->endpoint()->end_point );
          port->releaseOffsets( offsetv );

          // Clear our mailbox
          comms->mailBox[n].error_code = 0;
          comms->mailBox[n].request.reqBasic.type = DataTransfer::ContainerComms::NoRequest;

          // We will copy our copy of their mailbox back to them
          int offset = sizeof(UpAndRunningMarker)+sizeof(ContainerComms::MailBox)*n;
          clearRemoteMailbox( offset, res->sMemServices->endpoint() );

        }
        break;

      case DataTransfer::ContainerComms::ReqInputOffsets:
        {

          CircuitId circuit_id = comms->mailBox[n].request.reqInputOffsets.circuitId;
          ocpiDebug("Handling case DataTransfer::ContainerComms::ReqInputOffsets: 0x%x", circuit_id);
          int port_id = comms->mailBox[n].request.reqInputOffsets.portId;

          SMBResources* res=    
            getEndpointResources( comms->mailBox[n].request.reqInputOffsets.url );

          // Get the circuit 
          Circuit* c = getCircuit( circuit_id );


          OCPI::DataTransport::Port* port=NULL;
          for ( OCPI::OS::uint32_t y=0; y<c->getInputPortSetCount(); y++ ) {
            PortSet* ps = static_cast<PortSet*>(c->getInputPortSet(y));
            port = static_cast<OCPI::DataTransport::Port*>(ps->getPortFromOrdinal( port_id ));
            if ( port ) {
              break;
            }
          }
          if ( !port ) {
                break;
          }

          OCPI::Util::VList offsetv;
          port->getOffsets( comms->mailBox[n].return_offset, offsetv);
          sendOffsets( offsetv, res->sMemServices->endpoint()->end_point );
          port->releaseOffsets( offsetv );

          // Clear our mailbox
          comms->mailBox[n].error_code = 0;
          comms->mailBox[n].request.reqBasic.type = DataTransfer::ContainerComms::NoRequest;

          // We will copy our copy of their mailbox back to them
          int offset = sizeof(UpAndRunningMarker)+sizeof(ContainerComms::MailBox)*n;
          clearRemoteMailbox( offset, res->sMemServices->endpoint() );

        }
        break;

      case DataTransfer::ContainerComms::NoRequest:
      default:
        ocpiDebug("Handling case DataTransfer::ContainerComms::Default:");
        //                                ocpiAssert(0);

        break;

      }
    }
  }

  nc--;
}


void  Transport::removeLocalEndpoint(  const char* loc )
{
  for ( unsigned int n=0; n<m_localEndpoints.getElementCount(); n++ ) {
    GEndPoint* gep = (GEndPoint*)(m_localEndpoints.getEntry(n));
    if ( gep->ep == loc ) {
      XferFactoryManager::getFactoryManager().deleteSMBResources(gep->loc);
      m_localEndpoints.remove( gep );
      delete gep;
    }
  }
}

SMBResources* Transport::addRemoteEndpoint( const char* loc )
{
  std::string sloc(loc);
  ocpiDebug("In Transport::addRemoteEndpoint, loc = %s", loc );
  
  SMBResources* res = getEndpointResources(loc);
  if ( res ) {
    return res;
  }

  GEndPoint* gep = new GEndPoint;
  gep->ep = loc;
        
  std::string nuls;
  XferFactory* tfactory = 
    XferFactoryManager::getFactoryManager().find( nuls, sloc );
  if ( ! tfactory ) {
    delete gep;
    return NULL;
  }
  gep->loc = tfactory->getEndPoint(sloc);
  gep->loc->local = false;
  gep->res = XferFactoryManager::getFactoryManager().getSMBResources( gep->loc );
  m_remoteEndpoints.insert( gep );
  return gep->res;
}


SMBResources *
Transport::
addLocalEndpointFromProtocol( const char* protocol )
{
  return findLocalCompatibleEndpoint(protocol);
#if 0
  std::string loc(protocol);
  std::string nuls;
  XferFactory* tfactory = 
    XferFactoryManager::getFactoryManager().find( loc, nuls );
  if ( !tfactory )
    throw UnsupportedEndpointEx(protocol);
  std::string sep =
    tfactory->allocateEndpoint( NULL, tfactory->getNextMailBox(), tfactory->getMaxMailBox());
  return addLocalEndpoint( sep.c_str() );
bogus here - dont necessarily add a new one.
#endif
}

SMBResources* 
Transport::
addLocalEndpoint(const char *nfep, bool compatibleWith)
{
  // If none specified, use the default endpoint
  if (!nfep) {
    if (!m_defEndpoint)
      try {
	m_defEndpoint = addLocalEndpointFromProtocol("ocpi-smb-pio");
      } catch (...) {
	if (m_endpoints.empty())
	  throw;
	m_defEndpoint = addLocalEndpoint(m_endpoints[0].c_str());
      }
    return m_defEndpoint;
  }
  // finalize it
  std::string loc(nfep);
  XferFactory* tfactory = 
    XferFactoryManager::getFactoryManager().find(loc);
  if ( !tfactory ) {
    return NULL;
  }
  DataTransfer::EndPoint * lep =
    compatibleWith ?
    tfactory->newCompatibleEndPoint(nfep) :
    tfactory->getEndPoint( loc, true);
  // Force create
  tfactory->getSmemServices( lep );
  const char* ep = lep->end_point.c_str();
  loc = ep;

  SMBResources* res = getEndpointResources( ep );
  if ( res ) {
    return res;
  }

  ocpiDebug("******** REQ EP = %s as local", ep );

  GEndPoint* gep = new GEndPoint;
  gep->ep = ep;
  gep->loc = tfactory->getEndPoint(loc);
  if ( gep->loc->maxCount >= MAX_ENDPOINTS ) {
    delete gep;
    throw OCPI::Util::EmbeddedException( MAX_ENDPOINT_COUNT_EXCEEDED, loc.c_str() );
  }
  ocpiAssert(gep->loc->local == true);
  try {
    gep->res = XferFactoryManager::getFactoryManager().createSMBResources(gep->loc);
  }
  catch( ... ) {
    delete gep;
    throw;
  }
  loc = gep->ep = gep->loc->end_point;
  gep->loc->setEndpoint( loc );

  ocpiDebug("******** ADDING EP = %s as local", loc.c_str() );

  m_localEndpoints.insert( gep );
  return gep->res;
}



bool Transport::isLocalEndpoint( const char* loc )
{
  for ( OCPI::OS::uint32_t n=0; n<m_localEndpoints.getElementCount(); n++ ) {
#if 0
#ifndef NDEBUG
    printf("isLocalEndpoint:: Comparing (%s) with (%s) \n", loc, 
           ((GEndPoint*)m_localEndpoints.getEntry(n))->ep.c_str()  );
#endif
#endif
    if ( strcmp( loc, ((GEndPoint*)m_localEndpoints.getEntry(n))->ep.c_str() ) == 0 ) {
      ocpiDebug("isLocalEndpoint:: is local"  );

      return true;
    }
  }
  return false;
}



SMBResources* Transport::getEndpointResources(const char* ep)
{
  unsigned int n;
  for ( n=0; n<m_remoteEndpoints.getElementCount(); n++ ) {
    if ( static_cast<GEndPoint*>(m_remoteEndpoints[n])->res->sMemServices->endpoint()->end_point == ep ) {
      return static_cast<GEndPoint*>(m_remoteEndpoints[n])->res;
    }
  }
  for ( n=0; n<m_localEndpoints.getElementCount(); n++ ) {
    if ( static_cast<GEndPoint*>(m_localEndpoints[n])->res->sMemServices->endpoint()->end_point == ep ) {
      return static_cast<GEndPoint*>(m_localEndpoints[n])->res;
    }
  }
  return NULL;
}

// static - should probably move to XferFactoryManager...

// Answer whether a given existing endpoint can talk to the provided
// remote endpoint (and thus can be used).
bool Transport::
canSupport(DataTransfer::EndPoint &local_ep, const char *remote_endpoint) {
  char *protocol = strdup(remote_endpoint);
  char *cs = strdup(remote_endpoint);
  uint32_t mailBox, maxMb, size;
  DataTransfer::EndPoint::getProtocolFromString(remote_endpoint, protocol);
  DataTransfer::EndPoint::getResourceValuesFromString(remote_endpoint, cs, &mailBox, &maxMb, &size);
  return
    local_ep.protocol == protocol &&
    maxMb == local_ep.maxCount && mailBox != local_ep.mailbox;
}
