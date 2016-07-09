// ------------------------------------ //
#include "Connection.h"

#include "NetworkResponse.h"
#include "NetworkRequest.h"
#include "Iterators/StringIterator.h"
#include "SFML/Network/IpAddress.hpp"
#include "SFML/Network/Packet.hpp"
#include "NetworkHandler.h"
#include "Exceptions.h"
#include "RemoteConsole.h"
#include "Threading/ThreadingManager.h"
#include "TimeIncludes.h"
#include "Utility/Convert.h"
#include "Engine.h"
using namespace Leviathan;
// ------------------------------------ //

//! Makes the program spam a ton of debug info about packets //
#define SPAM_ME_SOME_PACKETS 1

// ------------------------------------ //
DLLEXPORT Connection::Connection(const std::string &hostname) : 
    HostName(hostname), RawAddress(hostname)
{
	// We need to split the port number from the address //
	StringIterator itr(hostname);

	auto result = itr.GetUntilNextCharacterOrAll<std::string>(':');

	HostName = *result;

	// We should be fine not skipping a letter //
	result = itr.GetNextNumber<std::string>(DECIMALSEPARATORTYPE_NONE);

	if(!result){
		// Probably should get the default port number //
		LOG_WARNING("Connection: no port defined, using default, "
            "hostname: " + hostname);
		TargetPortNumber = 80;
		return;
	}
    
	TargetPortNumber = Convert::StringTo<int>(*result.get());
}

DLLEXPORT Connection::Connection(const sf::IpAddress &targetaddress,
    unsigned short port) :
    TargetPortNumber(port), TargetHost(targetaddress), AddressGot(true)
{
    RawAddress = GenerateFormatedAddressString();
}

DLLEXPORT Connection::~Connection(){

}
// ------------------------------------ //
DLLEXPORT bool Connection::Init(NetworkHandler* owninghandler){
    
    Owner = owninghandler;

    {
        // TODO: make this asynchronous
        GUARD_LOCK();

        // This might do something //
        if (!AddressGot) {
            TargetHost = sf::IpAddress(HostName);
        }
    }

	// We fail if we got an invalid address //
	if(TargetHost == sf::IpAddress::None){

		LOG_ERROR("Connection: Init: couldn't translate host name to a real address, "
            "host: " + HostName);
		return false;
	}

    Logger::Get()->Info("Connection: opening connection to " + 
        GenerateFormatedAddressString());

    // Reset various timers //
    LastSentPacketTime = LastReceivedPacketTime = Time::GetTimeMs64();
    
	return true;
}

DLLEXPORT void Connection::Release(){

    GUARD_LOCK();

    Logger::Get()->Info("Connection: disconnecting from " + GenerateFormatedAddressString());

    // Send a close packet //
    SendCloseConnectionPacket(guard);

    State = CONNECTION_STATE::Closed;

    // Destroy some of our stuff //
    TargetHost == sf::IpAddress::None;
        
    // Make sure that all our remaining packets fail //
    auto end = WaitingRequests.end();
    for(auto iter = WaitingRequests.begin(); iter != end; ++iter){

        // Mark as failed //
        (*iter)->SetWaitStatus(false);
    }

    // All are now properly closed //
    WaitingRequests.clear();
}
// ------------------------------------ //
DLLEXPORT std::shared_ptr<SentNetworkThing> Connection::SendPacketToConnection(Lock &guard,
    NetworkRequest &request, RECEIVE_GUARANTEE guarantee)
{
    if (!IsValidForSend())
        return nullptr;

	// Generate a packet from the request //
	sf::Packet actualpackettosend;
    
	// We need a complete header with acks and stuff //
	_PreparePacketHeaderForPacket(guard, ++LastUsedLocalID, actualpackettosend);

	// Generate packet object for the request //
    sf::Packet messagedata;

    request.AddDataToPacket(messagedata);

	// Add the data to the actual packet //
	actualpackettosend.append(messagedata.getData(), messagedata.getDataSize());

    _SendPacketToSocket(actualpackettosend);

	// Add to the sent packets //
    auto sentthing = std::make_shared<SentNetworkThing>(LastUsedLocalID, guarantee,
        std::move(messagedata), true);

	WaitingRequests.push_back(sentthing);

	return sentthing;
}

