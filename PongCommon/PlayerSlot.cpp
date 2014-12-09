#include "PongIncludes.h"
// ------------------------------------ //
#ifndef PONG_PLAYERSLOT
#include "PlayerSlot.h"
#endif
#include "Entities/Bases/BasePhysicsObject.h"
#include "Iterators/StringIterator.h"
#include "Networking/NetworkServerInterface.h"
#include "Utility/ComplainOnce.h"
#ifdef PONG_VERSION
#include "PongGame.h"
#include "PongNetHandler.h"
#endif //PONG_VERSION
#include "GameInputController.h"
using namespace Pong;
// ------------------------------------ //
Pong::PlayerSlot::PlayerSlot(int slotnumber, PlayerList* owner) :
    Slot(slotnumber), Parent(owner), Score(0), PlayerType(PLAYERTYPE_CLOSED), 
	PlayerNumber(0), ControlType(PLAYERCONTROLS_NONE), ControlIdentifier(0), Colour(Float4::GetColourWhite()),
    PlayerControllerID(0), SplitSlot(NULL), SlotsPlayer(NULL), TrackDirectptr(NULL), PlayerID(-1),
    NetworkedInputID(-1), InputObj(NULL)
{
	
}

Pong::PlayerSlot::~PlayerSlot(){
	_ResetNetworkInput();
	SAFE_DELETE(SplitSlot);
}
// ------------------------------------ //
void Pong::PlayerSlot::Init(PLAYERTYPE type /*= PLAYERTYPE_EMPTY*/, int PlayerNumber /*= 0*/,
    PLAYERCONTROLS controltype /*= PLAYERCONTROLS_NONE*/, int ctrlidentifier /*= 0*/, int playercontrollerid /*= -1*/,
    const Float4 &playercolour /*= Float4::GetColourWhite()*/)
{
	PlayerType = type;
	PlayerNumber = PlayerNumber;
	ControlType = controltype;
	ControlIdentifier = ctrlidentifier;
	Colour = playercolour;
	PlayerControllerID = playercontrollerid;
}
// ------------------------------------ //
void Pong::PlayerSlot::SetPlayer(PLAYERTYPE type, int identifier){
	PlayerType = type;
	PlayerNumber = identifier;
}

Pong::PLAYERTYPE Pong::PlayerSlot::GetPlayerType(){
	return PlayerType;
}

int Pong::PlayerSlot::GetPlayerNumber(){
	return PlayerNumber;
}
// ------------------------------------ //
void Pong::PlayerSlot::SetControls(PLAYERCONTROLS type, int identifier){
	ControlType = type;
	ControlIdentifier = identifier;
}

Pong::PLAYERCONTROLS Pong::PlayerSlot::GetControlType(){
	return ControlType;
}

int Pong::PlayerSlot::GetControlIdentifier(){
	return ControlIdentifier;
}
// ------------------------------------ //
int Pong::PlayerSlot::GetSlotNumber(){
	return Slot;
}
// ------------------------------------ //
int Pong::PlayerSlot::GetSplitCount(){
	return SplitSlot != NULL ? 1+SplitSlot->GetSplitCount(): 0;
}

void Pong::PlayerSlot::PassInputAction(CONTROLKEYACTION actiontoperform, bool active){
	GUARD_LOCK_THIS_OBJECT();

    Logger::Get()->Write("Input: "+Convert::ToString(actiontoperform)+", down: "+
        Convert::ToString<int>(active));
    
	// if this is player 0 or 3 flip inputs //
	if(Slot == 0 || Slot == 2){

		switch(actiontoperform){
		case CONTROLKEYACTION_LEFT: actiontoperform = CONTROLKEYACTION_POWERUPUP; break;
		case CONTROLKEYACTION_POWERUPDOWN: actiontoperform = CONTROLKEYACTION_RIGHT; break;
		case CONTROLKEYACTION_RIGHT: actiontoperform = CONTROLKEYACTION_POWERUPDOWN; break;
		case  CONTROLKEYACTION_POWERUPUP: actiontoperform = CONTROLKEYACTION_LEFT; break;
		}
	}

	// set control state //
	if(active){

		if(actiontoperform == CONTROLKEYACTION_LEFT){
			MoveState = 1;
		} else if(actiontoperform == CONTROLKEYACTION_RIGHT){
			MoveState = -1;
		}

	} else {

		if(actiontoperform == CONTROLKEYACTION_LEFT && MoveState == 1){
			MoveState = 0;
		} else if(actiontoperform == CONTROLKEYACTION_RIGHT && MoveState == -1){
			MoveState = 0;
		}

	}

    if(!TrackDirectptr){

        Leviathan::ComplainOnce::PrintWarningOnce(L"Slot_trackdirect_ptr_is_empty",
            L"Slot is trying to move but doesn't have track pointer set");
    }

	// Set the track speed based on move direction //
	if(TrackDirectptr && MoveState)
		TrackDirectptr->SetTrackAdvanceSpeed(MoveState*INPUT_TRACK_ADVANCESPEED);
}

