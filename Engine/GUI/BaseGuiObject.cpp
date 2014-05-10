#include "Include.h"
// ------------------------------------ //
#ifndef LEVIATHAN_GUI_BASEOBJECT
#include "BaseGuiObject.h"
#endif
#include "Iterators/StringIterator.h"
#include "ObjectFiles/ObjectFileProcessor.h"
#include "Script/ScriptModule.h"
#include <boost/assign/list_of.hpp>
#include "GUI/GuiManager.h"
#include "Common/StringOperations.h"
#include "Script/ScriptRunningSetup.h"
#include "Script/ScriptInterface.h"
using namespace Leviathan;
using namespace Gui;
// ------------------------------------ //
DLLEXPORT Leviathan::Gui::BaseGuiObject::BaseGuiObject(GuiManager* owner, const wstring &name, int fakeid, shared_ptr<ScriptScript> script 
	/*= NULL*/) : EventableScriptObject(script), OwningInstance(owner), FileID(fakeid), Name(name), 
	ID(IDFactory::GetID())
{
	
}

DLLEXPORT Leviathan::Gui::BaseGuiObject::~BaseGuiObject(){
	// script has smart pointer //

	// unregister all non-Rocket events //
	UnRegisterAllEvents();
}
// ------------------------------------ //
DLLEXPORT bool Leviathan::Gui::BaseGuiObject::LoadFromFileStructure(GuiManager* owner, vector<BaseGuiObject*> &tempobjects, 
	ObjectFileObject& dataforthis)
{
	// parse fake id from prefixes //
	int fakeid = 0;

	for(size_t i = 0; i < dataforthis.GetPrefixesCount(); i++){

		auto str = dataforthis.GetPrefix(i);

		if(StringOperations::StringStartsWith(str, wstring(L"ID"))){
			// get id number //
			StringIterator itr(str);

			auto tempnumber = itr.GetNextNumber<wstring>(DECIMALSEPARATORTYPE_NONE);

			fakeid = Convert::WstringToInt(*tempnumber);

			// Nothing more to find //
			break;
		}
	}

	unique_ptr<BaseGuiObject> tmpptr(new BaseGuiObject(owner, dataforthis.GetName(), fakeid, dataforthis.GetScript()));

	shared_ptr<NamedVariableList> listenon;

	// Get listeners //
	auto paramlist = dataforthis.GetListWithName(L"params");

	if(paramlist){

		listenon = paramlist->GetVariables().GetValueDirect(L"ListenOn");
	}

	if(tmpptr){
		
		// Listening start //
		if(listenon){
			tmpptr->StartMonitoring(listenon->GetValues());
		}

		tmpptr->_HookListeners();
		tempobjects.push_back(tmpptr.release());
		return true;
	}

	return false;
}
// ------------------------------------ //
void Leviathan::Gui::BaseGuiObject::_HookListeners(){
	// first we need to get probably right listeners and register with them //
	ScriptModule* mod = Scripting->GetModule();

	std::vector<shared_ptr<ValidListenerData>> containedlisteners;

	mod->GetListOfListeners(containedlisteners);


	for(size_t i = 0; i < containedlisteners.size(); i++){
		// generics cannot be rocket events //
		if(containedlisteners[i]->GenericTypeName && containedlisteners[i]->GenericTypeName->size() > 0){
			// custom event listener //

			RegisterForEvent(*containedlisteners[i]->GenericTypeName);

			continue;
		}

		// check for specific case //
		if(*containedlisteners[i]->ListenerName == L"Init"){
			// call now the init event //

		} else if(*containedlisteners[i]->ListenerName == L"Release"){
			// call later the release event //

		}

		// look for global events //
		EVENT_TYPE etype = ResolveStringToType(*containedlisteners[i]->ListenerName);
		if(etype != EVENT_TYPE_ERROR){

			RegisterForEvent(etype);
			continue;
		}

		Logger::Get()->Warning(L"BaseGuiObject: _HookListeners: unknown event type "+*containedlisteners[i]->ListenerName+L", did you intent to use Generic type?");
	}
}
// ------------------------------------ //
void Leviathan::Gui::BaseGuiObject::_CallScriptListener(Event** pEvent, GenericEvent** event2){

	ScriptModule* mod = Scripting->GetModule();

	if(pEvent){
		// Get the listener name from the event type //
		wstring listenername = GetListenerNameFromType((*pEvent)->GetType());

		// check does the script contain right listeners //
		if(mod->DoesListenersContainSpecificListener(listenername)){
			// setup parameters //
			vector<shared_ptr<NamedVariableBlock>> Args = boost::assign::list_of(new NamedVariableBlock(new VoidPtrBlock(this), L"BaseGuiObject"))
				(new NamedVariableBlock(new VoidPtrBlock(*pEvent), L"Event"));
			// we are returning ourselves so increase refcount
			AddRef();
			(*pEvent)->AddRef();

			ScriptRunningSetup sargs;
			sargs.SetEntrypoint(mod->GetListeningFunctionName(listenername)).SetArguments(Args);
			// run the script //
			shared_ptr<VariableBlock> result = ScriptInterface::Get()->ExecuteScript(Scripting.get(), &sargs);
			// do something with result //
		}
	} else {
		// generic event is passed //
		if(mod->DoesListenersContainSpecificListener(L"", (*event2)->GetTypePtr())){
			// setup parameters //
			vector<shared_ptr<NamedVariableBlock>> Args = boost::assign::list_of(new NamedVariableBlock(new VoidPtrBlock(this), L"BaseGuiObject"))
				(new NamedVariableBlock(new VoidPtrBlock(*event2), L"GenericEvent"));
			// we are returning ourselves so increase refcount
			AddRef();
			(*event2)->AddRef();

			ScriptRunningSetup sargs;
			sargs.SetEntrypoint(mod->GetListeningFunctionName(L"", (*event2)->GetTypePtr())).SetArguments(Args);
			// run the script //
			shared_ptr<VariableBlock> result = ScriptInterface::Get()->ExecuteScript(Scripting.get(), &sargs);
			// do something with result //
		}
	}
}