DLLEXPORT std::shared_ptr<SentNetworkThing> Connection::SendPacketToConnection(Lock &guard,
    NetworkResponse &response, RECEIVE_GUARANTEE guarantee)
{
    if(!IsValidForSend())
        return nullptr;

    // Generate a packet from the request //
    sf::Packet actualpackettosend;

    // We need a complete header with acks and stuff //
    _PreparePacketHeaderForPacket(guard, ++LastUsedLocalID, actualpackettosend);

    // Generate packet object for the request //
    sf::Packet messagedata;

    response.AddDataToPacket(messagedata);

    // Add the data to the actual packet //
    actualpackettosend.append(messagedata.getData(), messagedata.getDataSize());

    _SendPacketToSocket(actualpackettosend);

    // Add to the sent packets //
    auto sentthing = std::make_shared<SentNetworkThing>(LastUsedLocalID, guarantee,
        std::move(messagedata), false);

    WaitingRequests.push_back(sentthing);

    return sentthing;
}
// ------------------------------------ //
DLLEXPORT void Connection::SendKeepAlivePacket(Lock &guard){
    
    if (State == CONNECTION_STATE::Closed)
        return;

	// Generate a packet //
	sf::Packet actualpackettosend;
	_PreparePacketHeaderForPacket(guard, ++LastUsedLocalID, actualpackettosend, false);

    AddDataForResponseWithoutData(actualpackettosend, NETWORK_RESPONSE_TYPE::Keepalive);

    _SendPacketToSocket(actualpackettosend);

    // TODO: add to WaitingRequests if we want to time all the packets
}