void Pong::PlayerSlot::InputDisabled(){
	// set apply force to zero //
	if(PaddleObject){

		TrackDirectptr->SetTrackAdvanceSpeed(0.f);
	}

	// reset control state //
	MoveState = 0;
}

int Pong::PlayerSlot::GetScore(){
	return Score;
}
// ------------------------------------ //
void Pong::PlayerSlot::AddEmptySubSlot(){
	SplitSlot = new PlayerSlot(this->Slot, Parent);
	// Set this as the parent //
	SplitSlot->ParentSlot = this;


}

void Pong::PlayerSlot::SetPlayerProxy(PLAYERTYPE type){
	SetPlayer(type, ++CurrentPlayerNumber);
}

bool Pong::PlayerSlot::IsVerticalSlot(){
	return Slot == 0 || Slot == 2;
}

float Pong::PlayerSlot::GetTrackProgress(){
	return TrackDirectptr->GetProgressTowardsNextNode();
}

bool Pong::PlayerSlot::DoesPlayerNumberMatchThisOrParent(int number){
	if(number == PlayerNumber)
		return true;
	// Recurse to parent, if one exists //
	if(ParentSlot)
		return ParentSlot->DoesPlayerNumberMatchThisOrParent(number);

	return false;
}

int Pong::PlayerSlot::CurrentPlayerNumber = 0;
// ------------------------------------ //
void Pong::PlayerSlot::AddDataToPacket(sf::Packet &packet){
	GUARD_LOCK_THIS_OBJECT();

	// Write all our data to the packet //
	packet << Slot << (int)PlayerType << PlayerNumber << NetworkedInputID << ControlIdentifier << (int)ControlType << PlayerControllerID 
		<< Colour.X << Colour.Y << Colour.Z << Colour.W;
	packet << PlayerID << Score << (bool)(SplitSlot != NULL);

	if(SplitSlot){
		// Add our sub slot data //
		SplitSlot->AddDataToPacket(packet);
	}
}

