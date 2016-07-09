#pragma once

#include "angelscript.h"
#include "Events/Event.h"
#include "ScriptExecutor.h"
#include "Events/EventHandler.h"
#include "Utility/DataHandling/SimpleDatabase.h"
#include "Addons/GameModule.h"
#include "../Entities/Components.h"
#include "../Entities/GameWorld.h"
#include "add_on/autowrapper/aswrappedcall.h"
#include "Networking/NetworkCache.h"

#include "Interface/ScriptEventListener.h"
#include "Interface/ScriptLock.h"

#include "Engine.h"

using namespace Leviathan::Script;
using namespace std;

GenericEvent* WrapperGenericEventFactory(const string &name){

	return new GenericEvent(name, NamedVars());
}

Event* WrapperEventFactory(EVENT_TYPE type){

    try{
        return new Event(type, NULL);
        
    } catch(const Exception &e){

        Logger::Get()->Error("Failed to construct Event for script, exception: ");
        e.PrintToLog();

        return NULL;
    }
}

ScriptSafeVariableBlock* ScriptSafeVariableBlockFactoryString(const string &blockname,
    const string &valuestr)
{

	return new ScriptSafeVariableBlock(new StringBlock(valuestr), blockname);
}

template<typename TType>
ScriptSafeVariableBlock* ScriptSafeVariableBlockFactoryGeneric(const string &blockname,
    TType value)
{

	return new ScriptSafeVariableBlock(new DataBlock<TType>(value), blockname);
}
// ------------------ Float3 proxies ------------------ //
void Float3ConstructorProxy(void* memory){
	new(memory) Float3();
}

void Float3ConstructorProxyAll(void* memory, float x, float y, float z){
	new(memory) Float3(x, y, z);
}

void Float3ConstructorProxySingle(void* memory, float all){
	new(memory) Float3(all);
}

void Float3ConstructorProxyCopy(void* memory, const Float3 &other){
	new(memory) Float3(other);
}

void Float3DestructorProxy(void* memory){
	reinterpret_cast<Float3*>(memory)->~Float3();
}
// ------------------ Float4 proxies ------------------ //
void Float4ConstructorProxy(void* memory){
	new(memory) Float4();
}

void Float4ConstructorProxyAll(void* memory, float x, float y, float z, float w){
	new(memory) Float4(x, y, z, w);
}

void Float4ConstructorProxySingle(void* memory, float all){
	new(memory) Float4(all);
}

void Float4ConstructorProxyCopy(void* memory, const Float4 &other){
	new(memory) Float4(other);
}

void Float4DestructorProxy(void* memory){
	reinterpret_cast<Float4*>(memory)->~Float4();
}

// ------------------ Dynamic cast proxies ------------------ //
template<class From, class To>
To* DoReferenceCastDynamic(From* ptr){
	// If already invalid just return it //
	if(!ptr)
		return NULL;

	To* newptr = dynamic_cast<To*>(ptr);
	if(newptr){
		// Add reference so script doesn't screw up with the handles //
		newptr->AddRef();
	}

	// Return the ptr (which might be invalid) //
	return newptr;
}

template<class From, class To>
To* DoReferenceCastStatic(From* ptr){
	// If already invalid just return it //
	if(!ptr)
		return NULL;

	To* newptr = static_cast<To*>(ptr);
	if(newptr){
		// Add reference so script doesn't screw up with the handles //
		newptr->AddRef();
	}

	// Return the ptr (which might be invalid) //
	return newptr;
}


static std::string GetLeviathanVersionProxy(){

    return Leviathan::VERSIONS;
}