DLLEXPORT void Connection::SendCloseConnectionPacket(Lock &guard){

    State = CONNECTION_STATE::Closed;

    // Generate a packet //
    sf::Packet actualpackettosend;
    _PreparePacketHeaderForPacket(guard, ++LastUsedLocalID, actualpackettosend, true);

    // Only type is required //
    AddDataForResponseWithoutData(actualpackettosend, NETWORK_RESPONSE_TYPE::CloseConnection);

    _SendPacketToSocket(actualpackettosend);
}
// ------------------------------------ //
void Leviathan::Connection::_Resend(Lock &guard, SentNetworkThing &toresend){

    if (!IsValidForSend())
        return;

    if (toresend.Resend != RECEIVE_GUARANTEE::None) {
        LOG_ERROR("Connection: Trying to Resend non-guaranteed packet");
        return;
    }

	// Generate a packet from the existing data //
	sf::Packet tosend;
	_PreparePacketHeaderForPacket(guard, toresend.PacketNumber, tosend);

	// Add the packet data //
	tosend.append(toresend.AlmostCompleteData.getData(), 
        toresend.AlmostCompleteData.getDataSize());

    _SendPacketToSocket(tosend);

    // Reset time so that timeout works again later //
    toresend.RequestStartTime = Time::GetTimeMs64();
}
// ------------------------------------ //
DLLEXPORT inline void Connection::HandleRemoteAck(Lock &guard, 
    uint32_t localidconfirmedassent)
{
    if(localidconfirmedassent > LastConfirmedSent)
        LastConfirmedSent = localidconfirmedassent;

    for(auto iter = WaitingRequests.begin(); iter != WaitingRequests.end(); ++iter){

        if((*iter)->IsRequest)
            continue;

        if(localidconfirmedassent == (*iter)->PacketNumber){

            (*iter)->OnFinalized(true);

            WaitingRequests.erase(iter);
            break;
        }
    }

    // Check which ack packets have been received //
    for (auto iter = SentAckPackets.begin(); iter != SentAckPackets.end(); ++iter) {

        if(localidconfirmedassent == (*iter)->InsidePacket){

            // Mark as properly sent //
            (*iter)->Received = true;

            //! Mark acks as received
            RemoveSucceededAcks(guard, *(*iter)->AcksInThePacket);

            SentAckPackets.erase(iter);
            break;
        }
	}
}
// ------------------------------------ //
DLLEXPORT void Connection::UpdateListening(){

    // Timeout stuff (if possible) //
    int64_t timems = Time::GetTimeMs64();

    // Check for connection close //
    if (timems > LastReceivedPacketTime + KEEPALIVE_TIME*1.5f) {
        // We could timeout the connection //
        Logger::Get()->Info("Connection: timing out connection to " +
            GenerateFormatedAddressString());

        // Mark us as closing //
        Owner->CloseConnection(*this);
        return;
    }

    GUARD_LOCK();

	for(auto iter = WaitingRequests.begin(); iter != WaitingRequests.end(); ){

        // Second timeout //
        if ((timems - (*iter)->RequestStartTime > PACKET_LOST_AFTER_MILLISECONDS) ||
            (LastConfirmedSent > (*iter)->PacketNumber + PACKET_LOST_AFTER_RECEIVED_NEWER))
        {
            // The current attempt is lost //
            if ((*iter)->Resend == RECEIVE_GUARANTEE::ResendOnce) {

                if (++(*iter)->AttempNumber <= 2) {

                    // Resend //
                    _Resend(guard, **iter);
                    ++iter;
                    continue;
                }

            } else if ((*iter)->Resend == RECEIVE_GUARANTEE::Critical) {

                // If a critical one fails the connection must be closed //
                if (++(*iter)->AttempNumber > CRITICAL_PACKET_MAX_TRIES) {

                    LOG_INFO("Connection: Lost critical packet too many times, "
                        "closing connection to: " + GenerateFormatedAddressString());

                    (*iter)->OnFinalized(false);
                    Owner->CloseConnection(*this);
                    return;
                }

                // Resend //
                _Resend(guard, **iter);
                ++iter;
                continue;
            }

            // Shouldn't be sent anymore //

            // Failed //
            (*iter)->OnFinalized(false);
            _FailPacketAcks((*iter)->PacketNumber);
            iter = WaitingRequests.erase(iter);

        } else {

            ++iter;
        }
	}

	bool AcksCouldBeSent = false;

	// Check if we have acks that haven't been sent //
    for(auto iter = ReceivedRemotePackets.begin(); iter != ReceivedRemotePackets.end(); 
        ++iter)
    {
        if(iter->second == RECEIVED_STATE::StateReceived){

            // Waiting to be sent //
            AcksCouldBeSent = true;
            break;
        }
    }

	// Send keep alive packet if it has been a while //
	if(timems > LastSentPacketTime+KEEPALIVE_TIME){
        
		// Send a keep alive packet //
		SendKeepAlivePacket(guard);

	} else if(AcksCouldBeSent && timems > LastSentPacketTime+ACKKEEPALIVE){

		// Send some acks //
        SendKeepAlivePacket(guard);
	}

}
// ------------------------------------ //
DLLEXPORT bool Connection::IsThisYours(sf::IpAddress &sender,
    unsigned short &sentport)
{
	// Check for matching sender with our target //
	if(sentport != TargetPortNumber || sender != TargetHost){
        
		// Not mine //
		return false;
	}
    
	// It is mine //
    return true;
}

