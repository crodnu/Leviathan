// Leviathan Game Engine
// Copyright (c) 2012-2018 Henri Hyyryläinen
#pragma once
#include "Define.h"
// ------------------------------------ //
#include "GuiObjectExtraParameters.h"

#include "../ObjectFiles/ObjectFile.h"
#include "Common/ReferenceCounted.h"
#include "Events/Event.h"
#include "Events/EventableScriptObject.h"
#include "GuiCollection.h"
#include "Script/ScriptArgumentsProvider.h"
#include "Script/ScriptScript.h"
#include <functional>

#include "CEGUI/Event.h"

namespace Leviathan { namespace GUI {


//! \brief Represents a single GUI element that can use scripts to react to events
//! \todo Add a script module destruction queue
class BaseGuiObject : public ReferenceCounted,
                      public EventableScriptObject,
                      public ScriptArgumentsProvider,
                      public ThreadSafe {
    friend GuiManager;

public:
    DLLEXPORT BaseGuiObject(GuiManager* owner, const std::string& name, int fakeid,
        std::shared_ptr<ScriptScript> script = nullptr);
    DLLEXPORT virtual ~BaseGuiObject();

    DLLEXPORT ScriptSafeVariableBlock* GetAndPopFirstUpdated()
    {
        if(UpdatedValues.empty())
            return NULL;

        auto tmp = new ScriptSafeVariableBlock(
            UpdatedValues[0]->GetValueDirect(), UpdatedValues[0]->GetName());
        UpdatedValues.erase(UpdatedValues.begin());

        return tmp;
    }

    DLLEXPORT inline int GetID()
    {
        return ID;
    }


    DLLEXPORT GuiManager* GetOwningManager()
    {
        return OwningInstance;
    }

    DLLEXPORT static bool LoadFromFileStructure(GuiManager* owner,
        std::vector<BaseGuiObject*>& tempobjects, ObjectFileObject& dataforthis,
        const ExtraParameters& extra);


    //! \brief Sets this objects target CEGUI widget
    //!
    //! This will also register the widget for unconnect events to not use deleted pointers
    DLLEXPORT void ConnectElement(CEGUI::Window* windowobj);

    //! \brief Gets the name of this object
    DLLEXPORT std::string GetName();


    //! \brief Releases the data held onto by this object
    DLLEXPORT void ReleaseData();

    //! \brief Returns the TargetElement CEGUI window which might be NULL
    DLLEXPORT CEGUI::Window* GetTargetWindow() const;

    //! \brief Returns true if at least one CEGUI event is hooked
    DLLEXPORT bool IsCEGUIEventHooked() const;


    //! \brief Prints the window layout starting from TargetElement
    //! \param target The target window or NULL if TargetElement should be used
    //! \note Passing only the guard will work if you want to start from the target element
    DLLEXPORT void PrintWindowsRecursive(
        Lock& guard, CEGUI::Window* target = NULL, size_t level = 0) const;

    //! \brief No parameters version of PrintWindowsRecursive
    DLLEXPORT FORCE_INLINE void PrintWindowsRecursive() const
    {
        GUARD_LOCK();
        PrintWindowsRecursive(guard);
    }

    //! \brief Proxy for PrintWindowsRecursive
    DLLEXPORT void PrintWindowsRecursiveProxy();

    DLLEXPORT virtual std::unique_ptr<ScriptRunningSetup> GetParametersForInit() override;

    DLLEXPORT virtual std::unique_ptr<ScriptRunningSetup> GetParametersForRelease() override;

    REFERENCE_COUNTED_PTR_TYPE_NAMED(BaseGuiObject, GuiObject);

protected:
    virtual ScriptRunResult<int> _DoCallWithParams(
        ScriptRunningSetup& sargs, Event* event, GenericEvent* event2) override;

    // this function will try to hook all wanted listeners to CEGUI elements //
    void _HookListeners();

    //! \brief Registers for an event if it is a CEGUI event
    bool _HookCEGUIEvent(const std::string& listenername);


    //! \brief Clears CEGUIRegisteredEvents and unsubscribes from all
    void _UnsubscribeAllEvents(Lock& guard);


    //! \brief Calls the script for a specific CEGUI event listener
    //! \return The scripts return value changed to an int
    bool _CallCEGUIListener(const std::string& name,
        std::function<void(std::vector<std::shared_ptr<NamedVariableBlock>>&)> extraparam =
            NULL);

    std::unique_ptr<ScriptRunningSetup> _GetArgsForAutoFunc();


    // ------------------------------------ //

    int ID;
    int FileID;

    std::string Name;

    GuiManager* OwningInstance;

    //! The element that this script wrapper targets
    CEGUI::Window* TargetElement = nullptr;


    //! List of registered CEGUI events. This is used for unsubscribing
    std::vector<CEGUI::Event::Connection> CEGUIRegisteredEvents;

    // ------------------------------------ //
    //! This map collects all the available CEGUI events which can be hooked into
    static std::map<std::string, const CEGUI::String*> CEGUIEventNames;

public:
    //! \brief Frees CEGUIEventNames
    //!
    //! Only call this right before the Engine shuts down
    static void ReleaseCEGUIEventNames();

    //! \brief Constructs CEGUIEventNames
    //!
    //! This is safe to call at any time since the map is only filled once
    static void MakeSureCEGUIEventsAreFine(Lock& locked);


    //! The mutex required for MakeSureCEGUIEventsAreFine
    static Mutex CEGUIEventMutex;


protected:
    bool EventDestroyWindow(const CEGUI::EventArgs& args);

    bool EventOnClick(const CEGUI::EventArgs& args);

    bool EventOnCloseClicked(const CEGUI::EventArgs& args);

    bool EventOnListSelectionAccepted(const CEGUI::EventArgs& args);
};

}} // namespace Leviathan::GUI