//! \todo Create a wrapper around NewtonBody which has reference counting
bool BindEngineCommonScriptIterface(asIScriptEngine* engine){

	// Register common float types //


	// Float3 bind //
	if(engine->RegisterObjectType("Float3", sizeof(Float3), asOBJ_VALUE |
            asGetTypeTraits<Float3>() | asOBJ_APP_CLASS_ALLFLOATS) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Float3", asBEHAVE_CONSTRUCT, "void f()",
            asFUNCTION(Float3ConstructorProxy),
            asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Float3", asBEHAVE_CONSTRUCT, "void f(float value)",
            asFUNCTION(Float3ConstructorProxySingle), asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Float3", asBEHAVE_CONSTRUCT,
            "void f(float x, float y, float z)",
            asFUNCTION(Float3ConstructorProxyAll), asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Float3", asBEHAVE_CONSTRUCT,
            "void f(const Float3 &in other)",
            asFUNCTION(Float3ConstructorProxyCopy), asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Float3", asBEHAVE_DESTRUCT, "void f()",
            asFUNCTION(Float3DestructorProxy),
            asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	// Operators //
	if(engine->RegisterObjectMethod("Float3", "Float3& opAssign(const Float3 &in other)",
            asMETHODPR(Float3, operator=, (const Float3&), Float3&), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectMethod("Float3", "Float3 opAdd(const Float3 &in other) const",
            asMETHODPR(Float3, operator+, (const Float3&) const, Float3), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectMethod("Float3", "Float3 opSub(const Float3 &in other) const",
            asMETHODPR(Float3, operator-, (const Float3&) const, Float3), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectMethod("Float3", "Float3 opMul(float multiply) const",
            asMETHODPR(Float3, operator*, (float) const, Float3), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectMethod("Float3", "Float3 Normalize() const",
            asMETHOD(Float3, Normalize),
            asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectMethod("Float3", "float HAddAbs()", asMETHOD(Float3, HAddAbs),
            asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

    // Direct access
    if(engine->RegisterObjectProperty("Float3", "float X", asOFFSET(Float3, X)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Float3", "float Y", asOFFSET(Float3, Y)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Float3", "float Z", asOFFSET(Float3, Z)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

    // Float4
    if(engine->RegisterObjectType("Float4", sizeof(Float4), asOBJ_VALUE |
            asGetTypeTraits<Float4>() | asOBJ_APP_CLASS_ALLFLOATS) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("Float4", asBEHAVE_CONSTRUCT, "void f()",
            asFUNCTION(Float4ConstructorProxy),
            asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("Float4", asBEHAVE_CONSTRUCT, "void f(float value)",
            asFUNCTION(Float4ConstructorProxySingle), asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("Float4", asBEHAVE_CONSTRUCT,
            "void f(float x, float y, float z, float w)",
            asFUNCTION(Float4ConstructorProxyAll), asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("Float4", asBEHAVE_CONSTRUCT,
            "void f(const Float4 &in other)",
            asFUNCTION(Float4ConstructorProxyCopy), asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("Float4", asBEHAVE_DESTRUCT, "void f()",
            asFUNCTION(Float4DestructorProxy),
            asCALL_CDECL_OBJFIRST) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
    // Operators //
    if(engine->RegisterObjectMethod("Float4", "Float4& opAssign(const Float4 &in other)",
            asMETHODPR(Float4, operator=, (const Float4&), Float4&), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
    if(engine->RegisterObjectMethod("Float4", "Float4 opAdd(const Float4 &in other) const",
            asMETHODPR(Float4, operator+, (const Float4&) const, Float4), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectMethod("Float4", "Float4 opSub(const Float4 &in other) const",
            asMETHODPR(Float4, operator-, (const Float4&) const, Float4), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectMethod("Float4", "Float4 opMul(float multiply) const",
            asMETHODPR(Float4, operator*, (float) const, Float4), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

    // Direct access
    if(engine->RegisterObjectProperty("Float4", "float X", asOFFSET(Float4, X)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Float4", "float Y", asOFFSET(Float4, Y)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Float4", "float Z", asOFFSET(Float4, Z)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Float4", "float W", asOFFSET(Float4, W)) < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }
    
    // ObjectID bind
    // if(engine->RegisterObjectType("ObjectID", sizeof(int), asOBJ_VALUE |
    //         asGetTypeTraits<int>() | asOBJ_APP_CLASS_ALLINTS) < 0)
    // {
    //     ANGELSCRIPT_REGISTERFAIL;
    // }
    if(engine->RegisterTypedef("ObjectID", "int") < 0){

        ANGELSCRIPT_REGISTERFAIL;
    }

	// Bind NamedVars //

	// \todo make this safe to be passed to the script //
	if(engine->RegisterObjectType("NamedVars", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("NamedVars", asBEHAVE_ADDREF, "void f()", asMETHOD(NamedVars, AddRefProxy),
        asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("NamedVars", asBEHAVE_RELEASE, "void f()", asMETHOD(NamedVars, ReleaseProxy),
            asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}



	// bind event type enum //
	if(engine->RegisterEnum("EVENT_TYPE") < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
#define REGISTEREVENTTYPE(x)	{if(engine->RegisterEnumValue("EVENT_TYPE", #x, x) < 0){ANGELSCRIPT_REGISTERFAIL;}}

	REGISTEREVENTTYPE(EVENT_TYPE_SHOW);
	REGISTEREVENTTYPE(EVENT_TYPE_HIDE);
	REGISTEREVENTTYPE(EVENT_TYPE_TICK);
	REGISTEREVENTTYPE(EVENT_TYPE_LISTENERVALUEUPDATED);


	// bind event //
	if(engine->RegisterObjectType("Event", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Event", asBEHAVE_ADDREF, "void f()", asMETHOD(Event, AddRefProxy),
            asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("Event", asBEHAVE_RELEASE, "void f()", asMETHOD(Event, ReleaseProxy),
            asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectBehaviour("Event", asBEHAVE_FACTORY, "Event@ f(EVENT_TYPE type)",
            asFUNCTION(WrapperEventFactory), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

	// bind generic event //
	if(engine->RegisterObjectType("GenericEvent", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("GenericEvent", asBEHAVE_ADDREF, "void f()", asMETHOD(GenericEvent, AddRefProxy),
            asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("GenericEvent", asBEHAVE_RELEASE, "void f()",
            asMETHOD(GenericEvent, ReleaseProxy), asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	// Factory //
	if(engine->RegisterObjectBehaviour("GenericEvent", asBEHAVE_FACTORY, "GenericEvent@ f(const string &in typename)",
            asFUNCTION(WrapperGenericEventFactory), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	// Data get function //
	if(engine->RegisterObjectMethod("GenericEvent", "NamedVars@ GetNamedVars()",
            asMETHOD(GenericEvent, GetNamedVarsRefCounted), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Event handler which cannot be instantiated or copied around //
	if(engine->RegisterObjectType("EventHandler", 0, asOBJ_REF | asOBJ_NOHANDLE) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Global get function //
	if(engine->RegisterGlobalFunction("EventHandler& GetEventHandler()", asFUNCTION(EventHandler::Get),
            asCALL_CDECL) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Script event firing //
	if(engine->RegisterObjectMethod("EventHandler", "void CallEvent(GenericEvent@ event)",
            asMETHOD(EventHandler, CallEventGenericProxy), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

    // Bind EventListener //
	if(engine->RegisterObjectType("EventListener", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectBehaviour("EventListener", asBEHAVE_ADDREF, "void f()",
            asMETHOD(EventListener, AddRefProxy), asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	if(engine->RegisterObjectBehaviour("EventListener", asBEHAVE_RELEASE, "void f()",
            asMETHOD(EventListener, ReleaseProxy), asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterFuncdef("int OnEventCallback(Event@ event)") < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
    
    if(engine->RegisterFuncdef("int OnGenericEventCallback(GenericEvent@ event)") < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

	if(engine->RegisterObjectBehaviour("EventListener", asBEHAVE_FACTORY,
            "EventListener@ f(OnEventCallback@ onevent, OnGenericEventCallback@ ongeneric)",
            asFUNCTION(EventListenerFactory), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectMethod("EventListener", "bool RegisterForEvent(EVENT_TYPE type)",
            asMETHOD(EventListener, RegisterForEventType), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectMethod("EventListener", "bool RegisterForEvent(const string &in name)",
            asMETHOD(EventListener, RegisterForEventGeneric), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

    
    

	// bind datablock //
	if(engine->RegisterObjectType("ScriptSafeVariableBlock", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_ADDREF, "void f()",
            asMETHOD(ScriptSafeVariableBlock, AddRefProxy), asCALL_THISCALL) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_RELEASE, "void f()",
            asMETHOD(ScriptSafeVariableBlock, ReleaseProxy), asCALL_THISCALL) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	// Some factories //
	if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_FACTORY,
            "ScriptSafeVariableBlock@ f(const string &in blockname, const string &in value)", 
		asFUNCTION(ScriptSafeVariableBlockFactoryString), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_FACTORY,
            "ScriptSafeVariableBlock@ f(const string &in blockname, float value)", 
            asFUNCTION(ScriptSafeVariableBlockFactoryGeneric<float>), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
    if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_FACTORY,
            "ScriptSafeVariableBlock@ f(const string &in blockname, int value)", 
            asFUNCTION(ScriptSafeVariableBlockFactoryGeneric<int>), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_FACTORY,
            "ScriptSafeVariableBlock@ f(const string &in blockname, double value)", 
            asFUNCTION(ScriptSafeVariableBlockFactoryGeneric<double>), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_FACTORY,
            "ScriptSafeVariableBlock@ f(const string &in blockname, int8 value)", 
            asFUNCTION(ScriptSafeVariableBlockFactoryGeneric<char>), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectBehaviour("ScriptSafeVariableBlock", asBEHAVE_FACTORY,
            "ScriptSafeVariableBlock@ f(const string &in blockname, bool value)", 
            asFUNCTION(ScriptSafeVariableBlockFactoryGeneric<bool>), asCALL_CDECL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	// Implicit casts for normal types //
    if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "string opImplConv() const",
            WRAP_MFN(ScriptSafeVariableBlock, ConvertAndReturnVariable<string>),
            asCALL_GENERIC) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "int opImplConv() const",
            WRAP_MFN(ScriptSafeVariableBlock, ConvertAndReturnVariable<int>),
            asCALL_GENERIC) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "int8 opImplConv() const",
            WRAP_MFN(ScriptSafeVariableBlock, ConvertAndReturnVariable<char>),
            asCALL_GENERIC) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "float opImplConv() const",
            WRAP_MFN(ScriptSafeVariableBlock, ConvertAndReturnVariable<float>),
            asCALL_GENERIC) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "double opImplConv() const",
            WRAP_MFN(ScriptSafeVariableBlock, ConvertAndReturnVariable<double>),
            asCALL_GENERIC) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "bool opImplConv() const",
            WRAP_MFN(ScriptSafeVariableBlock, ConvertAndReturnVariable<bool>),
            asCALL_GENERIC) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

	// type check //
	if(engine->RegisterObjectMethod("ScriptSafeVariableBlock", "bool IsValidType()",
            asMETHOD(ScriptSafeVariableBlock, IsValidType), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}


	if(engine->RegisterObjectMethod("NamedVars", "ScriptSafeVariableBlock@ GetSingleValueByName(const string &in name)",
            asMETHOD(NamedVars, GetScriptCompatibleValue), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

	if(engine->RegisterObjectMethod("NamedVars", "bool AddValue(ScriptSafeVariableBlock@ value)",
            asMETHOD(NamedVars, AddScriptCompatibleValue), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}


	// Bind SimpleDataBase //
	if(engine->RegisterObjectType("SimpleDatabase", 0, asOBJ_REF | asOBJ_NOHANDLE) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Bind GameModule //
	if(engine->RegisterObjectType("GameModule", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("GameModule", asBEHAVE_ADDREF, "void f()", asMETHOD(GameModule, AddRefProxy),
        asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("GameModule", asBEHAVE_RELEASE, "void f()", asMETHOD(GameModule, ReleaseProxy),
            asCALL_THISCALL) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	// Bind simple name get function //
	if(engine->RegisterObjectMethod("GameModule", "string GetDescription(bool full = false)",
            asMETHOD(GameModule, GetDescription), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Bind GameWorld //
    // TODO: add reference counting for GameWorld
	if(engine->RegisterObjectType("GameWorld", 0, asOBJ_REF | asOBJ_NOCOUNT) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
    
	// Bind the ray cast function //
	// Result class for ray cast //
	if(engine->RegisterObjectType("RayCastHitEntity", 0, asOBJ_REF) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("RayCastHitEntity", asBEHAVE_ADDREF, "void f()",
            asMETHOD(RayCastHitEntity, AddRefProxy), asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectBehaviour("RayCastHitEntity", asBEHAVE_RELEASE, "void f()",
            asMETHOD(RayCastHitEntity, ReleaseProxy), asCALL_THISCALL) < 0)
    {
		ANGELSCRIPT_REGISTERFAIL;
	}
	if(engine->RegisterObjectMethod("RayCastHitEntity", "Float3 GetPosition()",
            asMETHOD(RayCastHitEntity, GetPosition), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}


	if(engine->RegisterObjectMethod("GameWorld", "RayCastHitEntity@ CastRayGetFirstHit(Float3 start, Float3 end)",
            asMETHOD(GameWorld, CastRayGetFirstHitProxy), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Bind the NewtonBody as non counted handle and don't register any methods since it will only be used to compare the pointer //
	if(engine->RegisterObjectType("NewtonBody", 0, asOBJ_REF | asOBJ_NOCOUNT) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

	// Compare function //
	if(engine->RegisterObjectMethod("RayCastHitEntity", "bool DoesBodyMatchThisHit(NewtonBody@ body)",
            asMETHOD(RayCastHitEntity, DoesBodyMatchThisHit), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}



    // AINetworkCache
    if(engine->RegisterObjectType("AINetworkCache", 0, asOBJ_REF | asOBJ_NOHANDLE) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}
    
    // TODO: register get function for interface
	//if(engine->RegisterGlobalFunction("AINetworkCache& GetAINetworkCache()", asFUNCTION(AINetworkCache::Get),
 //           asCALL_CDECL) < 0)
 //   {
	//	ANGELSCRIPT_REGISTERFAIL;
	//}

	if(engine->RegisterObjectMethod("AINetworkCache", "ScriptSafeVariableBlock@ GetVariable(const string &in name)",
            asMETHOD(NetworkCache, GetVariableWrapper), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

	if(engine->RegisterObjectMethod("AINetworkCache", "void SetVariable(ScriptSafeVariableBlock@ variable)",
            asMETHOD(NetworkCache, SetVariableWrapper), asCALL_THISCALL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}    
    

	// ------------------ Game entities ------------------ //

    // Physics
    if(engine->RegisterObjectType("Physics", 0, asOBJ_REF | asOBJ_NOCOUNT) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

    // Currently does nothing
    if(engine->RegisterObjectProperty("Physics", "bool Marked",
            asOFFSET(Physics, Marked)) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }
    
    if(engine->RegisterObjectMethod("Physics", "Float3 GetVelocity() const",
            asMETHODPR(Physics, GetVelocity, () const, Float3), asCALL_THISCALL) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("Physics", "NewtonBody@ GetBody() const",
            asMETHODPR(Physics, GetBody, () const, NewtonBody*), asCALL_THISCALL) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    // Position
    if(engine->RegisterObjectType("Position", 0, asOBJ_REF | asOBJ_NOCOUNT) < 0){
		ANGELSCRIPT_REGISTERFAIL;
	}

    if(engine->RegisterObjectProperty("Position", "bool Marked",
            asOFFSET(Position, Marked)) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Position", "Float3 _Position",
            asOFFSET(Position, Members._Position)) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectProperty("Position", "Float4 _Orientation",
            asOFFSET(Position, Members._Orientation)) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    // TrackController
 //   if(engine->RegisterObjectType("TrackController", 0, asOBJ_REF | asOBJ_NOCOUNT) < 0){
	//	ANGELSCRIPT_REGISTERFAIL;
	//}

 //   if(engine->RegisterObjectProperty("TrackController", "bool Marked",
 //           asOFFSET(TrackController, Marked)) < 0)
 //   {
 //       ANGELSCRIPT_REGISTERFAIL;
 //   }
 //   
 //   if(engine->RegisterObjectMethod("TrackController",
 //           "bool GetNodePosition(int index, Float3 &out pos, Float4 &out rot) const",
 //           asMETHODPR(TrackController, GetNodePosition, (int, Float3&, Float4&) const, bool),
 //           asCALL_THISCALL) < 0)
 //   {
 //       ANGELSCRIPT_REGISTERFAIL;
 //   }


    // GameWorld
    if(engine->RegisterObjectMethod("GameWorld", "Physics@ GetComponentPhysics(ObjectID id)",
            asMETHODPR(GameWorld, GetComponent<Physics>, (ObjectID), Physics&),
            asCALL_THISCALL) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }

    if(engine->RegisterObjectMethod("GameWorld", "Position@ GetComponentPosition(ObjectID id)",
            asMETHODPR(GameWorld, GetComponent<Position>, (ObjectID), Position&),
            asCALL_THISCALL) < 0)
    {
        ANGELSCRIPT_REGISTERFAIL;
    }
    

	// ------------------ Global functions ------------------ //

#ifdef _DEBUG


	if(engine->RegisterGlobalFunction("void DumpMemoryLeaks()", asFUNCTION(Engine::DumpMemoryLeaks), asCALL_CDECL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}

	

#endif // _DEBUG


	if(engine->RegisterGlobalFunction("string GetLeviathanVersion()",
            asFUNCTION(GetLeviathanVersionProxy), asCALL_CDECL) < 0)
	{
		ANGELSCRIPT_REGISTERFAIL;
	}




	return true;
}

void RegisterEngineScriptTypes(asIScriptEngine* engine, std::map<int, string> &typeids){

	typeids.insert(make_pair(engine->GetTypeIdByDecl("Event"), "Event"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("GenericEvent"), "GenericEvent"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("ScriptSafeVariableBlock"),
            "ScriptSafeVariableBlock"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("NamedVars"), "NamedVars"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("SimpleDatabase"), "SimpleDatabase"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("Float3"), "Float3"));
    typeids.insert(make_pair(engine->GetTypeIdByDecl("Float4"), "Float4"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("GameModule"), "GameModule"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("GameWorld"), "GameWorld"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("RayCastHitEntity"), "RayCastHitEntity"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("BaseObject"), "BaseObject"));
    typeids.insert(make_pair(engine->GetTypeIdByDecl("EventListener"), "EventListener"));
	typeids.insert(make_pair(engine->GetTypeIdByDecl("TrackController"), "TrackController"));
    typeids.insert(make_pair(engine->GetTypeIdByDecl("Physics"), "Physics"));
    typeids.insert(make_pair(engine->GetTypeIdByDecl("Position"), "Position"));
}