DLLEXPORT void Connection::HandlePacket(sf::Packet &packet){
    
    // Handle incoming packet //

    // Header //
	uint32_t packetnumber = 0;

	if(!(packet >> packetnumber)){

		Logger::Get()->Error("Received package has invalid format");
	}

	// We can discard this here if this is already received //
	if(_IsAlreadyReceived(packetnumber)){

		// Ignore repeat packet //
		return;
	}

	NetworkAckField otherreceivedpackages(packet);

	if(!packet){

		Logger::Get()->Error("Received package has invalid format");
	}

    // Mark as received a packet //
    GUARD_LOCK();

    // Payload header //
	bool isrequest;

	if(!(packet >> isrequest)){

		Logger::Get()->Error("Received package has invalid format");
	}

	bool ShouldNotBeMarkedAsReceived = false;

	if(isrequest){

		// Generate a request object and make the interface handle it //
        auto request = NetworkRequest::LoadFromPacket(packet, packetnumber);

        if (!request) {

            LOG_ERROR("Connection: received an invalid request packet");
            goto Connectionafterprocesslabel;
        }

        if (RestrictType != CONNECTION_RESTRICTION::None) {

            // Restrict mode checking //

            // We can possibly drop the connection or perform other extra tasks //
            if(RestrictType == CONNECTION_RESTRICTION::ReceiveRemoteConsole){
                // Check type //
                if (!Engine::Get()->GetRemoteConsole()->CanOpenNewConnection(*this, request)) {

                    _OnRestrictFail(static_cast<uint16_t>(request->GetType()));
                    return;
                }

                // Successfully opened, connection should now be safe as a
                // general purpose connection
                RestrictType = CONNECTION_RESTRICTION::None;
                goto Connectionafterprocesslabel;
            }
        }

		try{
			Owner->GetInterface()->HandleRequestPacket(request, *this);
		} catch(const InvalidArgument &e){
			// We couldn't handle this packet //
			Logger::Get()->Error("Connection: couldn't handle request packet! :");
			e.PrintToLog();
		}

	} else {
		// Generate a response and pass to the interface //
        auto response = NetworkResponse::LoadFromPacket(packet);

        if (!response) {

            LOG_ERROR("Connection: received an invalid response packet");
            goto Connectionafterprocesslabel;
        }

		// The response might have a corresponding request //
		auto possiblerequest = _GetPossibleRequestForResponse(
            guard, response);

		if(RestrictType != CONNECTION_RESTRICTION::None){

            // Restrict mode checking //

            // Not allowed //
            _OnRestrictFail(static_cast<uint16_t>(response->GetType()));
            return;
		}

		// See if interface wants to drop it //
		if(!Owner->GetInterface()->PreHandleResponse(response, possiblerequest.get(), *this))
        {

			LOG_WARNING("Connection: dropping packet due to interface not accepting response");
			return;
		}

		if(possiblerequest){

			// Add the response to the request //
			possiblerequest->GotResponse = response;

            // Remove the request //
            for(auto iter = WaitingRequests.begin(); iter != WaitingRequests.end(); ++iter){

                if((*iter).get() == possiblerequest.get()){

                    // Notify that the request is done /
                    possiblerequest->OnFinalized(true);
                    WaitingRequests.erase(iter);
                    break;
                }
            }

		} else {

			// Just discard if it is an empty response //
			if(response->GetType() != NETWORK_RESPONSE_TYPE::None){

				// Handle the response only packet //
				Owner->GetInterface()->HandleResponseOnlyPacket(response, *this, 
                    ShouldNotBeMarkedAsReceived);
			}
		}
	}

Connectionafterprocesslabel:
    
    // Handle resends based on ack field //
    SetPacketsReceivedIfNotSet(guard,otherreceivedpackages);

    LastReceivedPacketTime = Time::GetTimeMs64();

    if (State == CONNECTION_STATE::NothingReceived) {

        State = CONNECTION_STATE::Initial;
    }

    // We possibly do not want to create an ack for this packet //

    if(!ShouldNotBeMarkedAsReceived){

        // Report the packet as received //
        ReceivedRemotePackets[packetnumber] = RECEIVED_STATE::StateReceived;
    }
}
// ------------------------------------ //
void Leviathan::Connection::_PreparePacketHeaderForPacket(Lock &guard,
    uint32_t localpacketid, sf::Packet &tofill, bool dontsendacks /*= false*/)
{
    LEVIATHAN_ASSERT(localpacketid > 0, "Trying to fill packet with packetid == 0");

	// First thing is the packet number //
	tofill << localpacketid;

	// We have now made a new packet //
	LastSentPacketTime = Time::GetTimeMs64();

	if(dontsendacks || ReceivedRemotePackets.empty()){

		tofill << uint32_t(0) << uint8_t(0);

	} else {

        // First we need to determine which received packet to use as first value //
        FrontAcks = !FrontAcks;

        uint32_t firstselected = 0;
        uint32_t last = 0;
        uint8_t count = 0;

        // Number of acks that could be sent
        int couldbesent = 0;
        
        if(FrontAcks){

            for(auto iter = ReceivedRemotePackets.begin(); iter != ReceivedRemotePackets.end();
                ++iter)
            {

                if(iter->second == RECEIVED_STATE::StateReceived){

                    firstselected = iter->first;
                    
                    ++couldbesent;
                }
            }

            if(firstselected == 0){

                goto copyacksfromtheendlabel;
            }

            const int maxcount = static_cast<int>(
                ReceivedRemotePackets.size() - firstselected);

            count = std::min(maxcount, DEFAULT_ACKCOUNT * 2);
            
        } else {

            for(auto iter = ReceivedRemotePackets.begin(); iter != ReceivedRemotePackets.end();
                ++iter)
            {

                if(iter->second == RECEIVED_STATE::StateReceived){

                    ++couldbesent;
                }
            }

copyacksfromtheendlabel:

            firstselected = ReceivedRemotePackets.rbegin()->first;

            if (couldbesent > DEFAULT_ACKCOUNT) {
                count = DEFAULT_ACKCOUNT * 2;
            } else {
                count = DEFAULT_ACKCOUNT;
            }

            last = firstselected - count >= 0 ? firstselected - count: 0;
            
            std::swap(firstselected, last);
        }

        // Create the ack field //
        auto tmpacks = std::make_shared<SentAcks>(localpacketid,
                std::make_shared<NetworkAckField>(firstselected, count, 
                    ReceivedRemotePackets));

        // Add to acks that actually matter if it has anything //
        if(tmpacks->AcksInThePacket->Acks.size() > 0)
            SentAckPackets.push_back(tmpacks);

        // Put into the packet //
        tmpacks->AcksInThePacket->AddDataToPacket(tofill);
	}
}