void Pong::PlayerSlot::UpdateDataFromPacket(sf::Packet &packet){
	GUARD_LOCK_THIS_OBJECT();

	int tmpival;

	// Get our data from it //
	if(!(packet >> Slot)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(!(packet >> tmpival)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	PlayerType = static_cast<PLAYERTYPE>(tmpival);

	if(!(packet >> PlayerNumber)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(!(packet >> NetworkedInputID)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}
	
	if(!(packet >> ControlIdentifier)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(!(packet >> tmpival)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	ControlType = static_cast<PLAYERCONTROLS>(tmpival);

	if(!(packet >> PlayerControllerID)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(!(packet >> Colour.X >> Colour.Y >> Colour.Z >> Colour.W)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(!(packet >> PlayerID)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(!(packet >> Score)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	bool wantedsplit;

	if(!(packet >> wantedsplit)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	// Check do we need to change split //
	if(wantedsplit && !SplitSlot){
		AddEmptySubSlot();

	} else if(!wantedsplit && SplitSlot){
		SAFE_DELETE(SplitSlot);
	}

	// Update split value //
	if(wantedsplit){

		SplitSlot->UpdateDataFromPacket(packet);
	}

#ifdef PONG_VERSION

	// Update everything related to input //
	if(PlayerID == PongGame::Get()->GetInterface()->GetOurID()){

		// Create new one only if there isn't one already created //
		if(!InputObj){

            Logger::Get()->Info("Creating input for our player id");
            
			// Hook a networked input receiver to the server //
			PongGame::Get()->GetInputController()->RegisterNewLocalGlobalReflectingInputSource(
				PongGame::GetInputFactory()->CreateNewInstanceForLocalStart(NetworkedInputID, true));
		} else {

			// Update the existing one //
			InputObj->UpdateSettings(ControlType);
		}
	}


#endif //PONG_VERSION


}

int Pong::PlayerSlot::GetPlayerControllerID(){
	return PlayerControllerID;
}

void Pong::PlayerSlot::SlotJoinPlayer(Leviathan::ConnectedPlayer* ply){
	SlotsPlayer = ply;

	PlayerID = SlotsPlayer->GetID();

	PlayerType = PLAYERTYPE_HUMAN;
}

void Pong::PlayerSlot::SlotLeavePlayer(){
	SlotsPlayer = NULL;
	ControlType = PLAYERCONTROLS_NONE;
	PlayerType = PLAYERTYPE_EMPTY;
}

void Pong::PlayerSlot::SetInputThatSendsControls(PongNInputter* input, PongNInputter* oldcheck /*= NULL*/){
	GUARD_LOCK_THIS_OBJECT();
	if(oldcheck && InputObj != oldcheck)
		return;

	_ResetNetworkInput();

	InputObj = input;
}

void Pong::PlayerSlot::_ResetNetworkInput(){
	GUARD_LOCK_THIS_OBJECT();
	if(InputObj){


		InputObj->StopSendingInput(this);
        // The game InputController might be getting deleted during this, so potentially avoid this call //
        auto icontroller = GameInputController::Get();
        
        if(icontroller){
            // Hopefully it is impossible for it to get deleted during this call //
            icontroller->QueueDeleteInput(InputObj);
        }
            
	}

	InputObj = NULL;
}

// ------------------ PlayerList ------------------ //
Pong::PlayerList::PlayerList(boost::function<void (PlayerList*)> callback, size_t playercount /*= 4*/) : SyncedResource(L"PlayerList"), 
	CallbackFunc(callback), GamePlayers(4)
{

	// Fill default player data //
	for(int i = 0; i < 4; i++){

		GamePlayers[i] = new PlayerSlot(i, this);
	}

}

Pong::PlayerList::~PlayerList(){
	SAFE_DELETE_VECTOR(GamePlayers);
}
// ------------------------------------ //
void Pong::PlayerList::UpdateCustomDataFromPacket(sf::Packet &packet) THROWS{
	
	sf::Int32 vecsize;

	if(!(packet >> vecsize)){

		throw Leviathan::ExceptionInvalidArgument(L"packet format for PlayerSlot is invalid", 0, __WFUNCTION__, L"packet", L"");
	}

	if(vecsize != GamePlayers.size()){
		// We need to resize //
		int difference = vecsize-GamePlayers.size();

		if(difference < 0){

			// Loop and delete items //
			int deleted = 0;
			while(deleted < difference){

				SAFE_DELETE(GamePlayers[GamePlayers.size()-1]);
				GamePlayers.pop_back();
				deleted++;
			}
		} else {
			// We need to add items //
			for(int i = 0; i < difference; i++){

				GamePlayers.push_back(new PlayerSlot(GamePlayers.size(), this));
			}
		}
	}

	// Update the data //
	auto end = GamePlayers.end();
	for(auto iter = GamePlayers.begin(); iter != end; ++iter){

		(*iter)->UpdateDataFromPacket(packet);
	}


	// Notify update //
	OnValueUpdated();
}

void Pong::PlayerList::SerializeCustomDataToPacket(sf::Packet &packet){
	GUARD_LOCK_THIS_OBJECT();
	// First put the size //
	packet << static_cast<sf::Int32>(GamePlayers.size());

	// Loop through them and add them //
	for(auto iter = GamePlayers.begin(); iter != GamePlayers.end(); ++iter){

		// Serialize it //
		(*iter)->AddDataToPacket(packet);
	}
}

void Pong::PlayerList::OnValueUpdated(){
	// Call our callback //
	CallbackFunc(this);
}

PlayerSlot* Pong::PlayerList::GetSlot(size_t index) THROWS{
	if(index >= GamePlayers.size())
		throw Leviathan::ExceptionInvalidArgument(L"index is out of range", GamePlayers.size(), __WFUNCTION__, L"index", Convert::ToWstring(index));

	return GamePlayers[index];
}