std::shared_ptr<SentNetworkThing> Connection::_GetPossibleRequestForResponse(
    Lock &guard, std::shared_ptr<NetworkResponse> response)
{
	// Return if it doesn't have a proper matching expected response id //
	const int lookingforid = response->GetResponseID();

	if(lookingforid == 0)
		return nullptr;

	for(auto iter = WaitingRequests.begin(); iter != WaitingRequests.end(); ++iter){

		if((*iter)->PacketNumber == lookingforid){
            
			// Found matching object //
			return *iter;
		}
	}

	// Nothing found //
	return nullptr;
}

DLLEXPORT void Connection::SetRestrictionMode(CONNECTION_RESTRICTION type){
	RestrictType = type;
}

DLLEXPORT bool Connection::IsTargetHostLocalhost(){
	// Check does the address match localhost //
	return TargetHost == sf::IpAddress::LocalHost;
}

DLLEXPORT std::string Connection::GenerateFormatedAddressString() const{
	return TargetHost.toString() + ":" + Convert::ToString(TargetPortNumber);
}
// ------------------------------------ //
DLLEXPORT void Connection::CalculateNetworkPing(int packets, int allowedfails,
    std::function<void(int, int)> onsucceeded, std::function<void(PING_FAIL_REASON,
        int)> onfailed)
{
    // Avoid dividing by zero here //
    if(packets == 0){

        ++packets;
        LOG_WARNING("Connection: avoided dividing by zero by increasing ping packet count by "
            "one");
    }
    
    // The finishing check task needs to store this. Using a smart pointer avoids copying this around
    std::shared_ptr<std::vector<std::shared_ptr<SentNetworkThing>>> sentechos = std::make_shared<
        std::vector<std::shared_ptr<SentNetworkThing>>>();
    
    sentechos->reserve(packets);

    if(packets >= 100){

        Logger::Get()->Warning("Connection: trying to send loads of ping packets, sending "+
            Convert::ToString(packets)+" packets");
    }

    // Send the packet count of echo requests //
    for(int i = 0; i < packets; i++){
        
        // Create a suitable echo request //
        // This needs to be regenerated for each loop as each need to have unique id for responses
        // to be registered properly
        RequestEcho echorequest(NETWORK_REQUEST_TYPE::Echo);
        
        auto cursent = SendPacketToConnection(echorequest, RECEIVE_GUARANTEE::Critical);
        cursent->SetAsTimed();

        sentechos->push_back(cursent);
    }

    ThreadingManager::Get()->QueueTask(new ConditionalTask(std::bind<void>([](
                    std::shared_ptr<std::vector<std::shared_ptr<SentNetworkThing>>> requests,
                    std::function<void(int, int)> onsucceeded,
                    std::function<void(PING_FAIL_REASON, int)> onfailed,
                    int allowedfails) -> void
        {
            int fails = 0;

            std::vector<int64_t> packagetimes;
            packagetimes.reserve(requests->size());
            
            // Collect the times //
            auto end = requests->end();
            for(auto iter = requests->begin(); iter != end; ++iter){

                if(!(*iter)->Succeeded ||
                    (*iter)->ConfirmReceiveTime.load(std::memory_order_acquire) < 2)
                {
                    // This one has failed //
                    
                    fails++;
                    continue;
                }

                // Store the time //
                packagetimes.push_back((*iter)->ConfirmReceiveTime.load(
                    std::memory_order_acquire) -
                    (*iter)->RequestStartTime);
            }
            
            
            // Check has too many failed //
            if(fails > allowedfails){

                Logger::Get()->Warning("Connection: pinging failed due to too many lost "
                    "packets, lost: "+Convert::ToString(fails));
                
                onfailed(PING_FAIL_REASON::LossTooHigh, fails);
                return;
            }

            // Use some nice distribution math to get the ping //
            int finalping = 0;

            // The values shouldn't be able to be more than 1000 each so ints will
            // be able to hold all the values
            int64_t sum = 0;
            float averagesquared = 0.f;
            
            auto end2 = packagetimes.end();
            for(auto iter = packagetimes.begin(); iter != end2; ++iter){

                sum += *iter;
                averagesquared += (powf(static_cast<float>(*iter), 2));
            }
            
            float average = sum/static_cast<float>(packagetimes.size());

            averagesquared /= static_cast<float>(packagetimes.size());

            float standarddeviation = sqrtf(averagesquared-powf(average, 2));

            // End mathematics

            // Just one more calculation to get ping that represents average bad case in some way,
            // might require tweaking...
            
            finalping = static_cast<int>(average+(standarddeviation*0.7f));
            
            onsucceeded(finalping, fails);
            
            Logger::Get()->Info("Connection: pinging completed, ping: "+
                Convert::ToString(finalping));
            
        }, sentechos, onsucceeded, onfailed, allowedfails), std::bind<bool>([](
                std::shared_ptr<std::vector<std::shared_ptr<SentNetworkThing>>> requests) 
            -> bool
            {
                // Check if even one is still waiting //
                auto end = requests->end();
                for(auto iter = requests->begin(); iter != end; ++iter){
                    if(!(*iter)->IsFinalized())
                        return false;
                }

                // None are still waiting, good to go //
                return true;

            }, sentechos)));
    
}
// ------------------------------------ //
DLLEXPORT bool Leviathan::Connection::BlockUntilFinished(
    std::shared_ptr<SentNetworkThing> packet) 
{
    if (!packet)
        throw InvalidArgument("packet is null");

    // Now we wait //
    packet->GetStatus();

    return packet->Succeeded;
}

DLLEXPORT void Leviathan::Connection::AddDataForResponseWithoutData(sf::Packet &packet,
    NETWORK_RESPONSE_TYPE type) 
{
    packet << false << static_cast<uint32_t>(0) << static_cast<uint16_t>(type);
}
// ------------------------------------ //
DLLEXPORT void Leviathan::Connection::SetPacketsReceivedIfNotSet(Lock &guard, 
    NetworkAckField &acks) 
{
    // We need to loop through all our acks and set them in the map //
    for (uint32_t i = 0; i < static_cast<uint32_t>(acks.Acks.size()); i++) {

        // Loop the individual bits //
        for (int bit = 0; bit < 8; bit++) {

            // Check is it set //
            if (acks.Acks[i] & (1 << bit)) {

                // Set as received //
                HandleRemoteAck(guard, acks.FirstPacketID + i * 8 + bit);
            }
        }
    }
}


DLLEXPORT void Leviathan::Connection::RemoveSucceededAcks(Lock &guard, NetworkAckField &acks) {

    // We need to loop through all our acks and erase them from the map (if set) //
    for (uint32_t i = 0; i < static_cast<uint32_t>(acks.Acks.size()); i++) {
        // Loop the individual bits //
        for (int bit = 0; bit < 8; bit++) {

            // Check is it set //
            if (acks.Acks[i] & (1 << bit)) {

                // Remove it //
                ReceivedRemotePackets.erase(acks.FirstPacketID + i * 8 + bit);
            }
        }
    }
}

// ------------------------------------ //
DLLEXPORT void Leviathan::Connection::_SendPacketToSocket(sf::Packet actualpackettosend) {

    LEVIATHAN_ASSERT(Owner, "Connection no owner");

    auto guard(Owner->LockSocketForUse());
    Owner->_Socket.send(actualpackettosend, TargetHost, TargetPortNumber);
}
// ------------------------------------ //
bool Connection::_IsAlreadyReceived(uint32_t packetid){
    GUARD_LOCK();

	// It is moved through in reverse to quickly return matches,
    // but receiving the same packet twice isn't that common
    for(auto id : LastReceivedPacketIDs){

        if(id == packetid){

            // Found a match, this is an already received packet //
			return true;
        }
    }
    
	// Not found, add for future searches //
	LastReceivedPacketIDs.push_back(packetid);

	// It wasn't there //
	return false;
}



DLLEXPORT void Leviathan::Connection::_FailPacketAcks(uint32_t packetid) {

    for (auto iter = SentAckPackets.begin(); iter != SentAckPackets.end(); ++iter) {

        if ((*iter)->InsidePacket = packetid) {

            SentAckPackets.erase(iter);
            return;
        }
    }
}

DLLEXPORT void Leviathan::Connection::_OnRestrictFail(uint16_t type) {

    LOG_ERROR("Connection: received a non-valid packet "
        "in restrict mode(" + Convert::ToString(static_cast<int>(RestrictType)) + ") type: " +
        Convert::ToString(static_cast<int>(type)) + " from: " + 
        GenerateFormatedAddressString());

    Owner->CloseConnection(*this);
}

// ------------------ SentNetworkThing ------------------ //
DLLEXPORT Leviathan::SentNetworkThing::SentNetworkThing(uint32_t packetid, 
    RECEIVE_GUARANTEE guarantee, sf::Packet&& packetsdata, bool isrequest)  :
    PacketNumber(packetid), Resend(guarantee), 
    RequestStartTime(Time::GetTimeMs64()),
    AlmostCompleteData(std::move(packetsdata)),
    IsRequest(isrequest)
{

}

DLLEXPORT void SentNetworkThing::SetWaitStatus(bool status){
    
    Succeeded = status;
    IsDone.store(true, std::memory_order_release);

    if (Callback)
        (*Callback)(status, *this);
}

DLLEXPORT void Leviathan::SentNetworkThing::OnFinalized(bool succeeded) {

    if (!succeeded) {

        SetWaitStatus(false);
        return;
    }

    if (ConfirmReceiveTime.load(std::memory_order_consume) == 1) {

        ConfirmReceiveTime.store(Time::GetTimeMs64(),
            std::memory_order_release);
    }

    // We want to notify all waiters that it has been received //
    SetWaitStatus(true);
}

DLLEXPORT bool SentNetworkThing::GetStatus() {

    while(!IsDone.load(std::memory_order_acquire)){

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return Succeeded;
}

DLLEXPORT void SentNetworkThing::SetAsTimed(){
    
    ConfirmReceiveTime.store(1, std::memory_order_release);
}

DLLEXPORT void SentNetworkThing::SetCallback(std::shared_ptr<CallbackType> func)
{
    Callback = func;
}

DLLEXPORT void Leviathan::SentNetworkThing::SetCallbackFunc(CallbackType func) {

    Callback = std::make_shared<CallbackType>(std::move(func));
}
// ------------------ NetworkAckField ------------------ //
DLLEXPORT Leviathan::NetworkAckField::NetworkAckField(uint32_t firstpacketid, 
    uint8_t maxacks, ReceivedPacketField &copyfrom) :
    FirstPacketID(firstpacketid)
{

    // Id is 0 nothing should be copied //
    if (FirstPacketID == 0)
        return;

    auto iter = copyfrom.begin();

    for (; iter != copyfrom.end(); ++iter) {
        if (iter->first >= firstpacketid) {
            break;
        }
    }

    if (iter == copyfrom.end()) {
        // No start ack found //
        FirstPacketID = 0;
        return;
    }

    // Reserve space for data //
    Acks.reserve(maxacks / 8);

    // Start filling it //
    for (char i = 0; i < maxacks; i++) {

        uint32_t curcheckacknumber = firstpacketid + i;

        size_t vecelement = i / 8;

        while (Acks.size() <= vecelement) {

            Acks.push_back(0);
        }

        // If reached the end just leave the zeros //
        if (iter == copyfrom.end()) {
            break;
        }

        if (iter->first == curcheckacknumber) {

setcurrentnumbertoackfield:

            // Mark it as copied to a ack field //
            // This isn't actually needed as we want to
            //iter->second = RECEIVED_STATE::AcksSent;

            // Set it //
            Acks[vecelement] |= (1 << (i - vecelement));

            ++iter;

        } else if (iter->first > curcheckacknumber) {
            // We are missing these packets so let them be zeroed out //

        } else {
            // Advance iterator to reach this //
            for (; iter != copyfrom.end(); ++iter) {

                if (iter->first == curcheckacknumber) {

                    goto setcurrentnumbertoackfield;

                } else if (iter->first > curcheckacknumber) {

                    break;
                }
            }
        }
    }

    // We need to reset FirstPacketID if no acks set //
    if (Acks.size() == 0)
        FirstPacketID = 0;
}

DLLEXPORT NetworkAckField::NetworkAckField(sf::Packet &packet){

    // Get data //
	packet >> FirstPacketID;
    
	uint8_t tmpsize = 0;
    
	packet >> tmpsize;

    if(!packet)
        return;
    
	// Fill in the acks from the packet //
	Acks.resize(tmpsize);
    
	for(char i = 0; i < tmpsize; i++){
		packet >> Acks[i];
	}
}

DLLEXPORT void NetworkAckField::AddDataToPacket(sf::Packet &packet){

	// First set the trivial data //
	uint8_t tmpsize = static_cast<uint8_t>(Acks.size());

	packet << FirstPacketID << tmpsize;
    
	// Now to fill in the ack data //
	for(char i = 0; i < tmpsize; i++){
        
		packet << Acks[i];
	}
}


